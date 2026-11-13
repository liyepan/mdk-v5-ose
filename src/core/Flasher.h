#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;
class QTimer;

// 烧写配置。存在 Settings 的 INI 里，**不往 .uvprojx 写任何东西**，
// 免得破坏 Keil 自己的配置。
struct FlashConfig
{
    QString backend = QStringLiteral("jlink");   // jlink / openocd / pyocd / custom
    QString toolPath;                            // 空 = 自动探测 PATH
    QString interface = QStringLiteral("SWD");   // SWD / JTAG
    int     speed = 4000;                        // kHz
    bool    verify = true;
    bool    resetAfter = true;

    // OpenOCD 需要这两个 cfg
    QString openocdInterface = QStringLiteral("interface/cmsis-dap.cfg");
    QString openocdTarget;

    // custom 后端用的命令行模板，支持占位符：
    //   {file} {device} {interface} {speed} {addr}
    QString customCommand;
};

struct FlashRequest
{
    QString     filePath;                 // .hex / .elf(.axf) / .bin
    bool        binary = false;
    quint32     baseAddress = 0x08000000;
    QString     device;                   // 例如 GD32F103RE
    QString     workDir;
    FlashConfig config;
};

// 调外部烧写工具（J-Link Commander / OpenOCD / pyOCD / 自定义命令），
// 把输出逐行发给界面。
class Flasher : public QObject
{
    Q_OBJECT

public:
    explicit Flasher(QObject* parent = nullptr);

    // 返回可执行文件的完整路径，找不到返回空
    static QString detectTool(const QString& backend);
    // 后端名 -> 程序名（Windows/Linux 都试）
    static QStringList toolNames(const QString& backend);

    void start(const FlashRequest& request);
    void cancel();

    bool isRunning() const;

signals:
    void output(const QString& line);
    void finished(bool ok, const QString& error);

private:
    void handleStderr();
    void handleStdout();
    void fail(const QString& error);
    void succeed();

    QProcess* m_process = nullptr;
    QTimer*   m_timeout = nullptr;
    QString   m_scriptPath;      // J-Link 的临时脚本，结束后删掉
    QString   m_errorText;
    bool      m_finished = false;
};
