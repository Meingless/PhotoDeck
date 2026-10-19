QT += widgets
CONFIG += c++17 release thread
CONFIG -= debug
TEMPLATE = app
TARGET = photodeck
DESTDIR = bin
SOURCES += src/main.cpp src/document.cpp src/canvas.cpp src/mainwindow.cpp
HEADERS += src/document.h src/canvas.h src/transformhandles.h src/mainwindow.h src/version.h
!msvc:QMAKE_CXXFLAGS += -Wall -Wextra
msvc:QMAKE_CXXFLAGS += /utf-8
unix:!macx:target.path = /usr/local/bin
unix:!macx:INSTALLS += target
macx:QMAKE_INFO_PLIST = packaging/Info.plist
macx:ICON = packaging/photodeck.icns
win32:RC_ICONS = packaging/photodeck.ico
HEADERS += src/applicationicon.h
RESOURCES += resources.qrc

SOURCES += src/appearance.cpp
HEADERS += src/appearance.h
SOURCES += src/tones.cpp src/tonedialog.cpp
HEADERS += src/tones.h src/tonedialog.h
SOURCES += src/selection.cpp
HEADERS += src/selection.h
SOURCES += src/blending.cpp
HEADERS += src/blending.h

SOURCES += src/transformdialog.cpp
HEADERS += src/transformdialog.h

SOURCES += src/filters.cpp
HEADERS += src/filters.h

SOURCES += src/effectdialog.cpp
HEADERS += src/effectdialog.h

QT += printsupport
SOURCES += src/output.cpp
HEADERS += src/output.h

SOURCES += src/outputdialog.cpp
HEADERS += src/outputdialog.h

SOURCES += src/brushpreset.cpp
HEADERS += src/brushpreset.h

SOURCES += src/recoverymanager.cpp src/recoveryfiles.cpp
HEADERS += src/recoverymanager.h src/recoveryfiles.h
