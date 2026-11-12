#include "core/UvprojModel.h"

#include <QFileInfo>
#include <QRegularExpression>

FileKind fileKindFromPath(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();

    if (suffix == QLatin1String("c"))
        return FileKind::CSource;
    if (suffix == QLatin1String("cpp") || suffix == QLatin1String("cxx")
        || suffix == QLatin1String("cc") || suffix == QLatin1String("c++"))
        return FileKind::CppSource;
    if (suffix == QLatin1String("s") || suffix == QLatin1String("asm")
        || suffix == QLatin1String("sct") || suffix == QLatin1String("inc"))
        return FileKind::Assembly;
    if (suffix == QLatin1String("o") || suffix == QLatin1String("obj"))
        return FileKind::Object;
    if (suffix == QLatin1String("lib") || suffix == QLatin1String("a")
        || suffix == QLatin1String("ar"))
        return FileKind::Library;

    return FileKind::Other;
}

FileKind fileKindFromType(int fileType)
{
    switch (fileType) {
    case 1:  return FileKind::CSource;
    case 2:  return FileKind::Assembly;
    case 3:  return FileKind::Object;
    case 5:  return FileKind::CppSource;
    case 7:  return FileKind::Library;
    default: return FileKind::Other;
    }
}

QString fileKindName(FileKind kind)
{
    switch (kind) {
    case FileKind::CSource:   return QStringLiteral("C Source");
    case FileKind::CppSource: return QStringLiteral("C++ Source");
    case FileKind::Assembly:  return QStringLiteral("Assembly");
    case FileKind::Object:    return QStringLiteral("Object");
    case FileKind::Library:   return QStringLiteral("Library");
    default:                  return QStringLiteral("File");
    }
}

QStringList Target::allSourceFiles() const
{
    QStringList out;
    for (const ProjGroup& g : groups) {
        for (const ProjFile& f : g.files) {
            if (f.excluded)
                continue;
            const FileKind k = f.kind();
            if (k == FileKind::CSource || k == FileKind::CppSource || k == FileKind::Assembly)
                out.append(f.path);
        }
    }
    return out;
}

QString Target::displayCpu() const
{
    static const QRegularExpression re(QStringLiteral("CPUTYPE\\(\"([^\"]+)\"\\)"));
    const QRegularExpressionMatch m = re.match(cpu);
    if (m.hasMatch())
        return m.captured(1);
    return QStringLiteral("Cortex-M3");
}

QString UvProject::directory() const
{
    return QFileInfo(filePath).absolutePath();
}

QString UvProject::baseName() const
{
    return QFileInfo(filePath).completeBaseName();
}

Target* UvProject::target(int index)
{
    if (index < 0 || index >= targets.size())
        return nullptr;
    return &targets[index];
}

const Target* UvProject::target(int index) const
{
    if (index < 0 || index >= targets.size())
        return nullptr;
    return &targets[index];
}

QStringList splitOptionList(const QString& raw)
{
    QStringList out;
    const QStringList chunks = raw.split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                                         Qt::SkipEmptyParts);
    for (const QString& c : chunks) {
        const QString t = c.trimmed();
        if (!t.isEmpty())
            out.append(t);
    }
    return out;
}
