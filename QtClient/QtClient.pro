QT += core gui network widgets

CONFIG += c++17

SOURCES += \
    chatwindow.cpp \
    main.cpp

HEADERS += \
    chatwindow.h \
    protocol.h

TARGET = QtClient
TEMPLATE = app
