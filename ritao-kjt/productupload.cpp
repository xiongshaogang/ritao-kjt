#include "productupload.h"

#include <QSqlQuery>
#include <QVariant>
#include <QDebug>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QSqlError>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QFile>
#include <QNetworkRequest>
#include <QNetworkReply>

#include "global.h"

ProductUpload::ProductUpload(QObject *parent) : QObject(parent)
{
    _timer = new QTimer;
    connect(_timer, SIGNAL(timeout()), this, SLOT(sTimeout()));

    _manager = new QNetworkAccessManager;
    connect(_manager, SIGNAL(finished(QNetworkReply*)), this, SLOT(sReplyFinished(QNetworkReply*)));
}

void ProductUpload::upload()
{
    _productIdQueue.clear();

    /// 获取需要新增到跨境通的商品KID列表
    QSqlQuery query(tr("select 同步主键KID from 数据同步 "
                       "where 跨境通=1 and 跨境通处理=0 and 同步指令='新增' and 同步表名='商品' "
                       "order by 数据同步KID "));
    while (query.next())
        _productIdQueue.enqueue(query.value(tr("同步主键KID")).toInt());

    qDebug() << _productIdQueue;

    _optType = OTProductUploading;
    _timer->start(1000);
}

void ProductUpload::sTimeout()
{
    _timer->stop();

    switch (_optType) {
    case OTProductUploading:
        uploadNextProduct();
        break;
    case OTProductUploadEnd:
        emit finished(true, tr("上传商品结束。"));
        break;
    default:
        break;
    }
}

void ProductUpload::uploadNextProduct()
{
    if (_productIdQueue.isEmpty())
    {
        _optType = OTProductUploadEnd;
        _timer->start(1000);
        return;
    }

    _ohData._currentProductId = _productIdQueue.dequeue();

    QSqlQuery query;
    query.prepare(tr("select * from 商品 "
                     "where 商品KID=:id "));
    query.bindValue(":id", _ohData._currentProductId);
    if (!query.exec())
    {
//        _optType = OTOrderUploadError;
//        _msg = tr("订单");
//        qFatal(query.lastError().text().toStdString().c_str());
        qInfo() << query.lastError().text();
        _timer->start(1000);
        return;
    }

    if (query.first())
    {
        /// 商品属于保税仓（p1=1），则上传，否则跳过
        if (1 != query.value("p1").toInt())
        {
            _timer->start(1000);
            return;
        }

        QMap<QString, QString> paramsMap(g_paramsMap);
        paramsMap["method"] = "Product.ProductCreate";             // 由接口提供方指定的接口标识符
        paramsMap["timestamp"] = QDateTime::currentDateTime().toString("yyyyMMddhhmmss");       // 调用方时间戳，格式为“4 位年+2 位月+2 位日+2 位小时(24 小时制)+2 位分+2 位秒”
        paramsMap["nonce"] = QString::number(100000 + qrand() % (999999 - 100000)); // QString::number(100000 + qrand() % (999999 - 100000));

        QJsonObject json;
        json["IsSettledDown"] = query.value(tr("入住商品")).toInt();                // 是否为入驻商品。0 = 否 1 = 是
        json["MerchantProductID"] = query.value(tr("商品KID")).toString();         // 商户商品 ID
        json["ProductName"] = query.value(tr("商品名称")).toString();               // 商品名称
        json["BriefName"] = "HW";   // query.value(tr("品名简称")).toString();                 // 商品简称  //
        json["BrandCode"] = "950"; // query.value(tr("商品品牌ID")).toString();     // 品牌编号 code
        json["C3Code"] = "A46"; // query.value(tr("商品分类ID")).toString();        // 三级分类 code
        json["ProductTradeType"] = query.value(tr("贸易类型")).toInt();             // 贸易类型  0 = 直邮 1 = 自贸
        json["OriginCode"] = "JP"; // query.value(tr("产地")).toString();                   // 产地，两位字母     //
        json["ProductDesc"] = query.value(tr("商品简述")).toString();               // 商品简述

        QJsonObject productPriceInfoJsonObject;                     // 商品价格信息
        productPriceInfoJsonObject["CurrentPrice"] = query.value(tr("销售价")).toDouble();
        json["ProductPriceInfo"] = productPriceInfoJsonObject;

        QJsonObject productEntryInfoJsonObject;                     // 商品备案信息
        productEntryInfoJsonObject["ProductNameEN"] = query.value(tr("商品英文名称")).toString();
        productEntryInfoJsonObject["Specifications"] = tr("30＊10片");   // ?? 30*10 // query.value(tr("商品规格")).toString();
        productEntryInfoJsonObject["TaxUnit"] = "g";    // query.value(tr("计税单位")).toString(); // 计税单位, 不能为空!!     //
        /// 海关关区根据商品所入仓库对应的四位数关区代码填写
        /// 2244 – 直邮进口模式
        /// 2216 – 浦东机场自贸模式
        /// 2249 – 洋山港自贸模式
        /// 2218 – 外高桥自贸模式
        productEntryInfoJsonObject["CustomsCode"] = "2244"; // query.value(tr("关区代码")).toString();  // 海关关区根据商品所入仓库对应的四位数关区代码填写        //
        productEntryInfoJsonObject["StoreType"] = 0;    // query.value(tr("运输方式")).toInt();         // 运输方式（默认0，常温） 0 = 常温 1 = 冷藏 2 = 冷冻     //
        productEntryInfoJsonObject["ApplyUnit"] = "123";    // query.value(tr("申报单位")).toString();  // 申报单位, 不能为空        //
        productEntryInfoJsonObject["ApplyQty"] = 123;   // query.value(tr("申报数量")).toInt();  // 申报数量, 不能为空        //
        productEntryInfoJsonObject["GrossWeight"] = 12.0;   // query.value(tr("商品毛重")).toDouble();     //
        productEntryInfoJsonObject["SuttleWeight"] = 10.0;  // query.value(tr("商品净重")).toDouble();    //
        json["ProductEntryInfo"] = productEntryInfoJsonObject;

        QJsonObject productMaintainInfoJsonObject;                      // 商品维护信息
        productMaintainInfoJsonObject["ProductModel"] = "123";  // query.value(tr("商品型号")).toString();     //
        productMaintainInfoJsonObject["Weight"] = 10.0; // query.value(tr("商品物流重量")).toDouble();     //
        productMaintainInfoJsonObject["Length"] = query.value(tr("长度")).toDouble();
        productMaintainInfoJsonObject["Width"] = query.value(tr("宽度")).toDouble();
        productMaintainInfoJsonObject["Height"] = query.value(tr("高度")).toDouble();
        json["ProductMaintainInfo"] = productMaintainInfoJsonObject;

//        qInfo() << tr("下单用户真实姓名: ") << query.value(tr("个人姓名")).toString();

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

        qDebug() << params;
        QString sign = QCryptographicHash::hash(QString(params + kjt_secretkey).toLatin1(), QCryptographicHash::Md5).toHex();
        params.append("sign=").append(sign);

        QNetworkRequest req;
        req.setUrl(QUrl(kjt_url));
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
        _manager->post(req, params.toLatin1());
    }
}

