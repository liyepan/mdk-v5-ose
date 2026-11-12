#include "core/SymbolIndex.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace {

const QSet<QString>& cKeywords()
{
    static const QSet<QString> kw = {
        QStringLiteral("if"),     QStringLiteral("else"),     QStringLiteral("for"),
        QStringLiteral("while"),  QStringLiteral("switch"),   QStringLiteral("case"),
        QStringLiteral("do"),     QStringLiteral("return"),   QStringLiteral("sizeof"),
        QStringLiteral("goto"),   QStringLiteral("break"),    QStringLiteral("continue"),
        QStringLiteral("defined"), QStringLiteral("asm"),     QStringLiteral("__asm"),
        QStringLiteral("__attribute__"), QStringLiteral("typedef")
    };
    return kw;
}

// 函数定义 / 声明：行首若干修饰符 + 名字 + (
const QRegularExpression& functionRe()
{
    static const QRegularExpression re(QStringLiteral(
        "^[ \\t]{0,12}(?:static[ \\t]+|inline[ \\t]+|__INLINE[ \\t]+|extern[ \\t]+|"
        "const[ \\t]+|volatile[ \\t]+|unsigned[ \\t]+|signed[ \\t]+|register[ \\t]+)*"
        "[A-Za-z_][A-Za-z0-9_ \\t\\*]*?\\b([A-Za-z_][A-Za-z0-9_]*)[ \\t]*\\("));
    return re;
}

const QRegularExpression& macroRe()
{
    static const QRegularExpression re(
        QStringLiteral("^[ \\t]*#[ \\t]*define[ \\t]+([A-Za-z_][A-Za-z0-9_]*)"));
    return re;
}

const QRegularExpression& includeRe()
{
    static const QRegularExpression re(
        QStringLiteral("^[ \\t]*#[ \\t]*include[ \\t]*[<\"]([^>\"]+)[>\"]"));
    return re;
}

QString readFileText(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();

    const QByteArray raw = f.readAll();
    QString text = QString::fromUtf8(raw);
    if (text.contains(QChar(0xFFFD)))
        text = QString::fromLocal8Bit(raw);
    return text;
}

} // namespace

void SymbolIndex::clear()
{
    m_symbols.clear();
    m_indexed.clear();
    m_built = false;
}

void SymbolIndex::indexFile(const QString& path)
{
    if (m_indexed.contains(path))
        return;
    m_indexed.insert(path);

    const QString text = readFileText(path);
    if (text.isEmpty())
        return;

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString& line = lines.at(i);

        const QRegularExpressionMatch mm = macroRe().match(line);
        if (mm.hasMatch()) {
            SymbolLocation loc;
            loc.file = path;
            loc.line = i + 1;
            loc.definition = true;
            m_symbols[mm.captured(1)].append(loc);
            continue;
        }

        const QRegularExpressionMatch fm = functionRe().match(line);
        if (!fm.hasMatch())
            continue;

        const QString name = fm.captured(1);
        if (cKeywords().contains(name) || name.isEmpty())
            continue;

        // 判断是定义还是声明：本行有 { 就是定义，有 ; 就是声明，
        // 都没有就往下看最多三行（Allman 风格的花括号在下一行）
        bool definition = false;
        bool declaration = false;

        QString tail = line.mid(fm.capturedEnd(1));
        if (tail.contains(QLatin1Char('{')))
            definition = true;
        else if (tail.contains(QLatin1Char(';')))
            declaration = true;
        else {
            for (int k = 1; k <= 3 && i + k < lines.size(); ++k) {
                const QString next = lines.at(i + k);
                if (next.contains(QLatin1Char('{'))) {
                    definition = true;
                    break;
                }
                if (next.contains(QLatin1Char(';'))) {
                    declaration = true;
                    break;
                }
            }
        }

        SymbolLocation loc;
        loc.file = path;
        loc.line = i + 1;
        loc.definition = definition;
        m_symbols[name].append(loc);
        Q_UNUSED(declaration);
    }
}

void SymbolIndex::indexIncludes(const QString& path, const QStringList& includeDirs)
{
    const QString text = readFileText(path);
    if (text.isEmpty())
        return;

    const QFileInfo fi(path);
    QSet<QString> seen;

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        const QRegularExpressionMatch m = includeRe().match(line);
        if (!m.hasMatch())
            continue;

        const QString name = m.captured(1).trimmed();

        const QString local = QDir::cleanPath(fi.absolutePath() + QLatin1Char('/') + name);
        if (QFileInfo::exists(local)) {
            if (!seen.contains(local)) {
                seen.insert(local);
                indexFile(local);
            }
            continue;
        }

        for (const QString& dir : includeDirs) {
            const QString cand = QDir::cleanPath(dir + QLatin1Char('/') + name);
            if (QFileInfo::exists(cand)) {
                if (!seen.contains(cand)) {
                    seen.insert(cand);
                    indexFile(cand);
                }
                break;
            }
        }
    }
}

void SymbolIndex::build(const UvProject& project, int targetIndex)
{
    clear();

    const Target* target = project.target(targetIndex);
    if (!target)
        return;

    const QString projectDir = project.directory();

    QStringList includeDirs;
    for (const QString& inc : splitOptionList(target->includePaths)) {
        QString p = inc;
        p.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (!QFileInfo(p).isAbsolute())
            p = QDir::cleanPath(projectDir + QLatin1Char('/') + p);
        includeDirs << p;
    }

    for (const ProjGroup& group : target->groups) {
        if (group.excluded)
            continue;
        for (const ProjFile& file : group.files) {
            if (file.excluded)
                continue;

            QString p = file.path;
            p.replace(QLatin1Char('\\'), QLatin1Char('/'));
            if (!QFileInfo(p).isAbsolute())
                p = QDir::cleanPath(projectDir + QLatin1Char('/') + p);

            if (!QFileInfo::exists(p))
                continue;

            indexFile(p);
            indexIncludes(p, includeDirs);
        }
    }

    m_built = true;
}

QList<SymbolLocation> SymbolIndex::lookup(const QString& name) const
{
    QList<SymbolLocation> all = m_symbols.value(name);

    // 定义排前面，同一文件同一行去重
    QList<SymbolLocation> sorted;
    for (const SymbolLocation& loc : all) {
        if (loc.definition)
            sorted.prepend(loc);
        else
            sorted.append(loc);
    }
    return sorted;
}
