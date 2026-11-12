#include "core/BuildEngine.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTextStream>

namespace {

QString native(const QString& p)
{
#ifdef Q_OS_WIN
    return QDir::toNativeSeparators(p);
#else
    return p;
#endif
}

QString toAbsolute(const QString& projectDir, const QString& raw)
{
    QString p = raw.trimmed();
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (p.isEmpty())
        return QString();
    if (QFileInfo(p).isAbsolute())
        return QDir::cleanPath(p);
    return QDir::cleanPath(projectDir + QLatin1Char('/') + p);
}

QStringList splitArgsLoose(const QString& raw)
{
    QStringList out;
    const QStringList parts = raw.split(QRegularExpression(QStringLiteral("\\s+")),
                                        Qt::SkipEmptyParts);
    for (const QString& p : parts)
        out << p;
    return out;
}

QStringList splitCommandLine(const QString& command)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    return QProcess::splitCommand(command);
#else
    QStringList out;
    QString current;
    bool quoted = false;
    for (const QChar c : command) {
        if (c == QLatin1Char('"')) {
            quoted = !quoted;
            continue;
        }
        if (!quoted && c.isSpace()) {
            if (!current.isEmpty()) {
                out << current;
                current.clear();
            }
            continue;
        }
        current.append(c);
    }
    if (!current.isEmpty())
        out << current;
    return out;
#endif
}

QString formatElapsed(qint64 ms)
{
    const qint64 total = ms / 1000;
    const qint64 h = total / 3600;
    const qint64 m = (total % 3600) / 60;
    const qint64 s = total % 60;
    return QString::asprintf("%02lld:%02lld:%02lld", h, m, s);
}

// QProcess 在 Unix 上按【父进程】的 PATH 查找程序，不看我们塞给子进程的 PATH，
// 所以像 AfterMake 里的 "fromelf.exe ..." 这种裸命令必须自己先解析成绝对路径。
QString resolveProgram(const QString& program, const QString& binDir)
{
    if (program.isEmpty())
        return program;
    if (QFileInfo(program).isAbsolute() || program.contains(QLatin1Char('/'))
        || program.contains(QLatin1Char('\\'))) {
        return program;
    }

    QStringList dirs;
    if (!binDir.isEmpty())
        dirs << binDir;
    dirs << QProcessEnvironment::systemEnvironment()
                .value(QStringLiteral("PATH"))
                .split(QDir::listSeparator(), Qt::SkipEmptyParts);

    for (const QString& dir : dirs) {
        const QString candidate = QDir(dir).absoluteFilePath(program);
        if (QFileInfo(candidate).isFile() && QFileInfo(candidate).isExecutable())
            return candidate;
#ifdef Q_OS_WIN
        const QString withExe = candidate + QStringLiteral(".exe");
        if (QFileInfo(withExe).isFile())
            return withExe;
#endif
    }

    return program;
}

QString safeFileName(const QString& name)
{
    QString out = name;
    static const QRegularExpression bad(QStringLiteral("[\\\\/:*?\"<>|\\s]+"));
    out.replace(bad, QStringLiteral("_"));
    return out.isEmpty() ? QStringLiteral("target") : out;
}

QByteArray signatureOf(const QStringList& args)
{
    return QCryptographicHash::hash(args.join(QLatin1Char('\x1f')).toUtf8(),
                                    QCryptographicHash::Sha1)
        .toHex();
}

} // namespace

BuildEngine::BuildEngine(QObject* parent)
    : QObject(parent)
{
}

BuildEngine::~BuildEngine()
{
    if (m_proc) {
        m_proc->disconnect(this);
        m_proc->kill();
        m_proc->waitForFinished(1000);
        delete m_proc;
        m_proc = nullptr;
    }
}

void BuildEngine::setToolchain(const Toolchain& tc)
{
    m_toolchain = tc;
}

void BuildEngine::setProject(const UvProject& project, int targetIndex)
{
    m_project = project;
    m_targetIndex = targetIndex;
}

const Target* BuildEngine::currentTarget() const
{
    return m_project.target(m_targetIndex);
}

QString BuildEngine::projectDir() const
{
    return m_project.directory();
}

