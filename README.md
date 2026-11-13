# uVision5-OSE

用 Qt5 写的 Keil uVision5 风格嵌入式 IDE 前端，**Windows 为主**（Linux 也能编译，用于预览界面）。

目标不是复刻 Keil 的编译器，而是：**打开现有的 .uvprojx，用你自己的 ARM Compiler 把工程编译出来，
并且在高分屏上显示清晰**（uVision 的界面缩放一直是硬伤，这个项目优先解决它）。

> 本项目 **不分发** ARM Compiler。你需要自己安装 Keil MDK（或 Arm Compiler 发行包），
> 然后在 Tools -> Configure Toolchain 里指定 armcc.exe / armclang.exe 所在目录。
> 授权和合规由你与 Arm 的许可协议决定。

## 图标

- `resources/app.ico`：exe 自己的图标（`resources/app.rc` 里 `IDI_ICON1 ICON`），
  资源管理器 / 任务栏 / 快捷方式都取它；安装包图标也用它（工作流传 `/DICON_FILE`）
- `resources/icons/app.png`：窗口图标，编译进 qrc（`:/icons/app.png`），
  `main.cpp` 里 `QApplication::setWindowIcon`
- `packaging/logo.png`：源图（1254x1254）。改图之后重新生成 .ico / .png 即可，
  图标里 16/24/32/48/64/128/256 七种尺寸都在，白底已经抠成透明

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
- Build Output 只保留一行 Program Size（不再把 armlink 的按 .o 尺寸表打到控制台），
  .map 明细放在独立的 Memory Map 标签页，链接成功后自动载入
- Build Output 按类别着色：标题蓝、编译灰、Warning 琥珀、Error 红加粗、Program Size 绿、
  用户命令紫、汇总行成功绿/失败红
- 现代浅色主题（Fusion + QSS）：页签强调线、工具栏 hover、圆角输入框、细滚动条，
  Windows 下用 Segoe UI；编辑器用 Consolas 10.5pt

- **界面语言**：Window -> Language 切 English / 简体中文，切换即时生效（菜单和工具栏整体重建），
  选择会记住；首次运行跟随系统语言
- **文件编码**：默认 UTF-8。编辑器里右键 -> Encoding 可切 UTF-8 / GBK / GB18030 / Big5 / 系统默认，
  切完自动按新编码重新载入；工程树里右键文件也能「按指定编码打开」；状态栏显示当前文件编码

## 写回 .uvprojx 时保证不破坏 Keil 的构建

改 Target 选项（Options for Target）和工程管理器都会写回 `.uvprojx`，
做法是**就地改 DOM 再写回**，不是"按模型重新生成一个文件"：

- 只改到我们认识的那些叶子节点，其余内容（`CommonProperty` / `DllOption` /
  `DebugOption` / `Utilities` / `ArmAdsMisc` 里的大段配置）原样不动
- `<Groups>` 重建时，已有的分组和文件会把 `GroupOption` / `FileOption`
  整段克隆过去，你在 Keil 里配的组级/文件级宏和 include 路径不会丢
- 原来的 XML 声明、编码、BOM、CRLF 换行、`<X></X>` 的写法都保留
- 写盘用 `QSaveFile`（先写临时文件再原子替换），写一半崩了也不会留下半截工程
- 文件解析失败 / 写不进去就报错返回，**一个字节都不动**
- 工程管理器里什么都没改的话直接返回，压根不写文件

唯一的可见差异：QDom 序列化不保留纯空行，重写后会少几行空白行
（XML 语义无关），其余差异就只有你实际改的那几个值。

工具栏最右边的"工程文件管理"按钮，或者 Project -> Manage Project Items（Alt+M）。
布局和 uVision 一样，三列并排：

```
[Project Items][Folders/Extensions][Books][Project Info/Layer]
Project Targets:      Groups:              Files:
 + × ↑ ↓              + × ↑ ↓              × ↑ ↓
 Debug                 apl                  main.c
 Release               drivers              sysPara.c
                       Shell
[Set as Current Target]                      [Add Files...]
                                     [OK] [Cancel] [Help]
```

