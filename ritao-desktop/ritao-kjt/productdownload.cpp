#include "productdownload.h"

#include "global.h"
#include "inventorychannelq4sbatchget.h"
#include "configglobal.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>
#include <QTimer>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QUrl>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QFile>

ProductDownload::ProductDownload(QObject *parent) : QObject(parent),
    _optType(OTNone)
{
    _timer = new QTimer;
    connect(_timer, SIGNAL(timeout()), this, SLOT(sTimeout()));

    _manager = new QNetworkAccessManager;
    connect(_manager, SIGNAL(finished(QNetworkReply*)), this, SLOT(sReplyFinished(QNetworkReply*)));

    _inventoryChannelQ4SBatchGet = new InventoryChannelQ4SBatchGet;
    connect(_inventoryChannelQ4SBatchGet, SIGNAL(finished(bool,QString)), this, SLOT(sInventoryChannelQ4SBatchGetFinished(bool, QString)));
}

void ProductDownload::download()
{
    _optType = OTDownloadIdList;
    _phData._success = true;
    _phData._msgList.clear();

    _timer->start(1000);
}

void ProductDownload::sTimeout()
{
    _timer->stop();

    switch (_optType) {
    case OTDownloadIdList:
        downloadProductIdList();
        break;
    case OTProductDownloading:
        downloadNextProducts();
        break;
    case OTProductDownloadEnd:
        _inventoryChannelQ4SBatchGet->download();
//        emit finished(_phData._success, _phData._msgList.join("\r\n"));
        break;
    case OTProductDownloadError:
        emit finished(false, _msg);
    default:
        break;
    }
}

void ProductDownload::sReplyFinished(QNetworkReply *reply)
{
    QByteArray replyData = reply->readAll();

    QJsonObject json(QJsonDocument::fromJson(replyData).object());
    QString code = json.value("Code").toString("-99");
    QString desc = json.value("Desc").toString();

    QString opt;
    switch (_optType) {
    case OTDownloadIdList:
        if (code == "0")
        {
            /// 读取订单id列表
            QJsonObject data = json.value("Data").toObject();
            _phData._total = data.value("Total").toInt();
            QJsonArray productListArray = data.value("ProductList").toArray();
            QList<ProductSummary> productList;
            QList<QString> productIdList;
            for (int i = 0; i < productListArray.size(); i++)
            {
                QJsonObject productObject = productListArray.at(i).toObject();
                productList.append(ProductSummary(
                                       productObject.value("ProductID").toString(),
                                       productObject.value("Status").toInt(),
                                       QDateTime::fromString(productObject.value("ChangeDate").toString())
                                       ));
                productIdList.append(productObject.value("ProductID").toString());
            }
            _phData._productList = productList;
            _phData._currentIndex = 0;
            qDebug() << "total count:" << _phData._total;

            _inventoryChannelQ4SBatchGet->setProductIdList(productIdList);
            _optType = OTProductDownloading;
            _timer->start(1000);
        }
        else
        {
            _optType = OTProductDownloadError;
            _msg = tr("下载区间订单ID列表错误：") + desc;
            _timer->start(1000);
        }
        break;
    case OTProductDownloading:
        /// 解析数据，写入ERP数据库，并继续请求数据
        if ("0" == code)
        {
            QJsonObject data = json.value("Data").toObject();
            QJsonDocument jsonDoc(json);
            QFile file("data.txt");
            if (file.open(QIODevice::WriteOnly))
            {
                QTextStream out(&file);
                out << jsonDoc.toJson(QJsonDocument::Compact);
                file.close();
            }
            QJsonArray productListArray = data.value("ProductList").toArray();
            for (int i = 0; i < productListArray.size(); i++)
                insertProduct2ERPByJson(productListArray.at(i).toObject());
        }
        _timer->start(1000);
        break;
    default:
        break;
    }

    qInfo() << opt << code << desc;
}

void ProductDownload::sInventoryChannelQ4SBatchGetFinished(bool success, const QString &msg)
{
    if (!success)
    {
        qInfo() << "下载库存失败: " + msg;
        _phData._success = false;
        _phData._msgList.append(msg);
    }

    emit finished(_phData._success, _phData._msgList.join("\r\n"));
}

