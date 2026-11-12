# uVision5-OSE

用 Qt5 写的 Keil uVision5 风格嵌入式 IDE 前端，**Windows 为主**（Linux 也能编译，用于预览界面）。

目标不是复刻 Keil 的编译器，而是：**打开现有的 .uvprojx，用你自己的 ARM Compiler 把工程编译出来，
并且在高分屏上显示清晰**（uVision 的界面缩放一直是硬伤，这个项目优先解决它）。

> 本项目 **不分发** ARM Compiler。你需要自己安装 Keil MDK（或 Arm Compiler 发行包），
> 然后在 Tools -> Configure Toolchain 里指定 armcc.exe / armclang.exe 所在目录。
> 授权和合规由你与 Arm 的许可协议决定。

## 高 DPI（本轮重点）

- **PerMonitorV2 清单**（resources/app.manifest + app.rc）：Windows 10 1703+ 上每块显示器独立缩放，
  把窗口从 4K 屏拖到 1080p 屏会实时重排，而不是模糊拉伸。
- **分数缩放**：入口处设置 HighDpiScaleFactorRoundingPolicy::PassThrough，
  125% / 150% 按真实比例渲染，不会被凑整成 100% 或 200%。
- **多倍图图标**：所有 16x16 图标用 QPainter 按 1x/2x/3x 各画一张塞进同一个 QIcon，
  由 Qt 按当前屏幕的 devicePixelRatio 选最合适的那张，所以放大后依然锐利。
- **编辑器缩放**：View -> Zoom In / Zoom Out / Reset Zoom，或直接 Ctrl+滚轮。
  改的是字号（矢量渲染），不是位图拉伸。

## 构建引擎（本轮重点）

按 uVision 的实际语义实现，而不是"能调用 armcc 就算完"：

| 能力 | 说明 |
|---|---|
| 增量判断 | 目标文件 > 源文件、> 所有被包含的本地头文件、且编译参数指纹未变 |
| 头文件依赖 | 自己扫 #include 的双引号和尖括号形式，递归解析到 -I 目录，不依赖编译器生成 .d |
| 参数指纹 | 存在 Objects/.mdk-ose/<target>.sig；多个 Target 共用 Objects 目录时不会互相误判 |
| 组级选项 | GroupOption 的 IncludeInBuild、Define、IncludePath、MiscControls，优先级低于文件级、高于 Target |
| Create Library | CreateLib=1 时走 armar --create，Target 名为 Lib 的那类配置可用 |
| Scatter 生成 | "Use Memory Layout from Target Dialog" 时按 IROM1/IRAM1 生成 scatter，
执行域叫 ER_IROM1 / RW_IRAM1，于是 Image$$ER_IROM1$$Limit 这类链接器符号存在
（只传 --ro-base 是产生不了命名域的，cm_backtrace 那种库会直接 L6218E） |
| Before/After Make | BeforeCompile / BeforeMake / AfterMake 里的用户命令，
连 UserProg1Dos16Mode、nStopA1X 的语义都保留；PATH 自动带上工具链目录 |
| MicroLIB | useUlib=1 时同时给编译器/汇编器定义 __MICROLIB，并给链接器 --library_type=microlib
（漏了定义会得到 L6218E: __initial_sp / __heap_base / __heap_limit） |
| AC5 选项 | --c99/--c90、--gnu(uGnu)、--apcs=interwork、--split_sections、--strict、
--enum_is_int、--plain_char、--library_type=microlib、--no_stdlib 等 |
| 产物 | .axf / .hex / .bin / .lib，Program Size 从 armlink map 的 Grand Totals 解析 |
| 诊断 | armcc / gcc / armlink 三种格式，Error List 双击跳转，计数不重复 |
| 缺文件 | 源文件不存在时明确报错并继续，而不是抛一个看不懂的链接错误 |
| 并行编译 | 编译/汇编按 -j N 并行，链接、armar、hex、用户命令严格串行 |

## 并行编译

编译和汇编阶段并行执行，默认并行度是 **本机逻辑处理器数**（QThread::idealThreadCount()，
也就是"核数 x 超线程"，8 核 16 线程的机器就是 16 路），没有写死任何数值。

三种改法：Tools -> Parallel Build 菜单、命令行 -j N、ini 里的 build/jobs。

执行顺序是分阶段的，不是把整条队列丢进线程池：

    [BeforeCompile 用户命令]  串行
    [编译 / 汇编]             并行 -j N
    [BeforeMake 用户命令]     串行
    [链接 或 armar]           串行
    [fromelf 生成 hex]        串行
    [AfterMake 用户命令]      串行

串行任务必须等并行批次全部收尾才开始，所以链接一定拿到完整的 .o 集合。

输出在 -j>1 时按文件成块：一个源文件的 "compiling x.c..." 和它的告警/错误一起吐出，
不会出现 A 的标题下面跟着 B 的告警。状态栏显示 compiling 12/80 的进度。