void ProductUpload::sReplyFinished(QNetworkReply *reply)
{
    QByteArray replyData = reply->readAll();

    QJsonObject json(QJsonDocument::fromJson(replyData).object());
    QString code = json.value("Code").toString("-99");
    QString desc = json.value("Desc").toString();

    QString opt;
    switch (_optType) {
    case OTProductUploading:
        opt = tr("新增商品");
        if (code == "0")
        {
            /// 记录同步数据，并进行下一个跨境通同步
            QSqlQuery query;
            query.prepare(tr("update 数据同步 set 跨境通处理=1 "
                             "where 跨境通=1 and 同步指令='新增' and 同步表名='商品' and 同步主键KID=:id "));
            query.bindValue(":id", _ohData._currentProductId);
            query.exec();

//            outputSOWarehouse();      // 不需要了

            _timer->start(1000);
        }
        break;
//    case STOrderCreateKJTToERP:
//        opt = tr("获取订单");
//        _orderCreateKJTToERPData._replyData._code = code;
//        _orderCreateKJTToERPData._replyData._desc = desc;
//        if ("0" == code)
//        {
//            /// 读取订单id列表
//            QJsonObject data = json.value("Data").toObject();
//            _orderCreateKJTToERPData._total = data.value("Total").toInt();
//            QJsonArray orderIdListArray = data.value("OrderIDList").toArray();
//            QList<int> orderIdList;
//            for (int i = 0; i < orderIdListArray.size(); i++)
//                orderIdList.append(orderIdListArray.at(i).toInt());
//            _orderCreateKJTToERPData._orderIdList = orderIdList;
//            qDebug() << "orderIdList:" << _orderCreateKJTToERPData._orderIdList;

//            _orderCreateKJTToERPData._currentIndex = 0;
//            orderInfoBatchGet();
//        }
//        break;
//    case STOrderInfoBatchGet:
//        opt = tr("下载订单");
//        /// 解析数据，写入ERP数据库，并继续请求数据
//        if ("0" == code)
//        {
//            QJsonObject data = json.value("Data").toObject();
//            QJsonArray orderListArray = data.value("OrderList").toArray();
//            for (int i = 0; i < orderListArray.size(); i++)
//                insertOrder2ERPByJson(orderListArray.at(i).toObject());
//        }
//        _timer->start(1000);
//        break;
    default:
        break;
    }

    qInfo() << opt << code << desc;
}
