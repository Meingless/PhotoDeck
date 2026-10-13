QT += widgets testlib
CONFIG += c++17 testcase release
CONFIG -= debug
TARGET = photodeck-feature-audit
SOURCES += test_features.cpp ../src/document.cpp ../src/canvas.cpp ../src/mainwindow.cpp ../src/appearance.cpp
HEADERS += ../src/document.h ../src/canvas.h ../src/mainwindow.h ../src/appearance.h
INCLUDEPATH += ../src
RESOURCES += ../resources.qrc
msvc:QMAKE_CXXFLAGS += /utf-8
SOURCES += ../src/tones.cpp ../src/tonedialog.cpp
HEADERS += ../src/tones.h ../src/tonedialog.h
SOURCES += ../src/selection.cpp
HEADERS += ../src/selection.h
SOURCES += ../src/blending.cpp
HEADERS += ../src/blending.h

SOURCES += ../src/transformdialog.cpp
HEADERS += ../src/transformdialog.h
