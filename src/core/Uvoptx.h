#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// .uvoptx 是 uVision 的"工程选项"文件（和 .uvprojx 配对，同目录同名）。
// 里面存的是 .uvprojx 不放的东西：
//   - 文件扩展名 -> 编辑器类型的映射（Extensions）
//   - 每个 Target 的调试器/仿真器配置（DebugOpt、TargetDriverDllRegistry）
//   - Watch / Memory 窗口里的表达式
//   - 断点、窗口开关状态
//
// 我们只读不写：读它可以继承你在 Keil 里配好的烧写参数、
// 当前 target、Watch 表达式；写它风险太大（uVision 自己也经常重写）。

struct UvoptxTarget
{
    QString name;
    bool    current = false;        // OPTFL/IsCurrentTarget
    bool    simulate = false;       // DebugOpt/uSim：1=软件仿真，0=硬件
    QString driver;                 // DebugOpt/pMon，例如 Segger\\JL2CM3.dll
    QString driverArgs;             // TargetDriverDllRegistry 里对应驱动的命令行

    // 从 J-Link 的参数里解析出来的（解析不到就是空 / 0）
    QString device;                 // -FP0($$Device:GD32F103RE$...)
    int     speedKhz = 0;           // -ZTIFSpeedSel5000
    quint32 flashAddress = 0;       // -FS08000000
    quint32 flashSize = 0;          // -FL080000
    QString flashAlgorithm;         // -FF0GD32F10x_HD.FLM

    QStringList watchExpressions;   // WatchWindow1/2 里的 ItemText
    QStringList memoryExpressions;  // MemoryWindow1 里的 ItemText

    struct Breakpoint
    {
        QString file;
        int     line = 0;
    };
    QList<Breakpoint> breakpoints;
};

struct Uvoptx
{
    QString filePath;
    bool    valid = false;

    // <Extensions>
    QStringList cExt;      // *.c
    QStringList aExt;      // *.s*; *.src; *.a*
    QStringList cppExt;    // *.cpp; *.cc; *.cxx
    QStringList textExt;   // *.txt; *.h; *.inc; *.md

    QList<UvoptxTarget> targets;

    const UvoptxTarget* target(const QString& name) const;
    const UvoptxTarget* currentTarget() const;
};

namespace UvoptxReader {

// .uvprojx 路径 -> 同目录同名的 .uvoptx 路径（不管存不存在）
QString pathForProject(const QString& uvprojxPath);

// 解析失败只返回 false，不影响主流程（.uvoptx 本来就可能是空的）
bool load(const QString& path, Uvoptx& out, QString* error = nullptr);

} // namespace UvoptxReader
