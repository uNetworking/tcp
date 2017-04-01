TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += main.cpp \
    Tcp.cpp

HEADERS += \
    Tcp.h \
    IP.h
