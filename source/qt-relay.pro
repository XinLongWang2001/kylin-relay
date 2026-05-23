QT       += core gui widgets network
CONFIG   += c++14
TARGET    = qt-relay
TEMPLATE  = app

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    tcpserver.cpp \
    commandexecutor.cpp

HEADERS += \
    mainwindow.h \
    tcpserver.h \
    commandexecutor.h \
    config.h

unix {
    target.path = /usr/local/bin
    INSTALLS += target
}
