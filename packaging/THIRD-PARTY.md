# Third-party software

PhotoDeck's original code and example artwork are MIT licensed. The distributed application dynamically links Qt 6 (LGPLv3, with applicable third-party components), and platform support libraries. The Qt binaries are unmodified. Users may replace the shared Qt libraries and rebuild/relink PhotoDeck from its supplied source. No application-level check prevents replacing those libraries.

Qt source: https://code.qt.io/cgit/qt/ and https://download.qt.io/official_releases/qt/ . CI packages use Qt 6.8.3. The local development build uses the installed Qt version. Qt LGPL/GPL license texts are included in licenses/. Windows runtime files retain Microsoft's own redistribution terms. macOS operating system frameworks are not redistributed. Linux package copyright notices for bundled distribution libraries are copied into licenses/system where the packaging host supplies them.

The source release includes PhotoDeck's CMake/qmake build files. Build and installation instructions are in README.md and EARLY-ACCESS.md. No license in this application changes the terms of a bundled third-party library.
