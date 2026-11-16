#include "core/Uvoptx.h"

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace {

QString childText(const QDomElement& parent, const QString& tag, const QString& def = QString())
{
    const QDomElement e = parent.firstChildElement(tag);
    if (e.isNull())
        return def;
    const QString t = e.text().trimmed();
    return t.isEmpty() ? def : t;
}

bool childFlag(const QDomElement& parent, const QString& tag, bool def = false)
{
    const QString t = childText(parent, tag);
    if (t.isEmpty())
        return def;
    return t == QLatin1String("1") || t.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
}

// "*.s*; *.src; *.a*" -> {"*.s*", "*.src", "*.a*"}
QStringList splitExtList(const QString& raw)
{
    QStringList out;
    for (const QString& part : raw.split(QLatin1Char(';'))) {
        const QString t = part.trimmed();
        if (!t.isEmpty())
            out << t;
    }
    return out;
}

// 把 *.s* 这种模式转成正则，用来判断某个文件名算不算这一类
QRegularExpression globToRegex(const QString& pattern)
{
    QString re = QRegularExpression::escape(pattern);
    re.replace(QStringLiteral("\\*"), QStringLiteral(".*"));
    re.replace(QStringLiteral("\\?"), QStringLiteral("."));
    return QRegularExpression(QStringLiteral("^") + re + QStringLiteral("$"),
                              QRegularExpression::CaseInsensitiveOption);
}

} // namespace

const UvoptxTarget* Uvoptx::target(const QString& name) const
{
    for (const UvoptxTarget& t : targets) {
        if (t.name == name)
            return &t;
    }
    return nullptr;
}

const UvoptxTarget* Uvoptx::currentTarget() const
{
    for (const UvoptxTarget& t : targets) {
        if (t.current)
            return &t;
    }
    return targets.isEmpty() ? nullptr : &targets.first();
}

namespace UvoptxReader {

QString pathForProject(const QString& uvprojxPath)
{
    if (uvprojxPath.isEmpty())
        return QString();
    QString path = uvprojxPath;
    if (path.endsWith(QLatin1String(".uvprojx"), Qt::CaseInsensitive))
        path.chop(8);
    return path + QStringLiteral(".uvoptx");
}

bool load(const QString& path, Uvoptx& out, QString* error)
{
    out = Uvoptx();
    out.filePath = path;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("打不开 .uvoptx: ") + file.errorString();
        return false;
    }

    QDomDocument doc;
    QString parseError;
    int line = 0;
    int column = 0;
    if (!doc.setContent(&file, &parseError, &line, &column)) {
        if (error)
            *error = QStringLiteral("解析 .uvoptx 失败 (%1:%2): %3")
                         .arg(line).arg(column).arg(parseError);
        return false;
    }
    file.close();

    const QDomElement root = doc.documentElement();
    if (root.tagName() != QLatin1String("ProjectOpt")) {
        if (error)
            *error = QStringLiteral("不是 uVision 的 .uvoptx（根节点是 %1）").arg(root.tagName());
        return false;
    }

    // ---- Extensions ----
    const QDomElement ext = root.firstChildElement(QStringLiteral("Extensions"));
    if (!ext.isNull()) {
        out.cExt = splitExtList(childText(ext, QStringLiteral("cExt")));
        out.aExt = splitExtList(childText(ext, QStringLiteral("aExt")));
        out.cppExt = splitExtList(childText(ext, QStringLiteral("CppX")));
        out.textExt = splitExtList(childText(ext, QStringLiteral("tExt")));
    }

    // ---- Target ----
    static const QRegularExpression speedRe(QStringLiteral("-ZTIFSpeedSel(\\d+)"));
    static const QRegularExpression deviceRe(QStringLiteral("\\$\\$Device:([^$]+)\\$"));
    static const QRegularExpression flashAddrRe(QStringLiteral("-FS([0-9A-Fa-f]+)"));
    static const QRegularExpression flashSizeRe(QStringLiteral("-FL([0-9A-Fa-f]+)"));
    static const QRegularExpression flashAlgoRe(QStringLiteral("-FF0([^ ]+)"));