QString BuildEngine::outputDir() const
{
    const Target* t = currentTarget();
    if (!t)
        return projectDir();
    const QString raw = t->outputDirectory.isEmpty() ? QStringLiteral(".\\Objects\\")
                                                     : t->outputDirectory;
    return toAbsolute(projectDir(), raw);
}

QString BuildEngine::axfPath() const
{
    const Target* t = currentTarget();
    const QString name = t ? t->artifactName() : QStringLiteral("Project");
    return outputDir() + QLatin1Char('/') + name + QStringLiteral(".axf");
}

QString BuildEngine::libPath() const
{
    const Target* t = currentTarget();
    const QString name = t ? t->artifactName() : QStringLiteral("Project");
    return outputDir() + QLatin1Char('/') + name + QStringLiteral(".lib");
}

QString BuildEngine::cpuName() const
{
    const Target* t = currentTarget();
    return t ? t->displayCpu() : QStringLiteral("Cortex-M3");
}

// ---------------------------------------------------------------- 选项合并

QStringList BuildEngine::targetIncludeDirs() const
{
    const Target* t = currentTarget();
    if (!t)
        return QStringList();

    QStringList out;
    for (const QString& inc : splitOptionList(t->includePaths))
        out << toAbsolute(projectDir(), inc);
    return out;
}

QStringList BuildEngine::targetAsmIncludeDirs() const
{
    const Target* t = currentTarget();
    if (!t)
        return QStringList();

    QStringList out;
    for (const QString& inc : splitOptionList(t->asmIncludePaths))
        out << toAbsolute(projectDir(), inc);
    return out;
}

QStringList BuildEngine::mergeIncludeDirs(const QStringList& base, const ProjGroup& group,
                                          const ProjFile& file) const
{
    QStringList out = base;

    if (group.hasOwnOptions) {
        for (const QString& inc : splitOptionList(group.includePaths))
            out << toAbsolute(projectDir(), inc);
    }
    if (file.hasOwnOptions) {
        for (const QString& inc : splitOptionList(file.includePaths))
            out << toAbsolute(projectDir(), inc);
    }
    out.removeDuplicates();
    return out;
}

QStringList BuildEngine::mergeDefines(const ProjGroup& group, const ProjFile& file) const
{
    const Target* t = currentTarget();
    QStringList out = t ? splitOptionList(t->defines) : QStringList();
    if (group.hasOwnOptions)
        out += splitOptionList(group.defines);
    if (file.hasOwnOptions)
        out += splitOptionList(file.defines);
    out.removeDuplicates();
    return out;
}

// ---------------------------------------------------------------- 参数构造

QStringList BuildEngine::compileArgs(const ProjGroup& group, const ProjFile& file,
                                     const QString& sourcePath, const QString& objectPath) const
{
    const Target* t = currentTarget();
    QStringList a;

    const FileKind kind = file.kind();
    const bool isC = (kind == FileKind::CSource);

    if (m_toolchain.family == QLatin1String("AC6")) {
        a << QStringLiteral("--target=arm-arm-none-eabi");
        QString cpu = cpuName().toLower();
        cpu.replace(QLatin1Char('+'), QStringLiteral("plus"));
        a << (QStringLiteral("-mcpu=") + cpu);
        a << QStringLiteral("-mthumb");
        if (t && t->debugInformation)
            a << QStringLiteral("-g");
        a << (QStringLiteral("-O") + QString::number(t ? t->optimization : 1));
        a << QStringLiteral("-c");
    } else {
        a << QStringLiteral("--cpu") << cpuName();
        if (isC) {
            if (t && t->c99)
                a << QStringLiteral("--c99");
            else
                a << QStringLiteral("--c90");
        }
        if (t && t->gnuExtensions)
            a << QStringLiteral("--gnu");
        if (t && t->debugInformation)
            a << QStringLiteral("-g");
        a << (QStringLiteral("-O") + QString::number(t ? t->optimization : 1));
        if (t && t->interwork)
            a << QStringLiteral("--apcs=interwork");
        if (t && t->oneElfSectionPerFunction)
            a << QStringLiteral("--split_sections");
        if (t && t->splitLoadStore)
            a << QStringLiteral("--split_ldm");
        if (t && t->strict)
            a << QStringLiteral("--strict");
        if (t && t->enumIsInt)
            a << QStringLiteral("--enum_is_int");
        if (t && t->plainChar)
            a << QStringLiteral("--plain_char");
        if (t && t->noWarnings)
            a << QStringLiteral("-W");
        if (t && t->optimizeForTime)
            a << QStringLiteral("-Otime");
        a << QStringLiteral("-c");
    }

    for (const QString& d : mergeDefines(group, file))
        a << (QStringLiteral("-D") + d);

    // C / C++ 用 Target 的 C/C++ 页 include，汇编用 Asm 页的（两者在 uVision 里是分开的）
    for (const QString& inc : mergeIncludeDirs(targetIncludeDirs(), group, file))
        a << (QStringLiteral("-I") + native(inc));

    if (t && !t->miscControls.trimmed().isEmpty())
        a << splitArgsLoose(t->miscControls);
    if (group.hasOwnOptions && !group.misc.trimmed().isEmpty())
        a << splitArgsLoose(group.misc);
    if (file.hasOwnOptions && !file.misc.trimmed().isEmpty())
        a << splitArgsLoose(file.misc);

    a << QStringLiteral("-o") << native(objectPath);
    a << native(sourcePath);
    return a;
}

