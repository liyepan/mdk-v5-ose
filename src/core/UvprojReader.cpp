#include "core/UvprojReader.h"

#include <QDir>
#include <QDomDocument>
#include <QDomElement>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

namespace {

QString childText(const QDomElement& parent, const QString& tag, const QString& def = QString())
{
    const QDomElement e = parent.firstChildElement(tag);
    if (e.isNull())
        return def;
    const QString t = e.text().trimmed();
    return t.isEmpty() ? def : t;
}

int childInt(const QDomElement& parent, const QString& tag, int def)
{
    bool ok = false;
    const int v = childText(parent, tag).toInt(&ok);
    return ok ? v : def;
}

bool childFlag(const QDomElement& parent, const QString& tag, bool def)
{
    const QString t = childText(parent, tag);
    if (t.isEmpty())
        return def;
    return t == QLatin1String("1") || t.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
}

// <BeforeMake><RunUserProg1>1</RunUserProg1><UserProg1Name>...</UserProg1Name>
//              <nStopB1X>0</nStopB1X> ...
QList<UserProgram> readUserPrograms(const QDomElement& el, const QString& stopTagPrefix)
{
    QList<UserProgram> out;
    if (el.isNull())
        return out;

    for (int i = 1; i <= 2; ++i) {
        const QString runTag = QStringLiteral("RunUserProg%1").arg(i);
        const QString nameTag = QStringLiteral("UserProg%1Name").arg(i);
        const QString stopTag = QStringLiteral("%1%2X").arg(stopTagPrefix).arg(i);

        const QString cmd = childText(el, nameTag);
        if (cmd.isEmpty())
            continue;

        UserProgram p;
        p.command = cmd;
        p.enabled = childFlag(el, runTag, false);
        p.stopOnExitCode = childText(el, stopTag) != QLatin1String("0");
        out.append(p);
    }
    return out;
}

MemoryRegion readMemoryRegion(const QDomElement& el)
{
    MemoryRegion r;
    if (el.isNull())
        return r;

    bool okStart = false;
    bool okSize = false;
    r.start = childText(el, QStringLiteral("StartAddress")).toUInt(&okStart, 0);
    r.size = childText(el, QStringLiteral("Size")).toUInt(&okSize, 0);
    if (!okStart || !okSize || r.size == 0)
        return MemoryRegion();
    return r;
}

// <Cpu>IRAM(0x20000000,0x00010000) IROM(0x08000000,0x00080000) CPUTYPE("Cortex-M3")</Cpu>
MemoryRegion regionFromCpu(const QString& cpu, const QString& tag)
{
    const QRegularExpression re(QStringLiteral("%1\\s*\\(\\s*([^,]+?)\\s*,\\s*([^)]+?)\\s*\\)")
                                    .arg(tag));
    const QRegularExpressionMatch m = re.match(cpu);
    if (!m.hasMatch())
        return MemoryRegion();

    MemoryRegion r;
    r.start = m.captured(1).toUInt(nullptr, 0);
    r.size = m.captured(2).toUInt(nullptr, 0);
    return r;
}

QDomElement ensureChild(QDomDocument& doc, QDomElement parent, const QString& tag)
{
    QDomElement e = parent.firstChildElement(tag);
    if (e.isNull()) {
        e = doc.createElement(tag);
        parent.appendChild(e);
    }
    return e;
}

void setChildText(QDomDocument& doc, const QDomElement& parent, const QString& tag,
                  const QString& value)
{
    if (parent.isNull())
        return;
    QDomElement e = ensureChild(doc, parent, tag);
    if (e.text() == value)
        return;
    while (!e.firstChild().isNull())
        e.removeChild(e.firstChild());
    e.appendChild(doc.createTextNode(value));
}

void setChildInt(QDomDocument& doc, const QDomElement& parent, const QString& tag, int value)
{
    setChildText(doc, parent, tag, QString::number(value));
}

void setChildFlag(QDomDocument& doc, const QDomElement& parent, const QString& tag, bool value)
{
    setChildText(doc, parent, tag, value ? QStringLiteral("1") : QStringLiteral("0"));
}

} // namespace