void ProductDownload::downloadProductIdList()
{
    QMap<QString, QString> paramsMap(g_paramsMap);
    paramsMap["method"] = "Product.ProductIDGetQuery";    // 由接口提供方指定的接口标识符
    paramsMap["timestamp"] = QDateTime::currentDateTime().toString("yyyyMMddhhmmss");       // 调用方时间戳，格式为“4 位年+2 位月+2 位日+2 位小时(24 小时制)+2 位分+2 位秒”
    paramsMap["nonce"] = QString::number(100000 + qrand() % (999999 - 100000)); // QString::number(100000 + qrand() % (999999 - 100000));

    /// 获取上次运行的结束日期，作为本次的起始日期
    QDateTime dateStart;
    QSqlQuery query;
    query.prepare(tr("select 参数内容Date from 系统参数 where 参数分组='跨境通' and 参数名称='商品下载结束时间'"));
    if (!query.exec())
        qInfo() << query.lastError().text();
    else if (query.first())
        dateStart = query.value(tr("参数内容Date")).toDateTime();

    if (!dateStart.isValid())
        dateStart = QDateTime(QDate(1900, 1, 1));
    qDebug() << dateStart;
    _phData._dateStart = dateStart;
    _phData._dateEnd = QDateTime::currentDateTime();

    QJsonObject json;
    json["SaleChannelSysNo"] = g_config.kjtSaleschannelsysno();
    json["ChangedDateBegin"] = dateStart.toString("yyyy-MM-dd hh:mm:ss");
    json["ChangedDateEnd"] = _phData._dateEnd.toString("yyyy-MM-dd hh:mm:ss");
    QJsonDocument jsonDoc(json);
    QFile file("11.txt");
    if (file.open(QIODevice::WriteOnly))
    {
        QTextStream out(&file);
        out << jsonDoc.toJson(QJsonDocument::Compact);
        file.close();
    }
    qDebug() << jsonDoc.toJson(QJsonDocument::Compact);
    paramsMap["data"] = jsonDoc.toJson(QJsonDocument::Compact);
    qDebug() << paramsMap;

    QString params;
    QMapIterator<QString, QString> i(paramsMap);
    while (i.hasNext())
    {
        i.next();
        params.append(i.key()).append("=").append(i.value().toUtf8().toPercentEncoding()).append("&");
    }

    params.append(g_config.kjtSecretkey());
    urlencodePercentConvert(params);
    qDebug() << params;
    QString sign = QCryptographicHash::hash(params.toLatin1(), QCryptographicHash::Md5).toHex();

    QString url;
    url.append(g_config.kjtUrl()).append("?").append(params).append("&sign=").append(sign);

    qDebug() << url;
    _manager->get(QNetworkRequest(QUrl(url)));
}

void ProductDownload::downloadNextProducts()
{
    if (_phData._currentIndex >= _phData._total)
    {
        setProductGetFromKJTTime();   // 执行结束，修改系统参数的时间

        _optType = OTProductDownloadEnd;
        if (_phData._success)
            _phData._msgList.append(tr("下载商品结束."));
        _timer->start(1000);
        return;
    }

    QMap<QString, QString> paramsMap(g_paramsMap);
    paramsMap["method"] = "Product.ProudctInfoBatchGet";    // 由接口提供方指定的接口标识符
    paramsMap["timestamp"] = QDateTime::currentDateTime().toString("yyyyMMddhhmmss");       // 调用方时间戳，格式为“4 位年+2 位月+2 位日+2 位小时(24 小时制)+2 位分+2 位秒”
    paramsMap["nonce"] = QString::number(100000 + qrand() % (999999 - 100000)); // QString::number(100000 + qrand() % (999999 - 100000));

    QJsonObject json;
    QJsonArray productIdListArray;
    foreach (ProductSummary product, _phData._productList.mid(_phData._currentIndex, 5)) {
        productIdListArray.append(QJsonValue(product._productId));
    }
    _phData._currentIndex += 5;
    json["ProductIDs"] = productIdListArray;
    json["SaleChannelSysNo"] = g_config.kjtSaleschannelsysno();
    QJsonDocument jsonDoc(json);
//    QFile file("11.txt");
//    if (file.open(QIODevice::WriteOnly))
//    {
//        QTextStream out(&file);
//        out << jsonDoc.toJson(QJsonDocument::Compact);
//        file.close();
//    }
    qDebug() << jsonDoc.toJson(QJsonDocument::Compact);

    paramsMap["data"] = jsonDoc.toJson(QJsonDocument::Compact);
    qDebug() << paramsMap;

    QString params;
    QMapIterator<QString, QString> i(paramsMap);
    while (i.hasNext())
    {
        i.next();
        params.append(i.key()).append("=").append(i.value().toUtf8().toPercentEncoding()).append("&");
    }

    params.append(g_config.kjtSecretkey());
    urlencodePercentConvert(params);
    qDebug() << params;
    QString sign = QCryptographicHash::hash(params.toLatin1(), QCryptographicHash::Md5).toHex();

    QString url;
    url.append(g_config.kjtUrl()).append("?").append(params).append("&sign=").append(sign);

    qDebug() << url;
    _manager->get(QNetworkRequest(QUrl(url)));
}

