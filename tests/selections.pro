QT += widgets testlib
CONFIG += c++17 testcase release
CONFIG -= debug
TARGET = photodeck-selections-tests
SOURCES += test_selections.cpp ../src/document.cpp ../src/canvas.cpp ../src/mainwindow.cpp ../src/appearance.cpp ../src/tones.cpp ../src/tonedialog.cpp ../src/selection.cpp
HEADERS += ../src/document.h ../src/canvas.h ../src/mainwindow.h ../src/appearance.h ../src/tones.h ../src/tonedialog.h ../src/selection.h
INCLUDEPATH += ../src
RESOURCES += ../resources.qrc
msvc:QMAKE_CXXFLAGS += /utf-8
SOURCES += ../src/blending.cpp
HEADERS += ../src/blending.h

SOURCES += ../src/transformdialog.cpp
HEADERS += ../src/transformdialog.h
