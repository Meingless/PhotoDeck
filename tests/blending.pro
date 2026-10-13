QT += widgets testlib
CONFIG += c++17 testcase release
CONFIG -= debug
TARGET = photodeck-blending-tests
SOURCES += test_blending.cpp ../src/document.cpp ../src/canvas.cpp ../src/mainwindow.cpp ../src/appearance.cpp ../src/tones.cpp ../src/tonedialog.cpp ../src/selection.cpp ../src/blending.cpp
HEADERS += ../src/document.h ../src/canvas.h ../src/mainwindow.h ../src/appearance.h ../src/tones.h ../src/tonedialog.h ../src/selection.h ../src/blending.h
INCLUDEPATH += ../src
RESOURCES += ../resources.qrc
msvc:QMAKE_CXXFLAGS += /utf-8

SOURCES += ../src/transformdialog.cpp
HEADERS += ../src/transformdialog.h

SOURCES += ../src/filters.cpp
HEADERS += ../src/filters.h

SOURCES += ../src/effectdialog.cpp
HEADERS += ../src/effectdialog.h