namespace Uvproj {

bool load(const QString& path, UvProject& out, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法打开工程文件: ") + file.errorString();
        return false;
    }

    QByteArray raw = file.readAll();
    file.close();

    // Keil 有时会写出 UTF-8 BOM，QDomDocument 遇到会直接解析失败
    if (raw.startsWith("\xEF\xBB\xBF"))
        raw.remove(0, 3);

    QDomDocument doc;
    QString parseError;
    int line = 0;
    int column = 0;
    if (!doc.setContent(raw, &parseError, &line, &column)) {
        if (error)
            *error = QStringLiteral("工程文件解析失败 (%1:%2): %3")
                         .arg(line).arg(column).arg(parseError);
        return false;
    }

    const QDomElement root = doc.documentElement();
    if (root.tagName() != QLatin1String("Project")) {
        if (error)
            *error = QStringLiteral("不是有效的 uVision 工程：根节点为 <%1>").arg(root.tagName());
        return false;
    }

    UvProject project;
    project.filePath = QFileInfo(path).absoluteFilePath();
    project.schemaVersion = childText(root, QStringLiteral("SchemaVersion"));

    const QDomElement targetsEl = root.firstChildElement(QStringLiteral("Targets"));
    for (QDomElement tEl = targetsEl.firstChildElement(QStringLiteral("Target"));
         !tEl.isNull(); tEl = tEl.nextSiblingElement(QStringLiteral("Target"))) {

        Target t;
        t.name = childText(tEl, QStringLiteral("TargetName"), QStringLiteral("Target 1"));
        t.toolsetName = childText(tEl, QStringLiteral("ToolsetName"));
        t.compilerVersion = childText(tEl, QStringLiteral("pCCUsed"));
        t.ac6 = childFlag(tEl, QStringLiteral("uAC6"), false);

        const QDomElement targetOption = tEl.firstChildElement(QStringLiteral("TargetOption"));
        const QDomElement common = targetOption.firstChildElement(QStringLiteral("TargetCommonOption"));
        const QDomElement armAds = targetOption.firstChildElement(QStringLiteral("TargetArmAds"));
        const QDomElement misc = armAds.firstChildElement(QStringLiteral("ArmAdsMisc"));
        const QDomElement cads = armAds.firstChildElement(QStringLiteral("Cads"));
        const QDomElement aads = armAds.firstChildElement(QStringLiteral("Aads"));
        const QDomElement ldads = armAds.firstChildElement(QStringLiteral("LDads"));

        // Target 页
        t.device = childText(common, QStringLiteral("Device"));
        t.vendor = childText(common, QStringLiteral("Vendor"));
        t.packId = childText(common, QStringLiteral("PackID"));
        t.cpu = childText(common, QStringLiteral("Cpu"));

        // Output 页
        t.outputDirectory = childText(common, QStringLiteral("OutputDirectory"), QStringLiteral(".\\Objects\\"));
        t.outputName = childText(common, QStringLiteral("OutputName"));
        t.listingPath = childText(common, QStringLiteral("ListingPath"), QStringLiteral(".\\Listings\\"));
        t.createExecutable = childFlag(common, QStringLiteral("CreateExecutable"), true);
        t.createLib = childFlag(common, QStringLiteral("CreateLib"), false);
        t.createHexFile = childFlag(common, QStringLiteral("CreateHexFile"), false);
        t.debugInformation = childFlag(common, QStringLiteral("DebugInformation"), true);
        t.browseInformation = childFlag(common, QStringLiteral("BrowseInformation"), true);

        // C/C++ 页
        // uvprojx 里 Optim 是 1..4，对应 -O0..-O3；模型里统一存 -O 等级 0..3
        const int rawOptim = childInt(cads, QStringLiteral("Optim"), 2);
        t.optimization = qBound(0, rawOptim - 1, 3);
        t.optimizeForTime = childFlag(cads, QStringLiteral("oTime"), false);
        t.oneElfSectionPerFunction = childFlag(cads, QStringLiteral("OneElfS"), false);
        t.splitLoadStore = childFlag(cads, QStringLiteral("SplitLS"), false);
        t.interwork = childFlag(cads, QStringLiteral("interw"), true);
        t.strict = childFlag(cads, QStringLiteral("Strict"), false);
        t.enumIsInt = childFlag(cads, QStringLiteral("EnumInt"), false);
        t.plainChar = childFlag(cads, QStringLiteral("PlainCh"), false);
        t.c99 = childFlag(cads, QStringLiteral("uC99"), false);
        t.gnuExtensions = childFlag(cads, QStringLiteral("uGnu"), false);
        t.cpp11 = childFlag(cads, QStringLiteral("uCpp11"), false);
        t.warningLevel = childInt(cads, QStringLiteral("wLevel"), 2);
        t.noWarnings = (t.warningLevel == 0);

        const QDomElement cVarious = cads.firstChildElement(QStringLiteral("VariousControls"));
        t.defines = childText(cVarious, QStringLiteral("Define"));
        t.undefines = childText(cVarious, QStringLiteral("Undefine"));
        t.includePaths = childText(cVarious, QStringLiteral("IncludePath"));
        t.miscControls = childText(cVarious, QStringLiteral("MiscControls"));

        // Asm 页
        const QDomElement aVarious = aads.firstChildElement(QStringLiteral("VariousControls"));
        t.asmDefines = childText(aVarious, QStringLiteral("Define"));
        t.asmIncludePaths = childText(aVarious, QStringLiteral("IncludePath"));
        t.asmMisc = childText(aVarious, QStringLiteral("MiscControls"));

        // Linker 页
        t.useMemoryLayout = childFlag(ldads, QStringLiteral("umfTarg"), true);
        t.scatterFile = childText(ldads, QStringLiteral("ScatterFile"));
        t.linkerMisc = childText(ldads, QStringLiteral("Misc"));
        t.textAddressRange = childText(ldads, QStringLiteral("TextAddressRange"));
        t.dataAddressRange = childText(ldads, QStringLiteral("DataAddressRange"));
        t.noStartLib = childFlag(ldads, QStringLiteral("noStLib"), false);

        // 其它
        t.useMicrolib = childFlag(misc, QStringLiteral("useUlib"), false);

        // 片上存储：优先用 OnChipMemories（Target 对话框里填的），
        // 没有就退回 <Cpu> 里的 IROM()/IRAM()
        const QDomElement ocm = misc.firstChildElement(QStringLiteral("OnChipMemories"));
        t.irom1 = readMemoryRegion(ocm.firstChildElement(QStringLiteral("IROM")));
        t.iram1 = readMemoryRegion(ocm.firstChildElement(QStringLiteral("IRAM")));
        if (!t.irom1.isValid())
            t.irom1 = regionFromCpu(t.cpu, QStringLiteral("IROM"));
        if (!t.iram1.isValid())
            t.iram1 = regionFromCpu(t.cpu, QStringLiteral("IRAM"));

        // 构建前后用户命令（uVision 的 Before/After 选项卡）
        t.beforeCompile = readUserPrograms(
            common.firstChildElement(QStringLiteral("BeforeCompile")), QStringLiteral("nStopU"));
        t.beforeMake = readUserPrograms(
            common.firstChildElement(QStringLiteral("BeforeMake")), QStringLiteral("nStopB"));
        t.afterMake = readUserPrograms(
            common.firstChildElement(QStringLiteral("AfterMake")), QStringLiteral("nStopA"));

        // Groups
        const QDomElement groupsEl = tEl.firstChildElement(QStringLiteral("Groups"));
        for (QDomElement gEl = groupsEl.firstChildElement(QStringLiteral("Group"));
             !gEl.isNull(); gEl = gEl.nextSiblingElement(QStringLiteral("Group"))) {

            ProjGroup group;
            group.name = childText(gEl, QStringLiteral("GroupName"), QStringLiteral("Group"));

            const QDomElement gOption = gEl.firstChildElement(QStringLiteral("GroupOption"));
            group.excluded =
                !childFlag(gOption.firstChildElement(QStringLiteral("CommonProperty")),
                           QStringLiteral("IncludeInBuild"), true);

            // 组级编译选项（GroupOption/GroupArmAds/Cads/VariousControls），
            // 优先级低于文件级、高于 Target 级
            const QDomElement gAds = gOption.firstChildElement(QStringLiteral("GroupArmAds"));
            const QDomElement gVarious =
                gAds.firstChildElement(QStringLiteral("Cads"))
                    .firstChildElement(QStringLiteral("VariousControls"));
            if (!gVarious.isNull()) {
                group.defines = childText(gVarious, QStringLiteral("Define"));
                group.includePaths = childText(gVarious, QStringLiteral("IncludePath"));
                group.misc = childText(gVarious, QStringLiteral("MiscControls"));
                group.hasOwnOptions = !group.defines.isEmpty() || !group.includePaths.isEmpty()
                                      || !group.misc.isEmpty();
            }

            const bool groupInBuild = !group.excluded;

            const QDomElement filesEl = gEl.firstChildElement(QStringLiteral("Files"));
            for (QDomElement fEl = filesEl.firstChildElement(QStringLiteral("File"));
                 !fEl.isNull(); fEl = fEl.nextSiblingElement(QStringLiteral("File"))) {

                ProjFile f;
                f.groupName = group.name;
                f.name = childText(fEl, QStringLiteral("FileName"));
                f.path = childText(fEl, QStringLiteral("FilePath"));
                f.fileType = childInt(fEl, QStringLiteral("FileType"), 0);
                if (f.name.isEmpty())
                    f.name = QFileInfo(QString(f.path).replace(QLatin1Char('\\'), QLatin1Char('/'))).fileName();

                const QDomElement fOption = fEl.firstChildElement(QStringLiteral("FileOption"));
                const QDomElement fCommon = fOption.firstChildElement(QStringLiteral("CommonProperty"));
                const bool fileInBuild = childFlag(fCommon, QStringLiteral("IncludeInBuild"), true);
                f.excluded = !groupInBuild || !fileInBuild;

                const QDomElement fAds = fOption.firstChildElement(QStringLiteral("FileArmAds"));
                const QDomElement fCads = fAds.firstChildElement(QStringLiteral("Cads"));
                const QDomElement fVarious = fCads.firstChildElement(QStringLiteral("VariousControls"));
                if (!fVarious.isNull()) {
                    f.defines = childText(fVarious, QStringLiteral("Define"));
                    f.includePaths = childText(fVarious, QStringLiteral("IncludePath"));
                    f.misc = childText(fVarious, QStringLiteral("MiscControls"));
                    f.hasOwnOptions = !f.defines.isEmpty() || !f.includePaths.isEmpty()
                                      || !f.misc.isEmpty();
                }

                group.files.append(f);
            }

            t.groups.append(group);
        }

        project.targets.append(t);
    }

