# qmake 备用构建脚本（CMake 构建失败时可用）
QT       += core gui widgets xml
CONFIG   += c++17
TARGET    = mdk-v5-ose
TEMPLATE  = app

# CI 从这个变量取版本号（.github/workflows/package.yml）
VERSION   = 0.2.0

# Windows 上用 GUI 子系统，别弹黑色控制台窗口
win32: CONFIG += windows

INCLUDEPATH += src

SOURCES += \
    src/main.cpp \
    src/core/Settings.cpp \
    src/core/UvprojModel.cpp \
    src/core/UvprojReader.cpp \
    src/core/Toolchain.cpp \
    src/core/Diagnostic.cpp \
    src/core/BuildEngine.cpp \
    src/core/Formatter.cpp \
    src/core/Lang.cpp \
    src/core/SymbolIndex.cpp \
    src/core/ProjectLoader.cpp \
    src/ui/KeilStyle.cpp \
    src/ui/CSyntaxHighlighter.cpp \
    src/ui/CodeEditor.cpp \
    src/ui/ProjectTree.cpp \
    src/ui/BuildOutput.cpp \
    src/ui/OptionsDialog.cpp \
    src/ui/LoadingOverlay.cpp \
    src/ui/MainWindow.cpp

HEADERS += \
    src/core/Settings.h \
    src/core/UvprojModel.h \
    src/core/UvprojReader.h \
    src/core/Toolchain.h \
    src/core/Diagnostic.h \
    src/core/BuildEngine.h \
    src/core/Formatter.h \
    src/core/Lang.h \
    src/core/SymbolIndex.h \
    src/core/ProjectLoader.h \
    src/core/Timing.h \
    src/ui/KeilStyle.h \
    src/ui/CSyntaxHighlighter.h \
    src/ui/CodeEditor.h \
    src/ui/ProjectTree.h \
    src/ui/BuildOutput.h \
    src/ui/OptionsDialog.h \
    src/ui/LoadingOverlay.h \
    src/ui/MainWindow.h

# MSVC 默认按系统 ANSI 代码页读源文件，源码里有中文，必须显式指定 UTF-8
msvc: QMAKE_CXXFLAGS += /utf-8

# PerMonitorV2 的 DPI 清单（app.rc 里是 1 RT_MANIFEST）
win32: RC_FILE = resources/app.rc

# 关键：qmake 的 MSVC 默认还会 /MANIFEST:embed 再生成一份，
# 和 app.rc 里的 RT_MANIFEST 撞成 CVT1100（duplicate resource）。
# 只保留我们自己的那份。
win32: CONFIG -= embed_manifest_exe embed_manifest_dll