- 左列选 Target，中列显示它的分组，右列显示分组里的文件
- **+** 新建分组 / **×** 删除 / **↑ ↓** 调顺序（分组、文件都支持，双击分组名可改名）
- Target 那一列同样能复制新建、删除、调顺序；新建是**整段复制源 Target**
- **Set as Current Target** 切换活动配置，**Add Files...** 往当前分组加文件（多选）
- 点 OK 之后：Target 布局变了就重写 `<Targets>`（仅节点搬移/复制），
  分组变了就重写 `<Groups>`；两处都只动对应的节点，其余配置原样保留

## 编辑器

- **Ctrl+左键跳转到定义**：扫工程里的 .c/.h 建一个极简符号表
  （函数定义/声明、宏名，以及 `typedef struct {...} X;` / `struct X {` 这类结构体、枚举名），
  定义优先于声明；点 include 行里的头文件名直接打开该文件
- **Ctrl+悬停下划线**：凡是能 Ctrl+点击的东西（符号、include 里的头文件名），
  按住 Ctrl 把鼠标放上去就画一条蓝色下划线；只按 Ctrl 不动鼠标也会立刻出现
- **右键跳转菜单**：在标识符上右键，函数会给「跳转实现」（.c 里的函数体）和
  「跳转定义」（.h 里的声明）两项；结构体 / 枚举 / 宏这类只在头文件里的，只给「跳转定义」
- **格式化只用 clang-format**（和 VS Code C/C++ 插件同一个引擎）
- **风格可选**：Tools -> Configure Toolchain -> `格式化风格`，有
  自动 / 内置（4 空格 + Allman）/ LLVM / Google / Chromium / Mozilla / WebKit / Microsoft / GNU。
  选"自动"时工程里有 `.clang-format` 就按它，没有才用内置风格
- 格式化后光标按"第几行第几列"回到原位（不会因为行首多出缩进就跑到词中间）
- **路径在这里配**：Tools -> Configure Toolchain 里的 `clang-format:` 一行，
  可以直接填路径、"浏览…"、"自动探测"。留空就自动找 PATH / LLVM / VS Code cpptools 自带的那份
- 找不到 clang-format 时会明确报错并问你要不要现在去配置，**不会**偷偷用别的引擎顶上
  （两套引擎风格不一致，会把工程改花）
- Shift+Alt+F 手动格式化；Ctrl+S 和 **Save All** 都会先格式化再存盘
  （Tools -> Format on Save 可关；关窗口时的"全部保存"同样会格式化）

语法配色对齐 VS Code Light+：控制关键字紫、声明关键字蓝、类型青、函数名棕、
全大写宏紫、数字绿、字符串暗红、注释绿斜体。
- Build Output / Error List：Ctrl+A 全选、Ctrl+C 复制选中、Ctrl+Shift+C 复制全部，
  右键菜单还有 Clear / Save Output As；复制的是编译器原始行，可以直接贴回 Keil 或邮件
- 命令行：--build <uvprojx> [--toolchain <dir>] [--target <index>] [--rebuild] [-j N]，可进 CI
- --screenshot <png>：离屏渲染一张界面图，便于验证

## 性能与加载

打开工程走的是**后台线程 + 遮罩**：主窗口立刻盖上一层“加载中”（背景是整窗的模糊快照，
中间一块圆角卡片，蓝色进度条跟着扫描进度走/循环跑），读 XML、扫源文件全在工作线程里做，
主线程只负责最后铺工程树和填符号下拉框，铺完遮罩才收起来。所以工程再大也不会出现
“点不动、白屏、假死”那种感觉，遮罩一消失就能直接点编译。

4000 个文件 / 19 MB 的工程实测（Linux，Release）：打开动作本身 **12 ms** 就返回了，
工作线程扫完要 **127 ms**，最后铺工程树 + 填下拉框 **13 ms**，这 13 ms 也是盖在遮罩底下做的。
也就是说从点到打开工程到界面完全可用，主线程从头到尾没有一次超过 15 ms 的停顿。