    if (project.targets.isEmpty()) {
        if (error)
            *error = QStringLiteral("工程里没有找到任何 Target");
        return false;
    }

    out = project;
    return true;
}

bool saveTarget(const QString& path, const Target& target, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取工程文件: ") + file.errorString();
        return false;
    }

    QDomDocument doc;
    QString parseError;
    int line = 0;
    int column = 0;
    if (!doc.setContent(&file, &parseError, &line, &column)) {
        if (error)
            *error = QStringLiteral("解析失败 (%1:%2): %3").arg(line).arg(column).arg(parseError);
        return false;
    }
    file.close();

    const QDomElement root = doc.documentElement();
    const QDomElement targetsEl = root.firstChildElement(QStringLiteral("Targets"));

    QDomElement wanted;
    for (QDomElement tEl = targetsEl.firstChildElement(QStringLiteral("Target"));
         !tEl.isNull(); tEl = tEl.nextSiblingElement(QStringLiteral("Target"))) {
        if (childText(tEl, QStringLiteral("TargetName")) == target.name) {
            wanted = tEl;
            break;
        }
    }

    if (wanted.isNull()) {
        if (error)
            *error = QStringLiteral("找不到 Target: ") + target.name;
        return false;
    }

    const QDomElement targetOption = ensureChild(doc, wanted, QStringLiteral("TargetOption"));
    const QDomElement common = ensureChild(doc, targetOption, QStringLiteral("TargetCommonOption"));
    const QDomElement armAds = ensureChild(doc, targetOption, QStringLiteral("TargetArmAds"));
    const QDomElement misc = ensureChild(doc, armAds, QStringLiteral("ArmAdsMisc"));
    const QDomElement cads = ensureChild(doc, armAds, QStringLiteral("Cads"));
    const QDomElement cVarious = ensureChild(doc, cads, QStringLiteral("VariousControls"));
    const QDomElement ldads = ensureChild(doc, armAds, QStringLiteral("LDads"));

    setChildText(doc, common, QStringLiteral("OutputDirectory"), target.outputDirectory);
    setChildText(doc, common, QStringLiteral("OutputName"), target.outputName);
    setChildText(doc, common, QStringLiteral("ListingPath"), target.listingPath);
    setChildFlag(doc, common, QStringLiteral("CreateExecutable"), target.createExecutable);
    setChildFlag(doc, common, QStringLiteral("CreateLib"), target.createLib);
    setChildFlag(doc, common, QStringLiteral("CreateHexFile"), target.createHexFile);
    setChildFlag(doc, common, QStringLiteral("DebugInformation"), target.debugInformation);
    setChildFlag(doc, common, QStringLiteral("BrowseInformation"), target.browseInformation);

    setChildInt(doc, cads, QStringLiteral("Optim"), target.optimization + 1);
    setChildFlag(doc, cads, QStringLiteral("oTime"), target.optimizeForTime);
    setChildFlag(doc, cads, QStringLiteral("uC99"), target.c99);
    setChildFlag(doc, cads, QStringLiteral("OneElfS"), target.oneElfSectionPerFunction);
    setChildFlag(doc, cads, QStringLiteral("Strict"), target.strict);
    setChildFlag(doc, cads, QStringLiteral("EnumInt"), target.enumIsInt);
    setChildFlag(doc, cads, QStringLiteral("PlainCh"), target.plainChar);
    setChildInt(doc, cads, QStringLiteral("wLevel"), target.warningLevel);
    setChildText(doc, cVarious, QStringLiteral("Define"), target.defines);
    setChildText(doc, cVarious, QStringLiteral("Undefine"), target.undefines);
    setChildText(doc, cVarious, QStringLiteral("IncludePath"), target.includePaths);
    setChildText(doc, cVarious, QStringLiteral("MiscControls"), target.miscControls);

    setChildFlag(doc, ldads, QStringLiteral("umfTarg"), target.useMemoryLayout);
    setChildText(doc, ldads, QStringLiteral("ScatterFile"), target.scatterFile);
    setChildText(doc, ldads, QStringLiteral("Misc"), target.linkerMisc);
    setChildFlag(doc, misc, QStringLiteral("useUlib"), target.useMicrolib);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("无法写入工程文件: ") + file.errorString();
        return false;
    }

    QTextStream ts(&file);
    ts.setCodec("UTF-8");
    ts << doc.toString(2);
    ts.flush();
    file.close();
    return true;
}

