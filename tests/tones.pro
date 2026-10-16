QT += widgets testlib
CONFIG += c++17 testcase release
CONFIG -= debug
TARGET = photodeck-tones-tests
SOURCES += test_tones.cpp ../src/document.cpp ../src/canvas.cpp ../src/mainwindow.cpp ../src/appearance.cpp ../src/tones.cpp ../src/tonedialog.cpp
HEADERS += ../src/document.h ../src/canvas.h ../src/mainwindow.h ../src/appearance.h ../src/tones.h ../src/tonedialog.h
INCLUDEPATH += ../src
RESOURCES += ../resources.qrc
msvc:QMAKE_CXXFLAGS += /utf-8
SOURCES += ../src/selection.cpp
HEADERS += ../src/selection.h
SOURCES += ../src/blending.cpp
HEADERS += ../src/blending.h

SOURCES += ../src/transformdialog.cpp
HEADERS += ../src/transformdialog.h

SOURCES += ../src/filters.cpp
HEADERS += ../src/filters.h

SOURCES += ../src/effectdialog.cpp
HEADERS += ../src/effectdialog.h

QT += printsupport
SOURCES += ../src/output.cpp
HEADERS += ../src/output.h

SOURCES += ../src/outputdialog.cpp
HEADERS += ../src/outputdialog.h

SOURCES += ../src/brushpreset.cpp
HEADERS += ../src/brushpreset.h

CONFIG += thread
SOURCES += ../src/recoverymanager.cpp ../src/recoveryfiles.cpp
HEADERS += ../src/recoverymanager.h ../src/recoveryfiles.h