QStringList BuildEngine::assembleArgs(const ProjGroup& group, const ProjFile& file,
                                      const QString& sourcePath, const QString& objectPath) const
{
    const Target* t = currentTarget();
    QStringList a;

    a << QStringLiteral("--cpu") << cpuName();
    if (t && t->debugInformation)
        a << QStringLiteral("-g");

    QStringList defines = t ? splitOptionList(t->asmDefines) : QStringList();
    if (group.hasOwnOptions)
        defines += splitOptionList(group.defines);
    if (file.hasOwnOptions)
        defines += splitOptionList(file.defines);

    for (const QString& d : defines) {
        const int eq = d.indexOf(QLatin1Char('='));
        if (eq > 0)
            a << QStringLiteral("--pd") << (d.left(eq) + QStringLiteral(" SETA ") + d.mid(eq + 1));
        else
            a << QStringLiteral("--pd") << (d + QStringLiteral(" SETA 1"));
    }

    for (const QString& inc : mergeIncludeDirs(targetAsmIncludeDirs(), group, file))
        a << (QStringLiteral("-I") + native(inc));

    if (t && !t->asmMisc.trimmed().isEmpty())
        a << splitArgsLoose(t->asmMisc);
    if (group.hasOwnOptions && !group.misc.trimmed().isEmpty())
        a << splitArgsLoose(group.misc);

    a << QStringLiteral("-o") << native(objectPath);
    a << native(sourcePath);
    return a;
}

QStringList BuildEngine::linkArgs(const QStringList& objects) const
{
    const Target* t = currentTarget();
    QStringList a;
    if (!t)
        return a;

    a << QStringLiteral("--cpu") << cpuName();

    if (t->useMemoryLayout || t->scatterFile.isEmpty()) {
        if (!t->textAddressRange.trimmed().isEmpty())
            a << QStringLiteral("--ro-base") << t->textAddressRange.trimmed();
        if (!t->dataAddressRange.trimmed().isEmpty())
            a << QStringLiteral("--rw-base") << t->dataAddressRange.trimmed();
    } else {
        const QString scatter = toAbsolute(projectDir(), t->scatterFile);
        a << (QStringLiteral("--scatter=") + native(scatter));
    }

    if (t->strict)
        a << QStringLiteral("--strict");
    if (t->noStartLib)
        a << QStringLiteral("--no_stdlib");
    if (t->useMicrolib)
        a << QStringLiteral("--library_type=microlib");

    a << QStringLiteral("--info") << QStringLiteral("sizes,totals");
    a << QStringLiteral("--map");
    a << QStringLiteral("-o") << native(axfPath());

    if (!t->linkerMisc.trimmed().isEmpty())
        a << splitArgsLoose(t->linkerMisc);

    a << objects;
    return a;
}

QStringList BuildEngine::libArgs(const QStringList& objects) const
{
    QStringList a;
    a << QStringLiteral("--create") << native(libPath());
    a << objects;
    return a;
}

// ---------------------------------------------------------------- 依赖扫描

