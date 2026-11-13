#include "core/Formatter.h"

#include "core/Settings.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryFile>

namespace {

// 没有 .clang-format 时用的兜底风格（和工程默认风格一致）
const char* kFallbackStyle =
    "{BasedOnStyle: LLVM, IndentWidth: 4, TabWidth: 4, UseTab: Never, "
    "BreakBeforeBraces: Allman, ColumnLimit: 0, "
    "AllowShortFunctionsOnASingleLine: None, SortIncludes: false, "
    "IndentCaseLabels: false, SpaceAfterCStyleCast: false}";

// 从文件所在目录往上找 .clang-format / _clang-format
QString findStyleFile(const QString& filePath)
{
    if (filePath.isEmpty())
        return QString();

    QDir dir = QFileInfo(filePath).absoluteDir();
    while (true) {
        for (const QString& name : {QStringLiteral(".clang-format"),
                                    QStringLiteral("_clang-format")}) {
            const QString candidate = dir.absoluteFilePath(name);
            if (QFileInfo(candidate).isFile())
                return candidate;
        }
        if (!dir.cdUp())
            break;
    }
    return QString();
}

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

    // 输入走临时文件：直接往 stdin 写的话，大文件会和 stdout 互相等
    // （管道写满就死锁），之前就是这样超时的，只是那会儿有内置缩进器兜着才没暴露。
    QTemporaryFile temp(QDir::tempPath() + QStringLiteral("/mdk-ose-XXXXXX.c"));
    if (!temp.open()) {
        r.error = QStringLiteral("创建临时文件失败");
        return r;
    }
    temp.write(input.toUtf8());
    temp.flush();
    const QString tempPath = temp.fileName();

    // 风格：用户明确选了就用他选的；选"自动"时工程里有 .clang-format 就按它，
    // 没有才用内置兜底风格
    const QString style = Settings::instance().formatStyle();
    QString styleArg;
    if (style == QLatin1String("__builtin")) {
        styleArg = QStringLiteral("-style=") + QLatin1String(kFallbackStyle);
    } else if (!style.isEmpty()) {
        styleArg = QStringLiteral("-style=") + style;
    } else if (!findStyleFile(filePath).isEmpty()) {
        styleArg = QStringLiteral("-style=file");
    } else {
        styleArg = QStringLiteral("-style=") + QLatin1String(kFallbackStyle);
    }

    QProcess proc;
    proc.setProgram(clang);
    proc.setArguments({
        styleArg,
        QStringLiteral("-assume-filename=")
            + QDir::toNativeSeparators(QFileInfo(filePath).absoluteFilePath()),
        QDir::toNativeSeparators(tempPath),
    });

    proc.start();
    if (!proc.waitForStarted(3000)) {
        r.error = QStringLiteral("clang-format 启动失败");
        return r;
    }

    // 一边等一边把 stdout 收走，避免子进程写满缓冲区阻塞
    QByteArray output;
    QElapsedTimer timer;
    timer.start();
    while (!proc.waitForFinished(50)) {
        output += proc.readAllStandardOutput();
        if (timer.elapsed() > 15000) {
            proc.kill();
            proc.waitForFinished(1000);
            r.error = QStringLiteral("clang-format 超时");
            return r;
        }
    }
    output += proc.readAllStandardOutput();

    if (proc.exitCode() != 0) {
        r.error = QString::fromLocal8Bit(proc.readAllStandardError()).trimmed();
        if (r.error.isEmpty())
            r.error = QStringLiteral("clang-format 返回 %1").arg(proc.exitCode());
        return r;
    }

    r.text = QString::fromUtf8(output);
    r.ok = true;
    return r;
}

QString Formatter::activeStyleName(const QString& filePath)
{
    const QString style = Settings::instance().formatStyle();
    if (style == QLatin1String("__builtin"))
        return QStringLiteral("内置（4 空格 + Allman）");
    if (!style.isEmpty())
        return style;
    if (!findStyleFile(filePath).isEmpty())
        return QStringLiteral("工程 .clang-format");
    return QStringLiteral("内置（4 空格 + Allman）");
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
