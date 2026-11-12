# qmake 备用构建脚本（CMake 构建失败时可用）
QT       += core gui widgets xml
CONFIG   += c++17
TARGET    = mdk-v5-ose
TEMPLATE  = app

INCLUDEPATH += src

SOURCES += \
    src/main.cpp \
    src/core/Settings.cpp \
    src/core/UvprojModel.cpp \
    src/core/UvprojReader.cpp \
    src/core/Toolchain.cpp \
    src/core/Diagnostic.cpp \
    src/core/BuildEngine.cpp \
    src/ui/KeilStyle.cpp \
    src/ui/CSyntaxHighlighter.cpp \
    src/ui/CodeEditor.cpp \
    src/ui/ProjectTree.cpp \
    src/ui/BuildOutput.cpp \
    src/ui/OptionsDialog.cpp \
    src/ui/MainWindow.cpp

HEADERS += \
    src/core/Settings.h \
    src/core/UvprojModel.h \
    src/core/UvprojReader.h \
    src/core/Toolchain.h \
    src/core/Diagnostic.h \
    src/core/BuildEngine.h \
    src/ui/KeilStyle.h \
    src/ui/CSyntaxHighlighter.h \
    src/ui/CodeEditor.h \
    src/ui/ProjectTree.h \
    src/ui/BuildOutput.h \
    src/ui/OptionsDialog.h \
    src/ui/MainWindow.h

# MSVC 默认按系统 ANSI 代码页读源文件，源码里有中文，必须显式指定 UTF-8
msvc: QMAKE_CXXFLAGS += /utf-8

win32:RC_ICONS =
