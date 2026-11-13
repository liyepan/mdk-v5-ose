# uVision5-OSE

用 **.NET 10 + Avalonia 12 + AvaloniaEdit** 写的 uVision5（Keil MDK）风格嵌入式 IDE 前端。
跨平台，但主要面向 Windows。

> **0.3.0 起换掉了 Qt 实现。** 之前那一版是 Qt5 的（`0.2.x`），因为高 DPI 缩放做不好、
> 外观也跟不上，整个重写了。Qt 版的代码还在 git 历史里（`0.2.0` 之前的提交）。
>
> 没选 WinUI 3 的原因：WinUI 3 只能在 Windows 上构建，而开发机是 Linux，换过去就没法
> 编译、没法截图验证了。Avalonia 是 XAML + Fluent 风格，Windows 上跑原生，Linux 上也能
> 开发调试。编辑器直接有 AvaloniaEdit 可用（WinUI 3 没有像样的代码编辑器控件）。

## 技术栈

| 部分 | 选型 |
|---|---|
| 运行时 | .NET 10（`net10.0`） |
| UI | Avalonia 12.1.6（Fluent 主题 + 自定义浅色样式） |
| 编辑器 | AvaloniaEdit 12.0.0（AvalonEdit 的 Avalonia 移植） |
| 工程文件 | `.uvprojx`，`System.Xml.Linq` 直接解析 |

ARM 编译器还是**自带**：本程序不分发 armcc，你自己装 Keil MDK，程序里选编译器路径。

## 现在能做什么

**工程**
- 打开 `.uvprojx`：解析 Target / Cads / Aads / LDads / 片上存储 / Groups-Files / 用户命令
- 左侧工程树：Target → Group → File，点文件在右边打开标签页
- 多 Target 切换（工具栏下拉框）
- 最近打开的工程、上次工程自动打开、窗口大小记忆
- 打开工程走**后台线程 + 加载遮罩**（背景模糊、圆角卡片、蓝色循环进度条）

**编译**
- 工具 → 配置编译器：选 `armcc.exe` / `armclang.exe` 所在目录，自动识别 AC5 / AC6
- F7 编译、重新编译、清理、停止
- 并行编译（默认按逻辑处理器数），串行阶段（链接 / hex / 用户命令）等并行批次跑完再上
- 增量编译：源文件 mtime + 自己扫 `#include` 的本地头文件 + 编译参数指纹
  （存在 `<Objects>/.mdk-ose/<target>.sig`），切 Target 或改选项会自动重编
- 按 IROM1/IRAM1 生成 scatter 文件（`Image$$ER_IROM1$$Limit` 这类符号要用）
- `--microlib` 时自动定义 `__MICROLIB`（不然启动文件那组堆栈符号会 L6218E）
- 输出按 uVision 的格式回放：`Build started` / `*** Using Compiler` / `compiling x.c...` /
  `Program Size: Code=... RO-data=...` / `"xxx.axf" - 0 Error(s), 1 Warning(s).`
- Error List 里双击跳到出错行

**编辑**
- AvaloniaEdit：行号、等宽字体、C 语法高亮（`src/Assets/C.xshd`，VS Code Light+ 配色）
- 保存（Ctrl+S 还没接，现在是菜单/工具栏）

## 命令行构建（CI / 无界面）

    uvision5-ose --build path/to/project.uvprojx [--toolchain <bin目录>] [--target N] [--rebuild] [-j N]

输出和 uVision 一样，退出码 0 表示成功。没配 `--toolchain` 时用配置里存的那个。

## 还没做

- 符号索引 + Ctrl+点击跳转
- Target 选项对话框、文件级选项编辑（现在是只读解析）
- 格式化（clang-format 集成）、查找替换 / 全局搜索、书签、编码探测（GBK）
- 烧写（OpenOCD / pyOCD）与调试

## 构建与运行

需要 .NET 10 SDK。

    dotnet build
    dotnet run -- path/to/project.uvprojx

Linux 上跑图形界面需要 X / Wayland；Windows 上 `dotnet build -c Release` 直接出 exe。

## 没有显示器也能验证界面

    dotnet run -- --screenshot out.png --shot-delay 800 --shot-open path/to/project.uvprojx

headless 模式（Avalonia.Headless + Skia）把窗口真渲染一遍存成 PNG。另外两个开关：

- `--shot-map`：输出字符画版的面板布局（取每格里最暗的像素，能看出控件边界和文字分布）
- `--shot-info`：打印工程树结构、标签页数、状态栏文字、工具链状态等

## 打包与 Release（GitHub Actions）

`.github/workflows/package.yml`：

| Job | 产物 |
|---|---|
| windows-x86_64 | `uVision5-OSE-<版本>-win64-setup.exe`（NSIS）+ `-portable.zip` |
| release | 汇总产物 + `SHA256SUMS`，挂到 `v<版本>` 的 Release |

- 版本号在 `src/UvisionOse.csproj` 的 `<Version>`，改版本只改这一处
- 发布的是 win-x64 **自包含**包，用户不用另装 .NET 运行时
- Gitea 也会读 `.github/workflows`，每个 job 都带 `if: github.server_url == 'https://github.com'`
  守卫，在 Gitea 上只显示 skipped；**工作流只读写 GitHub，不碰 Gitea**

安装包会装到 `%ProgramFiles%\uVision5-OSE`，建开始菜单/桌面快捷方式，给 `.uvprojx`
登记 **OpenWithProgids**（右键"打开方式"里有它，但**不抢** Keil 的默认关联）。

## 目录

    src/Core/       工程模型、uvprojx 解析、工具链探测、构建引擎、诊断解析、配置
    src/Themes/     浅色样式（菜单 / 工具栏 / 状态栏 / 标签页 / 树）
    src/Assets/     C 语法高亮定义
    src/MainWindow  主窗口
    src/Cli.cs      命令行构建
    packaging/      NSIS 安装包脚本