实测（6 个各睡 0.5s 的假编译器）：-j1 3.6s，-j6 1.0s。

## 其它已实现

- 解析 .uvprojx（多 Target / Group / File / C-C++ / Linker / 钩子），写回时原地改 DOM
- 工程树、Target 切换、双击打开源文件、语法高亮、行号、当前行高亮
- Options for Target：Output / C/C++ / Linker 三页可回写工程
- Build Output 按类别着色：标题蓝、编译灰、Warning 琥珀、Error 红加粗、Program Size 绿、
  用户命令紫、汇总行成功绿/失败红
- 现代浅色主题（Fusion + QSS）：页签强调线、工具栏 hover、圆角输入框、细滚动条，
  Windows 下用 Segoe UI；编辑器用 Consolas 10.5pt

## 编辑器

- **Ctrl+左键跳转到定义**：扫工程里的 .c/.h 建一个极简符号表（函数定义/声明、宏名），
  定义优先于声明；点 include 行里的头文件名直接打开该文件
- **Ctrl+S 自动格式化**：优先调用 clang-format（和 VS Code C/C++ 插件同一个引擎，
  自动探测 PATH / LLVM / VS Code cpptools 自带的那份）；
  找不到就退回内置缩进器——它只改行首空白，绝不会改坏语义
- Shift+Alt+F 手动格式化，Tools -> Format on Save 可关掉边存边格式化
- 工程里有 .clang-format 就按你的配置来，没有就用内置兜底风格（4 空格、Allman、不限列宽）

语法配色对齐 VS Code Light+：控制关键字紫、声明关键字蓝、类型青、函数名棕、
全大写宏紫、数字绿、字符串暗红、注释绿斜体。
- Build Output / Error List：Ctrl+A 全选、Ctrl+C 复制选中、Ctrl+Shift+C 复制全部，
  右键菜单还有 Clear / Save Output As；复制的是编译器原始行，可以直接贴回 Keil 或邮件
- 命令行：--build <uvprojx> [--toolchain <dir>] [--target <index>] [--rebuild] [-j N]，可进 CI
- --screenshot <png>：离屏渲染一张界面图，便于验证

## 未实现

- 烧写（OpenOCD / pyOCD）与调试会话（GDB + DAP），菜单里是占位并会明确提示
- Pack Manager（按需求不做）
- 工程树里增删文件、文件级选项对话框
- 并行编译

## 构建

前置：Qt 5.12+（Widgets + Xml），CMake 3.16+ 或 qmake，Windows 上 MSVC 2017/2019 或 MinGW。

### CMake（推荐）

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/5.15.2/msvc2019_64
    cmake --build build --config Release

MinGW：

    cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH=C:/Qt/5.15.2/mingw81_64
    cmake --build build

产物在 build/Release/mdk-v5-ose.exe（MinGW 在 build/mdk-v5-ose.exe）。

发布前带上 Qt 运行库：

    C:/Qt/5.15.2/msvc2019_64/bin/windeployqt.exe build/Release/mdk-v5-ose.exe

### qmake（备用）

    C:/Qt/5.15.2/msvc2019_64/bin/qmake.exe mdk-v5-ose.pro
    nmake

两条路都会编译 resources/app.rc，把 PerMonitorV2 的清单嵌进 exe。

### Linux 上编译起来看界面

Linux 需要 mesa 的 GL 开发头文件（libgl-dev / mesa-common-dev），否则 Qt5 的
CMake 配置阶段会直接报找不到 GL/gl.h。没有的话用 qmake 那条路。

    cmake -S . -B build -DCMAKE_PREFIX_PATH=/home/jarue/Qt5/5.15.2/gcc_64
    cmake --build build -j
    ./build/mdk-v5-ose

## 第一次运行

1. Tools -> Configure Toolchain，填 C:/Keil_v5/ARM/ARMCC/bin（AC5）或 C:/Keil_v5/ARM/ARMCLANG/bin（AC6）。
2. File -> Open Project 打开 .uvprojx。
3. 左侧 Project 下拉框选 Target，F7 编译。输出在底部 Build Output / Error List。

## 已知限制

- 跳转定义是正则扫出来的符号表，不是 clangd：宏拼接、函数指针、同名静态函数可能跳错，
  重载和结构体成员跳不了。
- 头文件依赖是"扫 #include"而不是编译器依赖，宏拼出来的头文件名可能漏掉。
- 对象文件名用 源文件基名.o，重名时加 _2；和 Keil 的命名规则不完全一致。
- 响应文件（--via）还没做，源文件特别多的工程可能撞 Windows 命令行长度上限。
- 调试和烧写是空实现。

## 目录

    src/core/    工程模型、uvprojx 读写、工具链探测、构建引擎、诊断解析
    src/ui/      主窗口、工程树、编辑器、输出窗口、选项对话框、图标绘制
    resources/   Windows 清单与资源脚本（高 DPI 声明）