bool saveGroups(const QString& path, const Target& target, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取工程文件: ") + file.errorString();
        return false;
    }

    QDomDocument doc;
    QString parseError;
    int line = 0;
    int column = 0;
    if (!doc.setContent(&file, &parseError, &line, &column)) {
        if (error)
            *error = QStringLiteral("解析失败 (%1:%2): %3").arg(line).arg(column).arg(parseError);
        return false;
    }
    file.close();

    QDomElement root = doc.documentElement();
    QDomElement targetsEl = root.firstChildElement(QStringLiteral("Targets"));

    QDomElement wanted;
    for (QDomElement tEl = targetsEl.firstChildElement(QStringLiteral("Target"));
         !tEl.isNull(); tEl = tEl.nextSiblingElement(QStringLiteral("Target"))) {
        if (childText(tEl, QStringLiteral("TargetName")) == target.name) {
            wanted = tEl;
            break;
        }
    }

    if (wanted.isNull()) {
        if (error)
            *error = QStringLiteral("找不到 Target: ") + target.name;
        return false;
    }

    // 先把旧的分组记下来，等会儿按名字把组级/文件级的选项捞回来
    QHash<QString, QDomElement> oldGroups;
    QHash<QString, QDomElement> oldFiles;
    const QDomElement oldGroupsEl = wanted.firstChildElement(QStringLiteral("Groups"));
    if (!oldGroupsEl.isNull()) {
        for (QDomElement gEl = oldGroupsEl.firstChildElement(QStringLiteral("Group"));
             !gEl.isNull(); gEl = gEl.nextSiblingElement(QStringLiteral("Group"))) {
            const QString name = childText(gEl, QStringLiteral("GroupName"));
            oldGroups.insert(name, gEl);
            const QDomElement filesEl = gEl.firstChildElement(QStringLiteral("Files"));
            for (QDomElement fEl = filesEl.firstChildElement(QStringLiteral("File"));
                 !fEl.isNull(); fEl = fEl.nextSiblingElement(QStringLiteral("File"))) {
                oldFiles.insert(childText(fEl, QStringLiteral("FilePath")), fEl);
            }
        }
        wanted.removeChild(oldGroupsEl);
    }

    QDomElement groupsEl = doc.createElement(QStringLiteral("Groups"));
    wanted.appendChild(groupsEl);

    auto appendText = [&doc](QDomElement parent, const QString& tag, const QString& value) {
        QDomElement el = doc.createElement(tag);
        el.appendChild(doc.createTextNode(value));
        parent.appendChild(el);
        return el;
    };

    for (const ProjGroup& group : target.groups) {
        QDomElement gEl = doc.createElement(QStringLiteral("Group"));
        appendText(gEl, QStringLiteral("GroupName"), group.name);

        // 组级选项（GroupOption 之类）从旧节点搬过来
        const auto oldGroup = oldGroups.constFind(group.name);
        if (oldGroup != oldGroups.constEnd()) {
            for (QDomNode node = oldGroup->firstChild(); !node.isNull(); node = node.nextSibling()) {
                if (!node.isElement())
                    continue;
                const QString tag = node.toElement().tagName();
                if (tag == QLatin1String("GroupName") || tag == QLatin1String("Files"))
                    continue;
                gEl.appendChild(node.cloneNode(true));
            }
        }

        QDomElement filesEl = doc.createElement(QStringLiteral("Files"));
        gEl.appendChild(filesEl);

        for (const ProjFile& pf : group.files) {
            QDomElement fEl = doc.createElement(QStringLiteral("File"));
            appendText(fEl, QStringLiteral("FileName"), pf.name);
            appendText(fEl, QStringLiteral("FileType"), QString::number(pf.fileType));
            appendText(fEl, QStringLiteral("FilePath"), pf.path);

            const auto oldFile = oldFiles.constFind(pf.path);
            if (oldFile != oldFiles.constEnd()) {
                for (QDomNode node = oldFile->firstChild(); !node.isNull(); node = node.nextSibling()) {
                    if (!node.isElement())
                        continue;
                    const QString tag = node.toElement().tagName();
                    if (tag == QLatin1String("FileName") || tag == QLatin1String("FileType")
                        || tag == QLatin1String("FilePath"))
                        continue;
                    fEl.appendChild(node.cloneNode(true));
                }
            }

            filesEl.appendChild(fEl);
        }

        groupsEl.appendChild(gEl);
    }

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("无法写入工程文件: ") + file.errorString();
        return false;
    }

    QTextStream ts(&file);
    ts.setCodec("UTF-8");
    ts << doc.toString(2);
    ts.flush();
    file.close();
    return true;
}

QString resolvePath(const QString& projectDir, const QString& relative)
{
    QString p = relative;
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));

#ifdef Q_OS_WIN
    const QString native = QDir::fromNativeSeparators(projectDir + QLatin1Char('/') + p);
    return QDir::cleanPath(native);
#else
    return QDir::cleanPath(projectDir + QLatin1Char('/') + p);
#endif
}

} // namespace Uvproj
