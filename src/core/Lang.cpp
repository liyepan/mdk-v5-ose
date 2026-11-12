#include "core/Lang.h"

#include <QHash>

namespace {

Lang::Id g_current = Lang::Id::English;

const QHash<QString, QString>& dictionary()
{
    static const QHash<QString, QString> dict = []() {
        QHash<QString, QString> d;
        auto add = [&d](const char* en, const QString& zh) {
            d.insert(QString::fromUtf8(en), zh);
        };

        // ---- 文件菜单 ----
        add("&File", QStringLiteral("文件(&F)"));
        add("New File", QStringLiteral("新建文件"));
        add("Open File", QStringLiteral("打开文件"));
        add("Save As...", QStringLiteral("另存为..."));
        add("Save As", QStringLiteral("另存为"));
        add("Batch Build", QStringLiteral("批量编译"));
        add("Insert/Remove Breakpoint", QStringLiteral("插入/移除断点"));
        add("Enable/Disable Breakpoint", QStringLiteral("启用/禁用断点"));
        add("New Project...", QStringLiteral("新建工程..."));
        add("Open Project...", QStringLiteral("打开工程..."));
        add("Recent Projects", QStringLiteral("最近工程"));
        add("Close Project", QStringLiteral("关闭工程"));
        add("Exit", QStringLiteral("退出"));
        add("Save", QStringLiteral("保存"));
        add("Save All", QStringLiteral("全部保存"));

        // ---- 编辑 ----
        add("&Edit", QStringLiteral("编辑(&E)"));
        add("Undo", QStringLiteral("撤销"));
        add("Redo", QStringLiteral("重做"));
        add("Cut", QStringLiteral("剪切"));
        add("Copy", QStringLiteral("复制"));
        add("Paste", QStringLiteral("粘贴"));
        add("Find...", QStringLiteral("查找..."));
        add("Go to Line...", QStringLiteral("跳转到行..."));
        add("Go to Line", QStringLiteral("跳转到行"));
        add("Line:", QStringLiteral("行号："));

        // ---- 视图 ----
        add("&View", QStringLiteral("视图(&V)"));
        add("Project Window", QStringLiteral("工程窗口"));
        add("Build Output Window", QStringLiteral("编译输出窗口"));
        add("Error List Window", QStringLiteral("错误列表窗口"));
        add("Zoom In", QStringLiteral("放大"));
        add("Zoom Out", QStringLiteral("缩小"));
        add("Reset Zoom", QStringLiteral("恢复默认缩放"));
        add("Functions Window", QStringLiteral("函数窗口"));
        add("Source Browser", QStringLiteral("源码浏览器"));
        add("Books Window", QStringLiteral("文档窗口"));

        // ---- 工程 ----
        add("&Project", QStringLiteral("工程(&P)"));
        add("Options for Target...", QStringLiteral("目标选项..."));
        add("Build Target", QStringLiteral("编译目标"));
        add("Build target files (F7)", QStringLiteral("编译目标文件 (F7)"));
        add("Rebuild all target files", QStringLiteral("重新编译所有目标文件"));
        add("Translate", QStringLiteral("编译当前文件"));
        add("Translate current file (Ctrl+F7)", QStringLiteral("编译当前文件 (Ctrl+F7)"));
        add("Clean targets", QStringLiteral("清理目标"));
        add("Stop build", QStringLiteral("停止编译"));

        // ---- 烧写 ----
        add("&Flash", QStringLiteral("烧写(&L)"));
        add("Download to Flash", QStringLiteral("下载到 Flash"));
        add("Erase", QStringLiteral("擦除"));
        add("Configure Toolchain...", QStringLiteral("配置编译器..."));

        // ---- 调试 ----
        add("&Debug", QStringLiteral("调试(&D)"));
        add("Start/Stop Debug Session", QStringLiteral("启动/停止调试"));
        add("Run", QStringLiteral("运行"));
        add("Stop", QStringLiteral("停止"));
        add("Step", QStringLiteral("单步进入"));
        add("Step Over", QStringLiteral("单步跳过"));
        add("Step Out", QStringLiteral("跳出函数"));
        add("Run to Cursor Line", QStringLiteral("运行到光标行"));
        add("Breakpoints...", QStringLiteral("断点..."));
        add("Watch Windows", QStringLiteral("观察窗口"));
        add("Call Stack Window", QStringLiteral("调用栈窗口"));
        add("Register Window", QStringLiteral("寄存器窗口"));
        add("Memory Window", QStringLiteral("内存窗口"));

        // ---- 外设 / 工具 / 窗口 / 帮助 ----
        add("&Peripherals", QStringLiteral("外设(&P)"));
        add("System Viewer", QStringLiteral("系统查看器"));
        add("&Tools", QStringLiteral("工具(&T)"));
        add("Parallel Build", QStringLiteral("并行编译"));
        add("Auto  (%1 logical threads)", QStringLiteral("自动（%1 逻辑线程）"));
        add("Format Document", QStringLiteral("格式化文档"));
        add("Format on Save", QStringLiteral("保存时格式化"));
        add("Customize Tools Menu...", QStringLiteral("自定义工具菜单..."));
        add("&Window", QStringLiteral("窗口(&W)"));
        add("Close All", QStringLiteral("全部关闭"));
        add("Reset Window Layout", QStringLiteral("重置窗口布局"));
        add("&Help", QStringLiteral("帮助(&H)"));
        add("About uVision5-OSE", QStringLiteral("关于 uVision5-OSE"));
        add("Language", QStringLiteral("语言"));

        // ---- 停靠窗口 / 状态栏 ----
        add("Project", QStringLiteral("工程"));
        add("Build Output", QStringLiteral("编译输出"));
        add("Error List", QStringLiteral("错误列表"));
        add("Memory Map", QStringLiteral("内存映射"));
        add("Ready", QStringLiteral("就绪"));
        add("No Project", QStringLiteral("无工程"));
        add("Compiler: %1", QStringLiteral("编译器: %1"));
        add("Compiler: not configured", QStringLiteral("编译器: 未配置"));
        add("%1  |  Jobs: %2", QStringLiteral("%1  |  并行: %2"));
        add("Encoding: %1", QStringLiteral("编码: %1"));
        add("Ln 1, Col 1", QStringLiteral("行 1, 列 1"));
        add("Ln %1, Col %2", QStringLiteral("行 %1, 列 %2"));

        // ---- 对话框 ----
        add("New uVision Project", QStringLiteral("新建 uVision 工程"));
        add("New Project", QStringLiteral("新建工程"));
        add("Open uVision Project", QStringLiteral("打开 uVision 工程"));
        add("Open Project", QStringLiteral("打开工程"));
        add("Project '%1' loaded.", QStringLiteral("已加载工程 '%1'。"));
        add("Open File", QStringLiteral("打开文件"));
        add("Save Changes", QStringLiteral("保存修改"));
        add("%1 has been modified. Save changes?", QStringLiteral("%1 已被修改，是否保存？"));
        add("(empty)", QStringLiteral("（空）"));
        add("Options for Target", QStringLiteral("目标选项"));
        add("Options for File", QStringLiteral("文件选项"));
        add("Options saved.", QStringLiteral("选项已保存。"));
        add("Rebuild", QStringLiteral("重新编译"));
        add("Download", QStringLiteral("下载"));
        add("Build succeeded", QStringLiteral("编译成功"));
        add("Build failed", QStringLiteral("编译失败"));
        add("%1 - %2 Error(s), %3 Warning(s)", QStringLiteral("%1 - %2 个错误，%3 个警告"));
        add("Format", QStringLiteral("格式化"));

        // ---- Options for Target ----
        add("Options for Target '%1'", QStringLiteral("目标 '%1' 的选项"));
        add("Device:", QStringLiteral("器件："));
        add("Vendor:", QStringLiteral("厂商："));
        add("CPU:", QStringLiteral("CPU："));
        add("Target", QStringLiteral("目标"));
        add("Output", QStringLiteral("输出"));
        add("C/C++", QStringLiteral("C/C++"));
        add("Linker", QStringLiteral("链接器"));
        add("Create HEX File", QStringLiteral("生成 HEX 文件"));
        add("Debug Information", QStringLiteral("调试信息"));
        add("Browse Information", QStringLiteral("浏览信息"));
        add("Create Library", QStringLiteral("生成库"));
        add("Name of Executable:", QStringLiteral("输出文件名："));
        add("Output Directory:", QStringLiteral("输出目录："));
        add("Listing Directory:", QStringLiteral("列表目录："));
        add("Optimization:", QStringLiteral("优化等级："));
        add("C99 Mode (--c99)", QStringLiteral("C99 模式 (--c99)"));
        add("One ELF Section per Function", QStringLiteral("每个函数独立段"));
        add("Strict ANSI", QStringLiteral("严格 ANSI"));
        add("Enum as int", QStringLiteral("枚举按 int 处理"));
        add("Plain char is signed", QStringLiteral("char 默认有符号"));
        add("Interworking (--apcs=interwork)", QStringLiteral("ARM/Thumb 互调用"));
        add("No Warnings (-W)", QStringLiteral("关闭警告 (-W)"));
        add("Define:", QStringLiteral("宏定义："));
        add("Include Paths:", QStringLiteral("头文件路径："));
        add("Misc Controls:", QStringLiteral("其它选项："));
        add("Use Memory Layout from Target Dialog", QStringLiteral("使用目标对话框的内存布局"));
        add("Use MicroLIB", QStringLiteral("使用 MicroLIB"));
        add("Don't search Standard Libraries", QStringLiteral("不链接标准库"));
        add("Select Scatter File", QStringLiteral("选择分散加载文件"));
        add("Scatter File:", QStringLiteral("分散加载文件："));
        add("IROM1 Start:", QStringLiteral("IROM1 起始："));
        add("IRAM1 Start:", QStringLiteral("IRAM1 起始："));
        add("Scatter Files (*.sct);;All Files (*)", QStringLiteral("分散加载文件 (*.sct);;所有文件 (*)"));

        // ---- Configure Toolchain ----
        add("Configure Toolchain", QStringLiteral("配置编译器"));
        add("Compiler Folder:", QStringLiteral("编译器目录："));
        add("Family:", QStringLiteral("系列："));
        add("Browse...", QStringLiteral("浏览..."));
        add("Select ARM Compiler bin folder", QStringLiteral("选择 ARM 编译器的 bin 目录"));
        add("Detected: %1    compiler = %2", QStringLiteral("已识别: %1    编译器 = %2"));

        // ---- 工程树右键 ----
        add("Open", QStringLiteral("打开"));
        add("Options for File...", QStringLiteral("文件选项..."));

        // ---- 文件过滤 ----
        add("uVision Project (*.uvprojx)", QStringLiteral("uVision 工程 (*.uvprojx)"));
        add("uVision Project (*.uvprojx *.uvproj);;All Files (*)",
            QStringLiteral("uVision 工程 (*.uvprojx *.uvproj);;所有文件 (*)"));

        // ---- 编码 ----
        add("Encoding", QStringLiteral("编码"));
        add("Reload with Encoding", QStringLiteral("按此编码重新载入"));
        add("System Default", QStringLiteral("系统默认"));
        add("Save as UTF-8", QStringLiteral("另存为 UTF-8"));

        return d;
    }();
    return dict;
}

} // namespace

namespace Lang {

void set(Id id)
{
    g_current = id;
}

Id current()
{
    return g_current;
}

QString t(const char* en)
{
    const QString key = QString::fromUtf8(en);
    if (g_current == Id::English)
        return key;
    return dictionary().value(key, key);
}

QString idToString(Id id)
{
    return id == Id::Chinese ? QStringLiteral("zh") : QStringLiteral("en");
}

Id idFromString(const QString& s)
{
    return s.compare(QLatin1String("zh"), Qt::CaseInsensitive) == 0 ? Id::Chinese : Id::English;
}

} // namespace Lang
