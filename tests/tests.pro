QT += core gui quick testlib
CONFIG += testcase c++17
TEMPLATE = app
TARGET = tst_writer

INCLUDEPATH += ../src
SOURCES += \
    tst_writer.cpp \
    ../src/backend.cpp \
    ../src/markdownhighlighter.cpp
HEADERS += \
    ../src/backend.h \
    ../src/markdownhighlighter.h

QT += widgets printsupport quickcontrols2 quickdialogs2 dbus