QStringList BuildEngine::scanDependencies(const QString& sourcePath,
                                          const QStringList& includeDirs) const
{
    static const QRegularExpression includeRe(
        QStringLiteral("^\\s*#\\s*include\\s*[<\"]([^>\"\\r\\n]+)[>\"]"));

    QStringList result;
    QSet<QString> visited;
    QStringList queue;
    queue << QDir::cleanPath(sourcePath);

    while (!queue.isEmpty() && visited.size() < 2000) {
        const QString current = queue.takeFirst();
        if (visited.contains(current))
            continue;
        visited.insert(current);

        QFile f(current);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;

        QTextStream ts(&f);
        int guard = 0;
        while (!ts.atEnd() && guard++ < 200000) {
            const QString line = ts.readLine();
            const QRegularExpressionMatch m = includeRe.match(line);
            if (!m.hasMatch())
                continue;

            const QString name = m.captured(1).trimmed();
            QString found;

            // 1) 与当前文件同目录
            const QString local = QDir::cleanPath(QFileInfo(current).absolutePath()
                                                  + QLatin1Char('/') + name);
            if (QFileInfo::exists(local)) {
                found = local;
            } else {
                // 2) -I 目录
                for (const QString& dir : includeDirs) {
                    const QString cand = QDir::cleanPath(dir + QLatin1Char('/') + name);
                    if (QFileInfo::exists(cand)) {
                        found = cand;
                        break;
                    }
                }
            }

            if (found.isEmpty() || visited.contains(found))
                continue;

            result << found;
            queue << found;
        }
        f.close();
    }

    return result;
}

bool BuildEngine::isUpToDate(const QString& source, const QString& object,
                             const QByteArray& signature, const QStringList& includeDirs) const
{
    const QFileInfo objectInfo(object);
    if (!objectInfo.exists())
        return false;

    // 编译选项变了（包括切 Target 导致 include/define 变化）就必须重编
    if (m_signatures.value(object) != QString::fromLatin1(signature))
        return false;

    const QDateTime objectTime = objectInfo.lastModified();
    if (QFileInfo(source).lastModified() > objectTime)
        return false;

    const QStringList deps = scanDependencies(source, includeDirs);
    for (const QString& dep : deps) {
        if (QFileInfo(dep).lastModified() > objectTime)
            return false;
    }
    return true;
}

// ---------------------------------------------------------------- 签名文件

