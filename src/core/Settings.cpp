#include "core/Settings.h"

#include <QFileInfo>
#include <QSettings>

namespace {
const char* kToolchainDir  = "toolchain/dir";
const char* kToolchainFam  = "toolchain/family";
const char* kLastProject   = "project/last";
const char* kRecent        = "project/recent";
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