符号索引这块做了四件事：

- 手写逐行扫描，不再每行跑两条 QRegularExpression —— 单次扫描 81 ms → 4 ms；
  11 MB 的 .h 语料（489 个文件、26.6 k 个符号）上 206 ms → 11 ms
- 每个文件只读一次（原来 indexFile / indexIncludes 各读一遍），#include 递归扩散
- 解析结果按 路径 + mtime + 大小 存在进程里，切 target 不重扫
- 符号名排序放到工作线程里做，下拉框只做一次模型替换（原来是一个个 addItem）

改完在 621 个真实头文件上和旧实现逐行比对过，符号名 / 行号 / 是否定义完全一致。

想看时间花在哪：设环境变量 `MDK_OSE_TIMING=1`，启动和打开工程各阶段的耗时会打到 stderr；
`--screenshot <png> [--screenshot-delay ms]` 可以抓任意时刻的界面（比如加载遮罩）。

## 未实现

- 烧写（OpenOCD / pyOCD）与调试会话（GDB + DAP），菜单里是占位并会明确提示
- Pack Manager（按需求不做）
- 工程树里增删文件、文件级选项对话框

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

## 打包与 Release（GitHub Actions）

`.github/workflows/package.yml` 负责出 Windows 安装包并自动发 Release：

| Job | 产物 |
|---|---|
| windows-x86_64 | `uVision5-OSE-<版本>-win64-setup.exe`（NSIS）+ `-portable.zip` |
| release | 汇总产物 + `SHA256SUMS`，挂到 `v<版本>` 的 Release |

版本号从 `mdk-v5-ose.pro` 的 `VERSION = x.y.z` 读，改版本只改这一处。

只出 win64（x86_64）包：Qt 5.15 官方没有 Windows on ARM 的二进制包，交叉编译不现实，所以没做 arm64。

### 安装包做了什么

- 装到 `%ProgramFiles%\uVision5-OSE`，开始菜单 + 桌面快捷方式 + 控制面板卸载项
- 给 `.uvprojx` 登记 **OpenWithProgids**（右键“打开方式”里出现），**不抢** Keil 的默认关联
- 脚本在 `packaging/windows/installer.nsi`，本地可手动打：

      makensis /DAPP_VERSION=0.2.0 /DDEPLOY_DIR=deploy packaging/windows/installer.nsi

### 在 Gitea 上

Gitea 也会读 `.github/workflows`，每个 job 都带
`if: github.server_url == 'https://github.com'` 守卫，在 Gitea 上只会显示 skipped，不占 runner。
**工作流只读写 GitHub**：不往 Gitea 推分支、不推 tag、不碰 Release。

### Release 为什么会被“删掉”

Gitea 的 push mirror 是 `--mirror` 强推，隐含 `--prune`：**GitHub 上多出来、Gitea 没有的
ref 会被删掉**。Release 挂在 tag 上，tag 被 prune 掉，Release 就跟着没 —— 所以现象是

> 本地提交、push 到 Gitea，镜像同步之后 GitHub 上的 Release 消失

这不是工作流删的（里面没有任何删除步骤），是镜像同步干的。两种解法：

1. **仓库这边能做的（也是现在的做法）**：release job 里加了一步
   `确保 tag 存在于 GitHub`，构建时发现 tag 不在就在 GitHub 上补一个，
   下一次 push 触发构建就自愈。代价是镜像同步之后、下一次构建之前，Release 会短暂消失。
2. **治本（Gitea 侧，属于你自己的配置，我没动）**：把镜像的 prune 关掉，
   或者别用 `--mirror` 这种强推，GitHub 上多出来的 tag 就不会被删。

## 目录

    src/core/    工程模型、uvprojx 读写、工具链探测、构建引擎、诊断解析
    src/ui/      主窗口、工程树、编辑器、输出窗口、选项对话框、图标绘制
    resources/   Windows 清单与资源脚本（高 DPI 声明）