QHash<QString, QString> BuildEngine::loadSignatures() const
{
    QHash<QString, QString> out;
    if (m_signaturePath.isEmpty())
        return out;

    QFile f(m_signaturePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return out;

    QTextStream ts(&f);
    while (!ts.atEnd()) {
        const QString line = ts.readLine();
        const int sp = line.indexOf(QLatin1Char('\t'));
        if (sp <= 0)
            continue;
        out.insert(line.mid(sp + 1), line.left(sp));
    }
    return out;
}

void BuildEngine::saveSignatures() const
{
    if (m_signaturePath.isEmpty() || !m_signaturesDirty)
        return;

    QDir().mkpath(QFileInfo(m_signaturePath).absolutePath());

    QFile f(m_signaturePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return;

    QTextStream ts(&f);
    for (auto it = m_signatures.constBegin(); it != m_signatures.constEnd(); ++it)
        ts << it.value() << QLatin1Char('\t') << it.key() << QLatin1Char('\n');
    f.close();
}

// ---------------------------------------------------------------- 流程

void BuildEngine::resetRun(Mode mode)
{
    m_mode = mode;
    m_jobs.clear();
    m_errors = 0;
    m_warnings = 0;
    m_cancelled = false;
    m_buffer.clear();
    m_running = true;
    m_timer.start();
    m_axfForSize.clear();
    m_signaturesDirty = false;
}

void BuildEngine::enqueue(const Job& job)
{
    m_jobs.append(job);
}

void BuildEngine::enqueueHook(const UserProgram& program, bool skipIfErrors)
{
    if (!program.enabled || program.command.trimmed().isEmpty())
        return;

    const QStringList parts = splitCommandLine(program.command);
    if (parts.isEmpty())
        return;

    Job job;
    job.program = parts.first();
    job.args = parts.mid(1);
    job.title = QStringLiteral("*** Executing: %1").arg(program.command.trimmed());
    job.skipIfErrors = skipIfErrors;
    job.isHook = true;
    job.hookStopsOnError = program.stopOnExitCode;
    job.hookCommand = program.command.trimmed();
    enqueue(job);
}

void BuildEngine::build(bool rebuildAll)
{
    if (m_running)
        return;

    if (!currentTarget()) {
        emit output(QStringLiteral("*** Error: 没有可编译的 Target"));
        emit finished(false, 0, 0);
        return;
    }

    if (!m_toolchain.isValid()) {
        emit output(QStringLiteral("*** Error: 尚未配置 ARM 编译器路径"));
        emit output(QStringLiteral("*** 请到 Tools -> Configure Toolchain 选择 armcc.exe 所在目录"));
        emit finished(false, 0, 0);
        return;
    }

    resetRun(Mode::Build);
    prepareBuild(rebuildAll);
    startNext();
}

void BuildEngine::translate(const QString& absoluteFilePath)
{
    if (m_running)
        return;
    if (!m_toolchain.isValid()) {
        emit output(QStringLiteral("*** Error: 尚未配置 ARM 编译器路径"));
        emit finished(false, 0, 0);
        return;
    }

    resetRun(Mode::Translate);
    prepareTranslate(absoluteFilePath);
    startNext();
}

void BuildEngine::clean()
{
    if (m_running)
        return;

    resetRun(Mode::Clean);
    prepareClean();
    m_running = false;
    emit finished(m_errors == 0, m_errors, m_warnings);
}

void BuildEngine::prepareBuild(bool rebuildAll)
{
    const Target* t = currentTarget();

    emit output(QStringLiteral("Build started: Project: %1, Configuration: %2")
                    .arg(m_project.baseName(), t->name));

    const QString version = m_toolchain.queryVersion();
    emit output(QStringLiteral("*** Using Compiler '%1', folder: '%2'")
                    .arg(version.isEmpty() ? m_toolchain.family : version,
                         QDir::toNativeSeparators(m_toolchain.binDir)));

    emit output(QStringLiteral("Build target '%1'").arg(t->name));

    QDir().mkpath(outputDir());

    const QString sigDir = outputDir() + QStringLiteral("/.mdk-ose");
    QDir().mkpath(sigDir);
    m_signaturePath = sigDir + QLatin1Char('/') + safeFileName(t->name) + QStringLiteral(".sig");
    m_signatures = loadSignatures();

    for (const UserProgram& p : t->beforeCompile)
        enqueueHook(p, false);

    const bool makeLibrary = t->createLib && !t->createExecutable;

    QStringList objects;
    QSet<QString> usedNames;
    int compiled = 0;
    int total = 0;

    for (const ProjGroup& g : t->groups) {
        if (g.excluded)
            continue;

        for (const ProjFile& f : g.files) {
            const FileKind kind = f.kind();
            const QString src = toAbsolute(projectDir(), f.path);

            if (kind == FileKind::Object || kind == FileKind::Library) {
                if (!f.excluded)
                    objects << native(src);
                continue;
            }

            if (kind != FileKind::CSource && kind != FileKind::CppSource
                && kind != FileKind::Assembly)
                continue;

            if (f.excluded)
                continue;

            ++total;

            if (!QFileInfo::exists(src)) {
                emit output(QStringLiteral("*** Error: 找不到源文件 \"%1\"").arg(native(src)));
                ++m_errors;
                ++m_jobErrorDiagnostics;
                continue;
            }

            QString base = QFileInfo(src).completeBaseName();
            if (usedNames.contains(base)) {
                int n = 2;
                while (usedNames.contains(QStringLiteral("%1_%2").arg(base).arg(n)))
                    ++n;
                base = QStringLiteral("%1_%2").arg(base).arg(n);
            }
            usedNames.insert(base);

            const QString obj = outputDir() + QLatin1Char('/') + base + QStringLiteral(".o");
            objects << native(obj);

            Job job;
            if (kind == FileKind::Assembly) {
                job.program = m_toolchain.as;
                job.args = assembleArgs(g, f, src, obj);
                job.title = QStringLiteral("assembling %1...").arg(QFileInfo(src).fileName());
            } else {
                job.program = m_toolchain.cc;
                job.args = compileArgs(g, f, src, obj);
                job.title = QStringLiteral("compiling %1...").arg(QFileInfo(src).fileName());
            }

            if (job.program.isEmpty()) {
                emit output(QStringLiteral("*** Error: 找不到 armcc / armasm，请检查编译器路径"));
                ++m_errors;
                return;
            }

            const QByteArray signature = signatureOf(job.args);
            const bool upToDate =
                !rebuildAll
                && isUpToDate(src, obj, signature,
                              mergeIncludeDirs(kind == FileKind::Assembly ? targetAsmIncludeDirs()
                                                                          : targetIncludeDirs(),
                                               g, f));

            m_signatures.insert(obj, QString::fromLatin1(signature));
            m_signaturesDirty = true;

            if (upToDate)
                continue;

            ++compiled;
            enqueue(job);
        }
    }

    if (total == 0)
        emit output(QStringLiteral("*** Warning: 这个 Target 没有任何参与编译的源文件"));

    if (compiled == 0 && total > 0)
        emit output(QStringLiteral("Target is up to date."));

    saveSignatures();

    if (objects.isEmpty()) {
        // 源文件缺失已经报过错了，这里不再重复计数
        if (m_errors == 0) {
            emit output(QStringLiteral("*** Error: 没有可链接的目标文件"));
            ++m_errors;
        }
        return;
    }

    for (const UserProgram& p : t->beforeMake)
        enqueueHook(p, true);

    if (makeLibrary) {
        Job lib;
        lib.program = m_toolchain.ar;
        lib.args = libArgs(objects);
        lib.title = QStringLiteral("creating library...");
        lib.skipIfErrors = true;
        lib.isLink = true;
        enqueue(lib);
    } else {
        Job link;
        link.program = m_toolchain.ld;
        link.args = linkArgs(objects);
        link.title = QStringLiteral("linking...");
        link.skipIfErrors = true;
        link.isLink = true;
        enqueue(link);
    }

    if (!makeLibrary && t->createHexFile && !m_toolchain.from.isEmpty()) {
        Job hex;
        hex.program = m_toolchain.from;
        hex.args = QStringList{ QStringLiteral("--i32"),
                                QStringLiteral("--output=") + native(outputDir() + QLatin1Char('/')
                                                                     + t->artifactName()
                                                                     + QStringLiteral(".hex")),
                                native(axfPath()) };
        hex.title = QStringLiteral("creating hex file...");
        hex.skipIfErrors = true;
        enqueue(hex);
    }

    for (const UserProgram& p : t->afterMake)
        enqueueHook(p, true);

    m_axfForSize = makeLibrary ? QString() : axfPath();
}

void BuildEngine::prepareTranslate(const QString& absoluteFilePath)
{
    const Target* t = currentTarget();
    emit output(QStringLiteral("Build started: Project: %1, Configuration: %2")
                    .arg(m_project.baseName(), t->name));

    const QString version = m_toolchain.queryVersion();
    emit output(QStringLiteral("*** Using Compiler '%1', folder: '%2'")
                    .arg(version.isEmpty() ? m_toolchain.family : version,
                         QDir::toNativeSeparators(m_toolchain.binDir)));

    QDir().mkpath(outputDir());

    ProjGroup group;
    ProjFile file;
    file.name = QFileInfo(absoluteFilePath).fileName();
    file.path = absoluteFilePath;

    const QString base = QFileInfo(absoluteFilePath).completeBaseName();
    const QString obj = outputDir() + QLatin1Char('/') + base + QStringLiteral(".o");

    Job job;
    if (fileKindFromPath(absoluteFilePath) == FileKind::Assembly) {
        job.program = m_toolchain.as;
        job.args = assembleArgs(group, file, absoluteFilePath, obj);
        job.title = QStringLiteral("assembling %1...").arg(QFileInfo(absoluteFilePath).fileName());
    } else {
        job.program = m_toolchain.cc;
        job.args = compileArgs(group, file, absoluteFilePath, obj);
        job.title = QStringLiteral("compiling %1...").arg(QFileInfo(absoluteFilePath).fileName());
    }
    enqueue(job);
}

void BuildEngine::prepareClean()
{
    const Target* t = currentTarget();
    emit output(QStringLiteral("Clean started: Project: %1, Configuration: %2")
                    .arg(m_project.baseName(), t ? t->name : QString()));

    QDir dir(outputDir());
    if (dir.exists()) {
        const QStringList filters{ QStringLiteral("*.o"), QStringLiteral("*.axf"),
                                   QStringLiteral("*.lib"), QStringLiteral("*.hex"),
                                   QStringLiteral("*.bin"), QStringLiteral("*.map"),
                                   QStringLiteral("*.lst"), QStringLiteral("*.crf"),
                                   QStringLiteral("*.d"), QStringLiteral("*.lnp") };
        const QFileInfoList entries = dir.entryInfoList(filters, QDir::Files);
        for (const QFileInfo& fi : entries) {
            emit output(QStringLiteral("\".\\%1\" - Deleted.").arg(fi.fileName()));
            QFile::remove(fi.absoluteFilePath());
        }
    }

    // 顺手清掉增量签名，保证下次一定全量
    const QString sigDir = outputDir() + QStringLiteral("/.mdk-ose");
    QDir(sigDir).removeRecursively();

    emit output(QStringLiteral("Clean finished."));
}

void BuildEngine::startNext()
{
    if (m_cancelled) {
        finishRun(false);
        return;
    }

    while (!m_jobs.isEmpty()) {
        const Job job = m_jobs.takeFirst();

        if (job.skipIfErrors && m_errors > 0) {
            m_jobs.clear();
            finishRun(false);
            return;
        }

        if (!job.title.isEmpty())
            emit output(job.title);

        if (job.program.isEmpty()) {
            emit output(QStringLiteral("*** Error: 程序路径为空（armlink / armar / 用户命令？）"));
            ++m_errors;
            continue;
        }

        m_proc = new QProcess(this);
        m_proc->setWorkingDirectory(projectDir());
        m_proc->setProcessChannelMode(QProcess::MergedChannels);
        m_proc->setProgram(resolveProgram(job.program, m_toolchain.binDir));
        m_proc->setArguments(job.args);

        // 让 fromelf.exe 之类能在 PATH 里找到
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        if (!m_toolchain.binDir.isEmpty()) {
            const QString path = env.value(QStringLiteral("PATH"));
            env.insert(QStringLiteral("PATH"),
                       m_toolchain.binDir + QDir::listSeparator() + path);
        }
        m_proc->setProcessEnvironment(env);

        m_jobErrorDiagnostics = 0;

        // 程序不存在时 QProcess 只发 errorOccurred，不发 finished，
        // 不处理就会把整个任务队列卡死。
        connect(m_proc, &QProcess::errorOccurred, this,
                [this, job](QProcess::ProcessError e) {
                    if (e != QProcess::FailedToStart || !m_proc)
                        return;
                    emit output(QStringLiteral("*** Error: 无法启动 \"%1\"（找不到可执行文件？）")
                                    .arg(job.program));
                    ++m_errors;
                    m_proc->deleteLater();
                    m_proc = nullptr;
                    startNext();
                });

        connect(m_proc, &QProcess::readyRead, this, &BuildEngine::pumpOutput);
        connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, job](int code, QProcess::ExitStatus status) {
                    if (m_proc) {
                        m_buffer += m_proc->readAll();
                        pumpOutput();
                    }
                    if (!m_buffer.isEmpty()) {
                        handleLine(QString::fromLocal8Bit(m_buffer));
                        m_buffer.clear();
                    }

                    const bool ok = (status == QProcess::NormalExit && code == 0);
                    if (!ok) {
                        if (job.isHook) {
                            if (job.hookStopsOnError) {
                                ++m_errors;
                                emit output(QStringLiteral("*** 用户命令失败: %1")
                                                .arg(job.hookCommand));
                            } else {
                                ++m_warnings;
                                emit output(QStringLiteral("*** 用户命令返回 %1（已忽略）: %2")
                                                .arg(code).arg(job.hookCommand));
                            }
                        } else {
                            // 编译器自己打过 Error 行时不要重复计数
                            if (m_jobErrorDiagnostics == 0)
                                ++m_errors;
                            if (job.isLink)
                                emit output(QStringLiteral("*** 链接失败"));
                        }
                    } else if (job.isLink && !m_axfForSize.isEmpty()
                               && QFileInfo::exists(m_axfForSize)) {
                        emitProgramSize();
                    }

                    if (m_proc) {
                        m_proc->deleteLater();
                        m_proc = nullptr;
                    }

                    startNext();
                });

        m_proc->start();
        return;
    }

    finishRun(m_errors == 0);
}

