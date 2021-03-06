#-------------------------------------------------
#
# Project created by QtCreator 2015-12-30T16:17:47
#
#-------------------------------------------------

QT       += core gui
QT += network sql

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = ritao-kjt
TEMPLATE = app

DESTDIR = ../ritao_desktop_bin_kjt

SOURCES += main.cpp\
        mainwindow.cpp \
    ordercreatekjttoerp.cpp \
    global.cpp \
    rnetworkaccessmanager.cpp \
    orderupload.cpp \
    productupload.cpp \
    orderdownload.cpp \
    productdownload.cpp \
    productpricedownload.cpp \
    inventorychannelq4sbatchget.cpp \
    configglobal.cpp \
    kjtsynchronize.cpp

HEADERS  += mainwindow.h \
    connection.h \
    global.h \
    ordercreatekjttoerp.h \
    rnetworkaccessmanager.h \
    orderupload.h \
    productupload.h \
    orderdownload.h \
    productdownload.h \
    productpricedownload.h \
    inventorychannelq4sbatchget.h \
    configglobal.h \
    kjtsynchronize.h

FORMS    += mainwindow.ui
