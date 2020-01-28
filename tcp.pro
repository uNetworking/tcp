TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
    context.c \
    loop.c \
    main.c \
    socket.c

HEADERS += \
    Packets.h \
    internal.h

QMAKE_CXXFLAGS += -fsanitize=address
LIBS += -lasan
