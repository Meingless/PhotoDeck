QT += widgets testlib printsupport
CONFIG += c++17 testcase release
CONFIG -= debug
TARGET = photodeck-brushes-tests
SOURCES += test_brushes.cpp ../src/document.cpp ../src/canvas.cpp ../src/mainwindow.cpp ../src/appearance.cpp ../src/tones.cpp ../src/tonedialog.cpp ../src/selection.cpp ../src/blending.cpp ../src/transformdialog.cpp ../src/filters.cpp ../src/effectdialog.cpp ../src/output.cpp ../src/outputdialog.cpp
HEADERS += ../src/document.h ../src/canvas.h ../src/mainwindow.h ../src/appearance.h ../src/tones.h ../src/tonedialog.h ../src/selection.h ../src/blending.h ../src/transformdialog.h ../src/filters.h ../src/effectdialog.h ../src/output.h ../src/outputdialog.h
INCLUDEPATH += ../src
RESOURCES += ../resources.qrc
msvc:QMAKE_CXXFLAGS += /utf-8

SOURCES += ../src/brushpreset.cpp
HEADERS += ../src/brushpreset.h
