QT += core gui quick testlib widgets printsupport quickcontrols2
linux {
    QT += dbus
}
CONFIG += testcase c++17
TEMPLATE = app
TARGET = tst_writer

INCLUDEPATH += ../src
SOURCES += \
    tst_writer.cpp \
    ../src/backend.cpp \
    ../src/markdownhighlighter.cpp \
    ../src/systemtheme.cpp
HEADERS += \
    ../src/backend.h \
    ../src/markdownhighlighter.h \
    ../src/systemtheme.h
