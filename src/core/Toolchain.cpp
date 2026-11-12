#include "core/Toolchain.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>

QString Toolchain::exeSuffix()
{
#ifdef Q_OS_WIN
    return QStringLiteral(".exe");
#else
    return QString();
#endif
}

static QString findTool(const QString& dir, const QString& base)
{
    if (dir.isEmpty())
        return QString();

    const QString candidate = QDir(dir).absoluteFilePath(base + Toolchain::exeSuffix());
    if (QFileInfo(candidate).isFile())
        return candidate;

    // 没有 .exe 的版本（linux 手工放的）
    const QString plain = QDir(dir).absoluteFilePath(base);
    if (QFileInfo(plain).isFile())
        return plain;

    return QString();
}

Toolchain Toolchain::detect(const QString& binDir, const QString& preferFamily)
{
    Toolchain tc;
    tc.binDir = QDir(binDir).absolutePath();

    const QString armcc = findTool(binDir, QStringLiteral("armcc"));
    const QString armclang = findTool(binDir, QStringLiteral("armclang"));

    const bool wantAc6 = preferFamily.compare(QLatin1String("AC6"), Qt::CaseInsensitive) == 0;

    if (!armcc.isEmpty() && (!wantAc6 || armclang.isEmpty())) {
        tc.family = QStringLiteral("AC5");
        tc.cc = armcc;
    } else if (!armclang.isEmpty()) {
        tc.family = QStringLiteral("AC6");
        tc.cc = armclang;
    } else if (!armcc.isEmpty()) {
        tc.family = QStringLiteral("AC5");
        tc.cc = armcc;
    } else {
        tc.binDir = QDir(binDir).absolutePath();
        return tc;
    }

    tc.as = findTool(binDir, QStringLiteral("armasm"));
    tc.ld = findTool(binDir, QStringLiteral("armlink"));
    tc.from = findTool(binDir, QStringLiteral("fromelf"));
    return tc;
}

QString Toolchain::displayName() const
{
    if (binDir.isEmpty())
        return QStringLiteral("(未配置编译器)");
    return QStringLiteral("ARM Compiler %1").arg(family);
}

QStringList Toolchain::missingTools() const
{
    QStringList miss;
    if (cc.isEmpty())
        miss << QStringLiteral("armcc/armclang");
    if (as.isEmpty())
        miss << QStringLiteral("armasm");
    if (ld.isEmpty())
        miss << QStringLiteral("armlink");
    if (from.isEmpty())
        miss << QStringLiteral("fromelf");
    return miss;
}

QString Toolchain::guessKeilRoot()
{
    QString root;

#ifdef Q_OS_WIN
    // Keil 安装时把路径写在这里
    const QStringList keys = {
        QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Keil\\Products\\MDK"),
        QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Keil\\Products\\MDK")
    };
    for (const QString& k : keys) {
        QSettings reg(k, QSettings::NativeFormat);
        for (const QString& field : { QStringLiteral("Path"), QStringLiteral("InstallDir"),
                                      QStringLiteral("Folder") }) {
            QString v = reg.value(field).toString();
            if (v.isEmpty())
                continue;
            v = QDir::fromNativeSeparators(v);
            if (QFileInfo::exists(v)) {
                root = v;
                break;
            }
        }
        if (!root.isEmpty())
            break;
    }

    if (root.isEmpty()) {
        const QStringList guesses = {
            QStringLiteral("C:/Keil_v5"),
            QStringLiteral("C:/Keil"),
            QStringLiteral("D:/Keil_v5"),
            QStringLiteral("D:/Keil")
        };
        for (const QString& g : guesses) {
            if (QFileInfo::exists(g + QStringLiteral("/UV4/UV4.exe"))) {
                root = g;
                break;
            }
        }
    }
#endif

    return root;
}

QString Toolchain::guessBinDir(const QString& family)
{
    const QString root = guessKeilRoot();
    if (root.isEmpty())
        return QString();

    const QString sub = (family.compare(QLatin1String("AC6"), Qt::CaseInsensitive) == 0)
                            ? QStringLiteral("/ARM/ARMCLANG/bin")
                            : QStringLiteral("/ARM/ARMCC/bin");
    const QString dir = QDir::cleanPath(root + sub);
    return QFileInfo::exists(dir) ? dir : QString();
}

QString Toolchain::queryVersion() const
{
    if (cc.isEmpty())
        return QString();

    QProcess p;
    p.setProgram(cc);
    p.setArguments(family == QLatin1String("AC6") ? QStringList{ QStringLiteral("--version") }
                                                  : QStringList{ QStringLiteral("--vsn") });
    p.start();
    if (!p.waitForStarted(3000))
        return QString();
    if (!p.waitForFinished(5000))
        return QString();

    const QString out = QString::fromLocal8Bit(p.readAllStandardOutput())
                        + QString::fromLocal8Bit(p.readAllStandardError());

    // AC5:  Product: ARM Compiler 5.06 update 6 (build 750)
    static const QRegularExpression productRe(
        QStringLiteral("Product:\\s*(.+)"), QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch m = productRe.match(out);
    if (m.hasMatch()) {
        QString s = m.captured(1).trimmed();
        s.remove(QStringLiteral("ARM Compiler "));
        return s.trimmed();
    }

    // AC6: armclang --version 里找 "Arm Compiler for Embedded 6.x"
    static const QRegularExpression ac6Re(
        QStringLiteral("Compiler for Embedded\\s+([\\d.]+)"));
    m = ac6Re.match(out);
    if (m.hasMatch())
        return m.captured(1);

    static const QRegularExpression clangRe(QStringLiteral("clang version\\s+([\\d.]+)"));
    m = clangRe.match(out);
    if (m.hasMatch())
        return m.captured(1);

    return QString();
}