void BuildEngine::pumpOutput()
{
    if (!m_proc)
        return;

    m_buffer += m_proc->readAll();

    int idx = 0;
    while ((idx = m_buffer.indexOf('\n')) >= 0) {
        QByteArray line = m_buffer.left(idx);
        m_buffer.remove(0, idx + 1);
        if (line.endsWith('\r'))
            line.chop(1);
        handleLine(QString::fromLocal8Bit(line));
    }

    if (m_buffer.size() > (1 << 20)) {
        handleLine(QString::fromLocal8Bit(m_buffer));
        m_buffer.clear();
    }
}

void BuildEngine::handleLine(const QString& line)
{
    if (line.isEmpty())
        return;

    emit output(line);

    Diagnostic d;
    if (Diagnostics::parseLine(line, projectDir(), &d)) {
        if (d.severity == Severity::Error) {
            ++m_errors;
            ++m_jobErrorDiagnostics;
        } else if (d.severity == Severity::Warning) {
            ++m_warnings;
        }
        emit diagnostic(d);
    }
}

void BuildEngine::emitProgramSize()
{
    const QString mapPath = QFileInfo(m_axfForSize).absolutePath() + QLatin1Char('/')
                            + QFileInfo(m_axfForSize).completeBaseName() + QStringLiteral(".map");

    QFile map(mapPath);
    if (!map.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    // armlink 的 map 里，Totals 行是：
    //    Code (inc. data)  RO Data  RW Data  ZI Data  Debug   Grand Totals
    // 也就是 6 列；某些版本只给 5 列，所以要按列数分别解释。
    static const QRegularExpression totalsRe(
        QStringLiteral("^\\s*(\\d+)\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)\\s*(\\d+)?\\s*"
                       "(?:Object|Library|Grand)\\s+Totals.*$"));

    qint64 code = 0, ro = 0, rw = 0, zi = 0;
    bool found = false;

    QTextStream ts(&map);
    while (!ts.atEnd()) {
        const QString line = ts.readLine();
        const QRegularExpressionMatch m = totalsRe.match(line);
        if (!m.hasMatch())
            continue;

        const bool sixColumns = !m.captured(6).isEmpty();
        code = m.captured(1).toLongLong();
        if (sixColumns) {
            ro = m.captured(3).toLongLong();
            rw = m.captured(4).toLongLong();
            zi = m.captured(5).toLongLong();
        } else {
            ro = m.captured(2).toLongLong();
            rw = m.captured(3).toLongLong();
            zi = m.captured(4).toLongLong();
        }
        found = true;

        if (line.contains(QLatin1String("Grand Totals")))
            break;
    }
    map.close();

    if (!found)
        return;

    emit output(QStringLiteral("Program Size: Code=%1 RO-data=%2 RW-data=%3 ZI-data=%4")
                    .arg(code).arg(ro).arg(rw).arg(zi));
}

void BuildEngine::finishRun(bool success)
{
    if (!m_running)
        return;

    m_running = false;
    saveSignatures();

    if (m_mode == Mode::Build || m_mode == Mode::Translate) {
        const Target* t = currentTarget();
        const QString name = t ? t->artifactName() : QStringLiteral("Project");
        const bool makeLibrary = t && t->createLib && !t->createExecutable;

        QString dir = (t && !t->outputDirectory.isEmpty()) ? t->outputDirectory
                                                           : QStringLiteral(".\\Objects\\");
        dir.replace(QLatin1Char('/'), QLatin1Char('\\'));
        if (!dir.endsWith(QLatin1Char('\\')))
            dir += QLatin1Char('\\');

        const QString artifact =
            dir + name + (makeLibrary ? QStringLiteral(".lib") : QStringLiteral(".axf"));

        emit output(QStringLiteral("\"%1\" - %2 Error(s), %3 Warning(s).")
                        .arg(artifact).arg(m_errors).arg(m_warnings));
        emit output(QStringLiteral("Build Time Elapsed:  %1")
                        .arg(formatElapsed(m_timer.elapsed())));
    }

    emit finished(success && m_errors == 0, m_errors, m_warnings);
}

void BuildEngine::cancel()
{
    m_cancelled = true;
    m_jobs.clear();
    if (m_proc)
        m_proc->kill();
    emit output(QStringLiteral("*** 构建已取消"));
}
