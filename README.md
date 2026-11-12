# uVision5-OSE

用 Qt5 写的 Keil uVision5 风格嵌入式 IDE 前端，**Windows 为主**（Linux 也能编译，用于预览界面）。

目标：打开现有的 .uvprojx 工程，用你自己的 ARM Compiler 编译、链接、出 HEX，
再逐步补齐工程管理、烧写、调试。

> 本项目 **不分发** ARM Compiler。你需要自己安装 Keil MDK（或 Arm Compiler 发行包），
> 然后在 Tools -> Configure Toolchain 里指定 armcc.exe / armclang.exe 所在目录。
> 授权和合规由你与 Arm 的许可协议决定。

## 当前实现状态（第 1 阶段）

| 功能 | 状态 |
|---|---|
| 解析 .uvprojx（Target / Group / File / 编译选项） | 完成 |
| 工程树、Target 切换、双击打开源文件 | 完成 |
| C/C++/汇编语法高亮、行号、当前行高亮 | 完成 |
| 调用 armcc / armasm / armlink / fromelf 构建 | 完成 |
| 增量编译（时间戳）、Rebuild all、Clean | 完成 |
| Program Size、Error List、双击错误跳转 | 完成 |
| Options for Target（Output / C/C++ / Linker）写回 uvprojx | 完成 |
| 新建空工程、最近工程 | 完成 |
| Pack Manager | 按需求 **不做** |
| 烧写（OpenOCD / pyOCD） | 未实现 |
| 调试（GDB + DAP） | 未实现 |

## 构建

前置：Qt 5.12+（Widgets + Xml），CMake 3.16+ 或 qmake，Windows 上 MSVC 2017/2019 或 MinGW。

### CMake（推荐）

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
          -DCMAKE_PREFIX_PATH=C:/Qt/5.15.2/msvc2019_64
    cmake --build build --config Release

MinGW：

    cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH=C:/Qt/5.15.2/mingw81_64
    cmake --build build

产物在 build/Release/mdk-v5-ose.exe（MinGW 在 build/mdk-v5-ose.exe）。

发布前带上 Qt 运行库：

    C:\Qt\5.15.2\msvc2019_64\bin\windeployqt.exe build\Release\mdk-v5-ose.exe

### qmake（备用）

    C:\Qt\5.15.2\msvc2019_64\bin\qmake.exe mdk-v5-ose.pro
    nmake

### Linux 上编译起来看界面

Linux 需要 mesa 的 GL 开发头文件（libgl-dev / mesa-common-dev），否则 Qt5 的
CMake 配置阶段会直接报找不到 GL/gl.h。没有的话用下面的 qmake 路子。

    cmake -S . -B build -DCMAKE_PREFIX_PATH=/home/jarue/Qt5/5.15.2/gcc_64
    cmake --build build -j
    ./build/mdk-v5-ose

## 第一次运行

1. Tools -> Configure Toolchain，填 C:/Keil_v5/ARM/ARMCC/bin（AC5）或 C:/Keil_v5/ARM/ARMCLANG/bin（AC6）。
2. File -> Open Project 打开一个 .uvprojx。
3. F7 编译，输出在底部 Build Output / Error List。

## 已知限制

- 增量判断只看源文件时间戳，**不跟踪头文件依赖**。改了 .h 请用 Rebuild。
- 文件类别以后缀为准，FileType 只作参考。
- 链接对象太多超过 Windows 命令行长度时还没做 --via 回退。
- 烧写和调试是空实现，菜单会明确提示，而不是假装成功。

## 目录

    src/core/   工程模型、uvprojx 读写、工具链探测、构建引擎、诊断解析
    src/ui/     主窗口、工程树、编辑器、输出窗口、选项对话框、图标绘制