    for (QDomElement tEl = root.firstChildElement(QStringLiteral("Target"));
         !tEl.isNull(); tEl = tEl.nextSiblingElement(QStringLiteral("Target"))) {

        UvoptxTarget target;
        target.name = childText(tEl, QStringLiteral("TargetName"));

        const QDomElement option = tEl.firstChildElement(QStringLiteral("TargetOption"));

        const QDomElement optfl = option.firstChildElement(QStringLiteral("OPTFL"));
        target.current = childFlag(optfl, QStringLiteral("IsCurrentTarget"));

        // 调试器
        const QDomElement debugOpt = option.firstChildElement(QStringLiteral("DebugOpt"));
        target.simulate = childFlag(debugOpt, QStringLiteral("uSim"));
        target.driver = childText(debugOpt, QStringLiteral("pMon"));

        // 驱动参数：JL2CM3 / UL2CM3 ...
        // 注意 pMon 里是 Windows 路径（Segger\JL2CM3.dll），
        // 在 Linux 上 QFileInfo 不认反斜杠，先统一成正斜杠
        QString driverPath = target.driver;
        driverPath.replace(QLatin1Char('\\'), QLatin1Char('/'));
        const QString driverKey = QFileInfo(driverPath).completeBaseName();
        const QDomElement registry =
            option.firstChildElement(QStringLiteral("TargetDriverDllRegistry"));
        for (QDomElement entry = registry.firstChildElement(QStringLiteral("SetRegEntry"));
             !entry.isNull(); entry = entry.nextSiblingElement(QStringLiteral("SetRegEntry"))) {
            const QString key = childText(entry, QStringLiteral("Key"));
            if (!driverKey.isEmpty() && key == driverKey)
                target.driverArgs = childText(entry, QStringLiteral("Name"));
        }

        if (!target.driverArgs.isEmpty()) {
            const QString& args = target.driverArgs;
            if (const auto m = speedRe.match(args); m.hasMatch())
                target.speedKhz = m.captured(1).toInt();
            if (const auto m = deviceRe.match(args); m.hasMatch())
                target.device = m.captured(1);
            if (const auto m = flashAddrRe.match(args); m.hasMatch())
                target.flashAddress = m.captured(1).toUInt(nullptr, 16);
            if (const auto m = flashSizeRe.match(args); m.hasMatch())
                target.flashSize = m.captured(1).toUInt(nullptr, 16);
            if (const auto m = flashAlgoRe.match(args); m.hasMatch())
                target.flashAlgorithm = m.captured(1);
        }

        // Watch / Memory
        for (const QString& tag : { QStringLiteral("WatchWindow1"), QStringLiteral("WatchWindow2") }) {
            const QDomElement win = option.firstChildElement(tag);
            for (QDomElement item = win.firstChildElement(QStringLiteral("Ww"));
                 !item.isNull(); item = item.nextSiblingElement(QStringLiteral("Ww")))
                target.watchExpressions << childText(item, QStringLiteral("ItemText"));
        }
        for (const QString& tag : { QStringLiteral("MemoryWindow1"), QStringLiteral("MemoryWindow2") }) {
            const QDomElement win = option.firstChildElement(tag);
            for (QDomElement item = win.firstChildElement(QStringLiteral("Mm"));
                 !item.isNull(); item = item.nextSiblingElement(QStringLiteral("Mm")))
                target.memoryExpressions << childText(item, QStringLiteral("ItemText"));
        }

        // 断点：<Breakpoint><Bp><FileName>..</FileName><LineNumber>..</LineNumber></Bp>...
        const QDomElement bps = option.firstChildElement(QStringLiteral("Breakpoint"));
        for (QDomElement bp = bps.firstChildElement(QStringLiteral("Bp"));
             !bp.isNull(); bp = bp.nextSiblingElement(QStringLiteral("Bp"))) {
            UvoptxTarget::Breakpoint point;
            point.file = childText(bp, QStringLiteral("FileName"));
            point.line = childText(bp, QStringLiteral("LineNumber")).toInt();
            if (!point.file.isEmpty() && point.line > 0)
                target.breakpoints.append(point);
        }

        out.targets.append(target);
    }

    out.valid = true;
    return true;
}

} // namespace UvoptxReader
