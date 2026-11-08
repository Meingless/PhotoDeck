QT += widgets testlib
CONFIG += c++17 testcase release
CONFIG -= debug
TARGET = photodeck-tests
SOURCES += test_photodeck.cpp ../src/document.cpp ../src/canvas.cpp ../src/mainwindow.cpp
HEADERS += ../src/document.h ../src/canvas.h ../src/mainwindow.h
INCLUDEPATH += ../src
RESOURCES += ../resources.qrc

msvc:QMAKE_CXXFLAGS += /utf-8

SOURCES += ../src/appearance.cpp
HEADERS += ../src/appearance.h
SOURCES += ../src/tones.cpp ../src/tonedialog.cpp
HEADERS += ../src/tones.h ../src/tonedialog.h
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
