#include "core/Settings.h"

#include <QFileInfo>
#include <QLocale>
#include <QSettings>
#include <QThread>

namespace {
const char* kToolchainDir  = "toolchain/dir";
const char* kToolchainFam  = "toolchain/family";
const char* kLastProject   = "project/last";
const char* kRecent        = "project/recent";
const char* kBuildJobs     = "build/jobs";
const char* kLanguage      = "ui/language";
const char* kFormatOnSave  = "editor/formatOnSave";
const char* kClangFormat   = "editor/clangFormatPath";
const char* kFormatStyle   = "editor/formatStyle";
const char* kGeometry      = "window/geometry";
const char* kState         = "window/state";
}

Settings::Settings()
    : m_s(QSettings::IniFormat, QSettings::UserScope,
          QStringLiteral("linuxacme"), QStringLiteral("mdk-v5-ose"))
{
}

Settings& Settings::instance()
{
    static Settings s;
    return s;
}

QString Settings::toolchainDir() const
{
    return m_s.value(QLatin1String(kToolchainDir)).toString();
}

void Settings::setToolchainDir(const QString& dir)
{
    m_s.setValue(QLatin1String(kToolchainDir), dir);
}

QString Settings::toolchainFamily() const
{
    return m_s.value(QLatin1String(kToolchainFam), QStringLiteral("AC5")).toString();
}

void Settings::setToolchainFamily(const QString& family)
{
    m_s.setValue(QLatin1String(kToolchainFam), family);
}

int Settings::buildJobs() const
{
    // 默认就是本机的逻辑处理器数（核数 x 超线程），不设人为上限
    const int fallback = qBound(1, QThread::idealThreadCount(), 64);
    const int value = m_s.value(QLatin1String(kBuildJobs), fallback).toInt();
    return qBound(1, value, 64);
}

void Settings::setBuildJobs(int jobs)
{
    m_s.setValue(QLatin1String(kBuildJobs), qBound(1, jobs, 64));
}

QString Settings::language() const
{
    // 首次运行跟随系统语言，中文系统直接给中文界面
    const QString fallback =
        QLocale::system().language() == QLocale::Chinese ? QStringLiteral("zh")
                                                         : QStringLiteral("en");
    return m_s.value(QLatin1String(kLanguage), fallback).toString();
}

void Settings::setLanguage(const QString& id)
{
    m_s.setValue(QLatin1String(kLanguage), id);
}

bool Settings::formatOnSave() const
{
    return m_s.value(QLatin1String(kFormatOnSave), true).toBool();
}

void Settings::setFormatOnSave(bool enabled)
{
    m_s.setValue(QLatin1String(kFormatOnSave), enabled);
}

QString Settings::clangFormatPath() const
{
    return m_s.value(QLatin1String(kClangFormat)).toString();
}

void Settings::setClangFormatPath(const QString& path)
{
    m_s.setValue(QLatin1String(kClangFormat), path);
}

QString Settings::formatStyle() const
{
    return m_s.value(QLatin1String(kFormatStyle)).toString();
}

void Settings::setFormatStyle(const QString& style)
{
    m_s.setValue(QLatin1String(kFormatStyle), style);
}

QString Settings::lastProject() const
{
    return m_s.value(QLatin1String(kLastProject)).toString();
}

void Settings::setLastProject(const QString& path)
{
    m_s.setValue(QLatin1String(kLastProject), path);
}

QStringList Settings::recentProjects() const
{
    return m_s.value(QLatin1String(kRecent)).toStringList();
}

void Settings::addRecentProject(const QString& path)
{
    if (path.isEmpty())
        return;

    const QString abs = QFileInfo(path).absoluteFilePath();
    QStringList list = recentProjects();
    list.removeAll(abs);
    list.prepend(abs);
    while (list.size() > 10)
        list.removeLast();

    m_s.setValue(QLatin1String(kRecent), list);
}

QByteArray Settings::geometry() const
{
    return m_s.value(QLatin1String(kGeometry)).toByteArray();
}

void Settings::setGeometry(const QByteArray& state)
{
    m_s.setValue(QLatin1String(kGeometry), state);
}

QByteArray Settings::windowState() const
{
    return m_s.value(QLatin1String(kState)).toByteArray();
}

void Settings::setWindowState(const QByteArray& state)
{
    m_s.setValue(QLatin1String(kState), state);
}

void Settings::sync()
{
    m_s.sync();
}