void ProductDownload::setProductGetFromKJTTime()
{
    if (!_phData._success) return;
    if (!_phData._dateStart.isValid() || !_phData._dateEnd.isValid()) return;

    QSqlQuery query;
    query.prepare(tr("update 系统参数 set 参数内容Date=:dateStart where 参数分组='跨境通' and 参数名称='商品下载起始时间'"));
    query.bindValue(":dateStart", _phData._dateStart);
    if (!query.exec())
    {
        qInfo() << query.lastError().text();
    }

    query.prepare(tr("update 系统参数 set 参数内容Date=:dateStart where 参数分组='跨境通' and 参数名称='商品下载结束时间'"));
    query.bindValue(":dateStart", _phData._dateEnd);
    if (!query.exec())
    {
        qInfo() << query.lastError().text();
    }
}

void ProductDownload::insertProduct2ERPByJson(const QJsonObject &json)
{
    QString productId = json.value("ProductID").toString();             // ProductID
    if (productId == "B43JPB01g670001")
        int iii = 0;
    int categoryID = json.value("CategoryID").toInt();                  // 商品类别ID
    /// 获取ERP中的类别ID
    int categoryIDERP = 0;
    QSqlQuery querycategoryIDERP;
    querycategoryIDERP.prepare(tr("select 商品分类KID from 商品分类 where 跨境通分类编码=:categoryID"));
    querycategoryIDERP.bindValue(":categoryID", QString::number(categoryID));
    if (querycategoryIDERP.exec())
        if (querycategoryIDERP.first())
            categoryIDERP = querycategoryIDERP.value("商品分类KID").toInt();
    if (categoryIDERP < 1)
    {
        querycategoryIDERP.prepare("select top 1 商品分类KID from 商品分类 where 商品分类名称 like '%其它%'");
        if (querycategoryIDERP.exec())
            if (querycategoryIDERP.first())
                categoryIDERP = querycategoryIDERP.value("商品分类KID").toInt();
    }
    QString categoryName = json.value("CategoryName").toString();       // 商品类别名称
    QString productName = json.value("ProductName").toString();         // 商品名称
    QString briefName = json.value("BriefName").toString();             // 商品简称
    QString productMode = json.value("ProductMode").toString();         // 商品型号
    QString productDesc = json.value("ProductDesc").toString();         // 商品简述
    double weight = json.value("Weight").toDouble();                    // 重量(单位:克)
    QString productDescLong = json.value("ProductDescLong").toString(); // 详细描述
    QString productPhotoDesc = json.value("ProductPhotoDesc").toString();   // 以图片方式展示的详细描述
    QString performance = json.value("Performance").toString();         // 详细规格
    QString warranty = json.value("Warranty").toString();               // 售后服务
    QString attention = json.value("Attention").toString();             // 购买须知
    QString defaultImage = json.value("DefaultImage").toString();       // 默认图片
    QString promotionTitle = json.value("PromotionTitle").toString();   // 促销标题
    QString keyWords = json.value("KeyWords").toString();               // 关键字
    int vendorID = json.value("VendorID").toInt();                      // 供应商编号
    QString vendorName = json.value("VendorName").toString();           // 供应商名称
    int brandID = json.value("BrandID").toInt();                        // 品牌编号
    /// 获取ERP中的品牌ID
    int brandIDERP = 0;
    QSqlQuery querybrandIDERP;
    querybrandIDERP.prepare(tr("select 商品品牌KID from 商品品牌 where 跨境通品牌编码=:brandID"));
    querybrandIDERP.bindValue(":brandID", QString::number(brandID));
    if (querybrandIDERP.exec())
        if (querybrandIDERP.first())
            brandIDERP = querybrandIDERP.value("商品品牌KID").toInt();
    if (brandIDERP < 1)
    {
        querybrandIDERP.prepare("select 商品品牌KID from 商品品牌 where 品牌名称 like '%其它%'");
        if (querybrandIDERP.exec())
            if (querybrandIDERP.first())
                brandIDERP = querybrandIDERP.value("商品品牌KID").toInt();
    }
    QString brandName = json.value("BrandName").toString();             // 品牌名称
    int productTradeType = json.value("ProductTradeType").toString("0").toInt();      // 贸易类型：0 = 直邮, 1 = 自贸
    int onlineQty = json.value("OnlineQty").toInt();                    // 渠道独占库存
    int platformQty = json.value("PlatformQty").toInt();                // 平台可售库存
    /// 价格：跨境通实际返回的是string，文档中却是double，需要先toString，再toDouble
    double price = json.value("Price").toString().toDouble();           // 渠道分销价格，
    int storeSysNo = json.value("StoreSysNo").toInt();                  // 店铺编号
//    QDateTime orderDate = convertKjtTime(json.value("OrderDate").toString());           // 订单时间

    QJsonObject productEntryInfoObject = json.value("ProductEntryInfo").toObject();         // 商品库存列表
    QString productName_EN = productEntryInfoObject.value("ProductName_EN").toString();     // 商品英文名称
    QString specifications = productEntryInfoObject.value("Specifications").toString();     // 规格
    QString functions = productEntryInfoObject.value("Functions").toString();               // 功能
    QString component = productEntryInfoObject.value("Component").toString();               // 成分
    QString origin = productEntryInfoObject.value("Origin").toString();                     // 产地
    QString purpose = productEntryInfoObject.value("Purpose").toString();                   // 用途
    QString taxUnit = productEntryInfoObject.value("TaxUnit").toString();                   // 计税单位
    QString applyUnit = productEntryInfoObject.value("ApplyUnit").toString();               // 申报单位
    double taxQty = productEntryInfoObject.value("TaxQty").toDouble();                      // 计税单位数量
    double grossWeight = productEntryInfoObject.value("GrossWeight").toDouble();            // 毛重
    int bizType = productEntryInfoObject.value("BizType").toInt();                          // 业务类型: 0 =一般进口, 1=保税进口
    double suttleWeight = productEntryInfoObject.value("SuttleWeight").toDouble();          // 净重
    QString note = productEntryInfoObject.value("Note").toString();                         // 其他备注
    double tariffRate = productEntryInfoObject.value("TariffRate").toString().toDouble();   // 税率
    QString entryCode = productEntryInfoObject.value("EntryCode").toString();               // 备案信息
    int productStoreType = productEntryInfoObject.value("ProductStoreType").toInt();        // 存储方式: 0 =常温, 1 =冷藏, 2=冷冻
    QDateTime manufactureDate = convertKjtTime(json.value("ManufactureDate").toString());   // 出厂日期
    QString originCountryName = productEntryInfoObject.value("OriginCountryName").toString();   // 出产国家



    qDebug() << productId << productName;
    if (productId == "B43JPB03z840001")
        int iii = 0;

    /// 仅下载付款后，待出库的订单
//    if (sOStatusCode != 1) return;

    QSqlQuery query;
    query.prepare(tr("select * from 商品 where p31=:productId"));
    query.bindValue(":productId", productId);
    if (!query.exec())
    {
        qInfo() << query.lastError().text();
        _phData._success = false;
        return;
    }
    /// 若存在，则更新；若不存在，则新增
    if (query.first())
    {
        QSqlQuery queryUpdate;
        queryUpdate.prepare("update 商品 set "
                            "商品分类ID=:categoryIDERP, 品名简称=:briefName, 商品型号=:productMode, 商品简述=:productDesc, "
                            "商品物流重量=:weight, 商品详细描述=:productDescLong, p32=:productPhotoDesc, p33=:performance, p34=:warranty, "
                            "p35=:attention, p7=:vendorID, p37=:vendorName, 商品品牌ID=:brandIDERP, 贸易类型=:productTradeType, "
                            "p5=:onlineQty, p6=:platformQty, 商品英文名称=:productName_EN, 商品规格=:specifications, "
                            "产地=:origin, 计税单位=:taxUnit, 申报单位=:applyUnit, 商品毛重=:grossWeight, 商品净重=:suttleWeight, "
                            "商品备注=:note, 商品关税=:tariffRate, p39=:originCountryName, 是否属于保税仓=:isBaoshuicang, p23=:p23, p28=:p28, "
                            "手机端详细描述=:productDescLong2, 商品状态=:productStatus, 决策审核=:decisionreview, 自定义ID1=:customid1, 创建日期=:createDate, "
                            "p22=:productId3, p8=:storeSysNo, p10=:categoryID, p40=:categoryName, p11=:brandID, p41=:brandName, 进项税=:jinxiangshui "
                            "where p31=:productId");
        queryUpdate.bindValue(":categoryIDERP", categoryIDERP);
        /// 商品名称=:productName  这个赋值被特殊处理，由于商品名称在第一次下载后由erp后台进行维护，不需要从跨境通进行更新
//        queryUpdate.bindValue(":productName", briefName);       // 将“商品名称”与“品名简称”进行对调赋值
        queryUpdate.bindValue(":briefName", productName);
        queryUpdate.bindValue(":productMode", productMode);
        queryUpdate.bindValue(":productDesc", productDesc);

        queryUpdate.bindValue(":weight", weight / 1000.0);
        queryUpdate.bindValue(":productDescLong", productDescLong);
        queryUpdate.bindValue(":productPhotoDesc", productPhotoDesc);
        queryUpdate.bindValue(":performance", performance);
        queryUpdate.bindValue(":warranty", warranty);

        queryUpdate.bindValue(":attention", attention);
        queryUpdate.bindValue(":vendorID", vendorID);
        queryUpdate.bindValue(":vendorName", vendorName);
        queryUpdate.bindValue(":brandIDERP", brandIDERP);
        queryUpdate.bindValue(":productTradeType", productTradeType);

        queryUpdate.bindValue(":onlineQty", onlineQty);
        queryUpdate.bindValue(":platformQty", platformQty);
        /// 销售价=:price  此字段不更新
//        queryUpdate.bindValue(":price", price);
        queryUpdate.bindValue(":productName_EN", productName_EN);
        queryUpdate.bindValue(":specifications", specifications);

        queryUpdate.bindValue(":origin", origin);
        queryUpdate.bindValue(":taxUnit", taxUnit);
        queryUpdate.bindValue(":applyUnit", applyUnit);
        queryUpdate.bindValue(":grossWeight", grossWeight);
        queryUpdate.bindValue(":suttleWeight", suttleWeight);

        queryUpdate.bindValue(":note", note);
        queryUpdate.bindValue(":tariffRate", 0);
        queryUpdate.bindValue(":originCountryName", originCountryName);
        queryUpdate.bindValue(":isBaoshuicang", 1);
        queryUpdate.bindValue(":p23", specifications);
        queryUpdate.bindValue(":p28", productTradeType == 0 ? "直邮" : "保税");

        queryUpdate.bindValue(":productDescLong2", productDescLong);
        queryUpdate.bindValue(":productStatus", 1);
        queryUpdate.bindValue(":decisionreview", 1);
        queryUpdate.bindValue(":customid1", 1);
        queryUpdate.bindValue(":createDate", QDateTime::currentDateTime());

        queryUpdate.bindValue(":productId3", productId);
        queryUpdate.bindValue(":storeSysNo", storeSysNo);
        queryUpdate.bindValue(":categoryID", categoryID);
        queryUpdate.bindValue(":categoryName", categoryName);
        queryUpdate.bindValue(":brandID", brandID);
        queryUpdate.bindValue(":brandName", brandName);
        queryUpdate.bindValue(":jinxiangshui", tariffRate);

        queryUpdate.bindValue(":productId", productId);
        if (!queryUpdate.exec())
        {
            qInfo() << "更新商品" + productId + "错误：" << queryUpdate.lastError().text();
            _phData._success = false;
        }
    }
    else
    {
        QSqlQuery queryInsert;
        queryInsert.prepare("insert into 商品( "
                            "p31, 商品分类ID, 商品名称, 商品编码, 品名简称, 商品型号, 商品简述, "
                            "商品物流重量, 商品详细描述, p32, p33, p34, "
                            "p35, p7, p37, 商品品牌ID, 贸易类型, "
                            "p5, p6, 销售价, 商品英文名称, 商品规格, "
                            "产地, 计税单位, 申报单位, 商品毛重, 商品净重, "
                            "商品备注, 商品关税, p39, 是否属于保税仓, p23, p28,"
                            "手机端详细描述, 商品状态, 决策审核, 自定义ID1, 创建日期, "
                            "p22, p8, p10, p40, p11, p41, 进项税 "
                            ") values ("
                            ":productId, :categoryIDERP, :productName, :productId2, :briefName, :productMode, :productDesc, "
                            ":weight, :productDescLong, :productPhotoDesc, :performance, :warranty, "
                            ":attention, :vendorID, :vendorName, :brandIDERP, :productTradeType, "
                            ":onlineQty, :platformQty, :price, :productName_EN, :specifications, "
                            ":origin, :taxUnit, :applyUnit, :grossWeight, :suttleWeight, "
                            ":note, :tariffRate, :originCountryName, :isBaoshuicang, :p23, :p28,"
                            ":productDescLong2, :productStatus, :decisionreview, :customid1, :createDate, "
                            ":productId3, :storeSysNo, :categoryID, :categoryName, :brandID, :brandName, :jinxiangshui "
                            ")");
        queryInsert.bindValue(":productId", productId);
        queryInsert.bindValue(":categoryIDERP", categoryIDERP);
        queryInsert.bindValue(":productName", briefName);     // 将“商品名称”与“品名简称”进行对调赋值
        queryInsert.bindValue(":productId2", productId);
        queryInsert.bindValue(":briefName", productName);
        queryInsert.bindValue(":productMode", productMode);
        queryInsert.bindValue(":productDesc", productDesc);

        queryInsert.bindValue(":weight", weight / 1000.0);
        queryInsert.bindValue(":productDescLong", productDescLong);
        queryInsert.bindValue(":productPhotoDesc", productPhotoDesc);
        queryInsert.bindValue(":performance", performance);
        queryInsert.bindValue(":warranty", warranty);

        queryInsert.bindValue(":attention", attention);
        queryInsert.bindValue(":vendorID", vendorID);
        queryInsert.bindValue(":vendorName", vendorName);
        queryInsert.bindValue(":brandIDERP", brandIDERP);
        queryInsert.bindValue(":productTradeType", productTradeType);

        queryInsert.bindValue(":onlineQty", onlineQty);
        queryInsert.bindValue(":platformQty", platformQty);
        queryInsert.bindValue(":price", price);
        queryInsert.bindValue(":productName_EN", productName_EN);
        queryInsert.bindValue(":specifications", specifications);

        queryInsert.bindValue(":origin", origin);
        queryInsert.bindValue(":taxUnit", taxUnit);
        queryInsert.bindValue(":applyUnit", applyUnit);
        queryInsert.bindValue(":grossWeight", grossWeight);
        queryInsert.bindValue(":suttleWeight", suttleWeight);

        queryInsert.bindValue(":note", note);
        queryInsert.bindValue(":tariffRate", 0.0);
        queryInsert.bindValue(":originCountryName", originCountryName);
        queryInsert.bindValue(":isBaoshuicang", 1);
        queryInsert.bindValue(":p23", specifications);
        queryInsert.bindValue(":p28", productTradeType == 0 ? "直邮" : "保税");

        queryInsert.bindValue(":productDescLong2", productDescLong);
        queryInsert.bindValue(":productStatus", 1);
        queryInsert.bindValue(":decisionreview", 1);
        queryInsert.bindValue(":customid1", 1);
        queryInsert.bindValue(":createDate", QDateTime::currentDateTime());

        queryInsert.bindValue(":productId3", productId);
        queryInsert.bindValue(":storeSysNo", storeSysNo);
        queryInsert.bindValue(":categoryID", categoryID);
        queryInsert.bindValue(":categoryName", categoryName);
        queryInsert.bindValue(":brandID", brandID);
        queryInsert.bindValue(":brandName", brandName);
        queryInsert.bindValue(":jinxiangshui", tariffRate);
        if (!queryInsert.exec())
        {
            qInfo() << "下载商品" + productId + "错误：" << queryInsert.lastError().text();
            _phData._success = false;
        }
    }
}

QDateTime ProductDownload::convertKjtTime(const QString &kjtTime)
{
    /// 跨镜通将会把日期修改为 yyyy-MM-dd hh:mm:ss 形式。到时再修改此处
    qint64 utcTime = kjtTime.mid(kjtTime.indexOf("(") + 1, 13).toLongLong();
    return QDateTime::fromMSecsSinceEpoch(utcTime);      // 订单时间
}
