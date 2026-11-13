#include "core/Formatter.h"

#include "core/Settings.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>

namespace {

// 数一行的花括号净增量，跳过字符串、字符常量和注释
int braceDelta(const QString& line, bool& inBlockComment)
{
    int delta = 0;
    bool inString = false;
    bool inChar = false;
    bool escaped = false;

    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);

        if (inBlockComment) {
            if (c == QLatin1Char('*') && i + 1 < line.size()
                && line.at(i + 1) == QLatin1Char('/')) {
                inBlockComment = false;
                ++i;
            }
            continue;
        }

        if (inString || inChar) {
            if (escaped) {
                escaped = false;
                continue;
            }
            if (c == QLatin1Char('\\')) {
                escaped = true;
                continue;
            }
            if (inString && c == QLatin1Char('"'))
                inString = false;
            else if (inChar && c == QLatin1Char('\''))
                inChar = false;
            continue;
        }

        if (c == QLatin1Char('/') && i + 1 < line.size()) {
            if (line.at(i + 1) == QLatin1Char('/'))
                break;
            if (line.at(i + 1) == QLatin1Char('*')) {
                inBlockComment = true;
                ++i;
                continue;
            }
        }

        if (c == QLatin1Char('"')) {
            inString = true;
            continue;
        }
        if (c == QLatin1Char('\'')) {
            inChar = true;
            continue;
        }
        if (c == QLatin1Char('{'))
            ++delta;
        else if (c == QLatin1Char('}'))
            --delta;
    }

    return delta;
}

const char* kFallbackStyle =
    "{BasedOnStyle: LLVM, IndentWidth: 4, TabWidth: 4, UseTab: Never, "
    "BreakBeforeBraces: Allman, ColumnLimit: 0, "
    "AllowShortFunctionsOnASingleLine: None, SortIncludes: false, "
    "IndentCaseLabels: false, SpaceAfterCStyleCast: false}";

} // namespace

QString Formatter::detectClangFormat()
{
    const QString configured = Settings::instance().clangFormatPath();
    if (!configured.isEmpty() && QFileInfo(configured).isFile())
        return configured;

    const QString inPath = QStandardPaths::findExecutable(QStringLiteral("clang-format"));
    if (!inPath.isEmpty())
        return inPath;

#if defined(Q_OS_WIN)
    QStringList candidates;
    candidates << QStringLiteral("C:/Program Files/LLVM/bin/clang-format.exe")
               << QStringLiteral("C:/Program Files (x86)/LLVM/bin/clang-format.exe");

    // VS Code 的 C/C++ 插件自带一份
    const QDir extDir(QDir::homePath() + QStringLiteral("/.vscode/extensions"));
    if (extDir.exists()) {
        const QFileInfoList dirs = extDir.entryInfoList(
            QStringList{ QStringLiteral("ms-vscode.cpptools-*") }, QDir::Dirs);
        for (const QFileInfo& d : dirs) {
            const QString cand = d.absoluteFilePath()
                                 + QStringLiteral("/LLVM/bin/clang-format.exe");
            if (QFileInfo::exists(cand))
                candidates << cand;
        }
    }

    for (const QString& c : candidates) {
        if (QFileInfo(c).isFile())
            return c;
    }
#endif

    return QString();
}

bool Formatter::hasClangFormat()
{
    return !detectClangFormat().isEmpty();
}

Formatter::Result Formatter::runClangFormat(const QString& clang, const QString& input,
                                            const QString& filePath)
{
    Result r;
    r.engine = QStringLiteral("clang-format");

    QProcess proc;
    proc.setProgram(clang);
    proc.setArguments({
        QStringLiteral("-style=file"),
        QStringLiteral("-fallback-style=") + QLatin1String(kFallbackStyle),
        QStringLiteral("-assume-filename=")
            + QDir::toNativeSeparators(QFileInfo(filePath).absoluteFilePath()),
    });

    proc.start();
    if (!proc.waitForStarted(3000)) {
        r.error = QStringLiteral("clang-format 启动失败");
        return r;
    }

    proc.write(input.toUtf8());
    proc.closeWriteChannel();

    if (!proc.waitForFinished(15000)) {
        proc.kill();
        r.error = QStringLiteral("clang-format 超时");
        return r;
    }

    if (proc.exitCode() != 0) {
        r.error = QString::fromLocal8Bit(proc.readAllStandardError()).trimmed();
        if (r.error.isEmpty())
            r.error = QStringLiteral("clang-format 返回 %1").arg(proc.exitCode());
        return r;
    }

    r.text = QString::fromUtf8(proc.readAllStandardOutput());
    r.ok = true;
    return r;
}

QString Formatter::builtinIndent(const QString& input)
{
    // 只改行首空白，其它一个字符都不动，所以永远不会改坏代码
    QStringList result;
    const QStringList lines = input.split(QLatin1Char('\n'));

    int depth = 0;
    bool inBlockComment = false;

    for (const QString& rawLine : lines) {
        QString line = rawLine;
        while (!line.isEmpty()
               && (line.at(line.size() - 1) == QLatin1Char(' ')
                   || line.at(line.size() - 1) == QLatin1Char('\t')))
            line.chop(1);

        const QString trimmed = line.trimmed();

        if (trimmed.isEmpty()) {
            result << QString();
            continue;
        }

        if (!inBlockComment && trimmed.startsWith(QLatin1Char('#'))) {
            result << trimmed;
            const int delta = braceDelta(trimmed, inBlockComment);
            depth += delta;
            if (depth < 0)
                depth = 0;
            continue;
        }

        if (inBlockComment) {
            result << QString(depth * 4, QLatin1Char(' ')) + trimmed;
            braceDelta(trimmed, inBlockComment);
            continue;
        }

        bool dedent = trimmed.startsWith(QLatin1String("case "))
                      || trimmed.startsWith(QLatin1String("default:"));
        int lineDepth = depth - (dedent ? 1 : 0);
        if (trimmed.startsWith(QLatin1Char('}')))
            --lineDepth;
        if (lineDepth < 0)
            lineDepth = 0;

        result << QString(lineDepth * 4, QLatin1Char(' ')) + trimmed;

        depth += braceDelta(trimmed, inBlockComment);
        if (depth < 0)
            depth = 0;
    }

    return result.join(QLatin1Char('\n'));
}

Formatter::Result Formatter::format(const QString& input, const QString& filePath)
{
    Result r;
    const QString clang = detectClangFormat();
    if (!clang.isEmpty()) {
        r = runClangFormat(clang, input, filePath);
        if (r.ok)
            return r;
    }

    Result fallback;
    fallback.ok = true;
    fallback.engine = QStringLiteral("builtin");
    fallback.text = builtinIndent(input);
    if (clang.isEmpty())
        fallback.error = QStringLiteral("未找到 clang-format，已使用内置缩进器");
    else
        fallback.error = r.error;
    return fallback;
}
