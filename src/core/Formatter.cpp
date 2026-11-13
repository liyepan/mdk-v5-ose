#include "core/Formatter.h"

#include "core/Settings.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>

namespace {

// 没有 .clang-format 时用的兜底风格（和工程默认风格一致）
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

Formatter::Result Formatter::format(const QString& input, const QString& filePath)
{
    const QString clang = detectClangFormat();
    if (clang.isEmpty()) {
        Result r;
        r.engine = QStringLiteral("clang-format");
        r.error = QStringLiteral(
            "没找到 clang-format。格式化只用它：装一个 LLVM，"
            "或者在 Tools -> Configure Toolchain 里指定 clang-format 路径。");
        return r;
    }

    // 有 clang-format 就只认它，失败就报错，不再拿内置缩进器偷偷顶上
    return runClangFormat(clang, input, filePath);
}
