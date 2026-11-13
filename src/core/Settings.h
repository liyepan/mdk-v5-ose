#pragma once

#include <QSettings>
#include <QString>
#include <QStringList>
#include <QByteArray>

// 全局配置。刻意写在 INI 里而不是注册表，方便排查和迁移：
//   Windows: %APPDATA%/linuxacme/mdk-v5-ose.ini
//   Linux:   ~/.config/linuxacme/mdk-v5-ose.ini
class Settings
{
public:
    static Settings& instance();

    // ---- 工具链 ----
    // 用户手工选择的编译器目录，例如 C:/Keil_v5/ARM/ARMCC/bin
    QString toolchainDir() const;
    void setToolchainDir(const QString& dir);

    // "AC5" 或 "AC6"
    QString toolchainFamily() const;
    void setToolchainFamily(const QString& family);

    // ---- 烧写（不改 .uvprojx，全存在自己的 INI 里） ----
    QString flashBackend() const;
    void setFlashBackend(const QString& backend);
    QString flashToolPath() const;
    void setFlashToolPath(const QString& path);
    QString flashInterface() const;
    void setFlashInterface(const QString& iface);
    int     flashSpeed() const;
    void    setFlashSpeed(int khz);
    bool    flashVerify() const;
    void    setFlashVerify(bool on);
    bool    flashResetAfter() const;
    void    setFlashResetAfter(bool on);
    QString flashOpenOcdInterface() const;
    void    setFlashOpenOcdInterface(const QString& cfg);
    QString flashOpenOcdTarget() const;
    void    setFlashOpenOcdTarget(const QString& cfg);
    QString flashCustomCommand() const;
    void    setFlashCustomCommand(const QString& command);

    // ---- 构建 ----
    // 并行编译任务数，默认取本机逻辑处理器数（核数 x 超线程）；1 表示串行
    int  buildJobs() const;
    void setBuildJobs(int jobs);

    // ---- 界面语言： "en" / "zh" ----
    QString language() const;
    void setLanguage(const QString& id);

    // ---- 编辑器 ----
    bool formatOnSave() const;
    void setFormatOnSave(bool enabled);

    // 空表示自动探测
    QString clangFormatPath() const;
    void setClangFormatPath(const QString& path);

    // 格式化风格：空 = 自动（工程里有 .clang-format 就用它，否则用内置风格）
    // 其它值就是 clang-format 的内置风格名：LLVM / Google / Chromium / Mozilla / WebKit / Microsoft / GNU
    QString formatStyle() const;
    void setFormatStyle(const QString& style);

    // ---- 最近打开的工程 ----
    QString lastProject() const;
    void setLastProject(const QString& path);

    QStringList recentProjects() const;
    void addRecentProject(const QString& path);

    // ---- 窗口状态 ----
    QByteArray geometry() const;
    void setGeometry(const QByteArray& state);

    QByteArray windowState() const;
    void setWindowState(const QByteArray& state);

    void sync();

private:
    Settings();

    QSettings m_s;
};
