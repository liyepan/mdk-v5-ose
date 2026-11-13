#include "core/Diagnostic.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

QString Diagnostic::severityText() const
{
    switch (severity) {
    case Severity::Error:   return QStringLiteral("Error");
    case Severity::Warning: return QStringLiteral("Warning");
    default:                return QStringLiteral("Info");
    }
}

namespace {

// armcc / armasm:  ".\main.c", line 45: Error:  #20: identifier "x" is undefined
const QRegularExpression& armccRe()
{
    static const QRegularExpression re(
        QStringLiteral("^\"?(?<file>[^\"]+?)\"?,\\s*line\\s+(?<line>\\d+)\\s*:\\s*"
                       "(?<sev>Fatal error|Error|Warning|Note)\\s*:\\s*"
                       "(?:#\\s*(?<code>\\d+(?:-[A-Za-z]+)?)\\s*:\\s*)?(?<msg>.*)$"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

// gcc: /src/main.c:45:12: error: 'x' undeclared   (also tolerates Windows drive colon)
const QRegularExpression& gccRe()
{
    static const QRegularExpression re(
        QStringLiteral("^(?<file>.+?):(?<line>\\d+):(?:(?<col>\\d+):)?\\s*"
                       "(?<sev>fatal error|error|warning|note|remark)\\s*:\\s*(?<msg>.*)$"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

// armlink: Error: L6218E: Undefined symbol xxx (referred from main.o).
const QRegularExpression& armlinkRe()
{
    static const QRegularExpression re(
        QStringLiteral("^(?<sev>Error|Warning|Fatal error)\\s*:\\s*(?<code>L\\d+[A-Z]?)\\s*:\\s*(?<msg>.*)$"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

Severity toSeverity(const QString& text)
{
    if (text.compare(QLatin1String("Warning"), Qt::CaseInsensitive) == 0)
        return Severity::Warning;
    if (text.compare(QLatin1String("Note"), Qt::CaseInsensitive) == 0
        || text.compare(QLatin1String("remark"), Qt::CaseInsensitive) == 0)
        return Severity::Info;
    return Severity::Error;
}

QString absoluteFile(const QString& raw, const QString& projectDir)
{
    QString p = raw.trimmed();
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (p.isEmpty())
        return QString();

    const QFileInfo fi(p);
    if (fi.isAbsolute())
        return QDir::cleanPath(p);

    if (p.startsWith(QLatin1String("./")) || p.startsWith(QLatin1String("../")))
        return QDir::cleanPath(projectDir + QLatin1Char('/') + p);

    // armcc 有时给出相对于当前目录的裸文件名
    const QString inProject = QDir::cleanPath(projectDir + QLatin1Char('/') + p);
    if (QFileInfo::exists(inProject))
        return inProject;

    return QDir::cleanPath(p);
}

} // namespace

namespace Diagnostics {

bool parseLine(const QString& line, const QString& projectDir, Diagnostic* out)
{
    if (line.trimmed().isEmpty())
        return false;

    Diagnostic d;
    d.raw = line;

    QRegularExpressionMatch m = armccRe().match(line);
    if (m.hasMatch()) {
        d.severity = toSeverity(m.captured(QStringLiteral("sev")));
        d.file = absoluteFile(m.captured(QStringLiteral("file")), projectDir);
        d.line = m.captured(QStringLiteral("line")).toInt();
        d.code = m.captured(QStringLiteral("code"));
        d.message = m.captured(QStringLiteral("msg")).trimmed();
        if (out)
            *out = d;
        return true;
    }

    m = gccRe().match(line);
    if (m.hasMatch()) {
        d.severity = toSeverity(m.captured(QStringLiteral("sev")));
        d.file = absoluteFile(m.captured(QStringLiteral("file")), projectDir);
        d.line = m.captured(QStringLiteral("line")).toInt();
        d.column = m.captured(QStringLiteral("col")).toInt();
        d.message = m.captured(QStringLiteral("msg")).trimmed();
        if (out)
            *out = d;
        return true;
    }

    m = armlinkRe().match(line);
    if (m.hasMatch()) {
        d.severity = toSeverity(m.captured(QStringLiteral("sev")));
        d.code = m.captured(QStringLiteral("code"));
        d.message = m.captured(QStringLiteral("msg")).trimmed();
        if (out)
            *out = d;
        return true;
    }

    return false;
}

} // namespace Diagnostics
