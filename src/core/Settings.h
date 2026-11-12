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

    // ---- 构建 ----
    // 并行编译任务数，默认取 CPU 核数；1 表示串行
    int  buildJobs() const;
    void setBuildJobs(int jobs);

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
