cmake_minimum_required(VERSION 3.16)
project(mdk_v5_ose VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# 资源编译器要早于第一个 target 启用，否则某些生成器会抱怨
if(WIN32)
    enable_language(RC)
endif()

find_package(Qt5 5.12 REQUIRED COMPONENTS Core Gui Widgets Xml)

set(APP_SOURCES
    src/main.cpp
    src/core/Settings.cpp
    src/core/UvprojModel.cpp
    src/core/UvprojReader.cpp
    src/core/Toolchain.cpp
    src/core/Diagnostic.cpp
    src/core/BuildEngine.cpp
    src/core/Formatter.cpp
    src/core/Lang.cpp
    src/core/SymbolIndex.cpp
    src/ui/KeilStyle.cpp
    src/ui/CSyntaxHighlighter.cpp
    src/ui/CodeEditor.cpp
    src/ui/ProjectTree.cpp
    src/ui/BuildOutput.cpp
    src/ui/OptionsDialog.cpp
    src/ui/MainWindow.cpp
)

set(APP_HEADERS
    src/core/Settings.h
    src/core/UvprojModel.h
    src/core/UvprojReader.h
    src/core/Toolchain.h
    src/core/Diagnostic.h
    src/core/BuildEngine.h
    src/core/Formatter.h
    src/core/Lang.h
    src/core/SymbolIndex.h
    src/ui/KeilStyle.h
    src/ui/CSyntaxHighlighter.h
    src/ui/CodeEditor.h
    src/ui/ProjectTree.h
    src/ui/BuildOutput.h
    src/ui/OptionsDialog.h
    src/ui/MainWindow.h
)

if(WIN32)
    set(APP_TYPE WIN32)
endif()

add_executable(mdk-v5-ose ${APP_TYPE} ${APP_SOURCES} ${APP_HEADERS})

target_include_directories(mdk-v5-ose PRIVATE src)

target_link_libraries(mdk-v5-ose PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Xml
)

if(MSVC)
    target_compile_options(mdk-v5-ose PRIVATE /utf-8 /W3)
endif()

if(WIN32)
    # 让 exe 使用 Windows 子系统，避免弹出控制台窗口
    set_target_properties(mdk-v5-ose PROPERTIES WIN32_EXECUTABLE TRUE)

    # 资源文件里带 PerMonitorV2 的 DPI 清单
    target_sources(mdk-v5-ose PRIVATE resources/app.rc)
endif()
