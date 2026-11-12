#include "core/BuildEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTextStream>
#include <QTime>

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

QString formatElapsed(qint64 ms)
{
    const qint64 total = ms / 1000;
    const qint64 h = total / 3600;
    const qint64 m = (total % 3600) / 60;
    const qint64 s = total % 60;
    return QString::asprintf("%02lld:%02lld:%02lld", h, m, s);
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
    const QString name = t && !t->outputName.isEmpty() ? t->outputName : QStringLiteral("Project");
    return outputDir() + QLatin1Char('/') + name + QStringLiteral(".axf");
}

QString BuildEngine::cpuName() const
{
    const Target* t = currentTarget();
    return t ? t->displayCpu() : QStringLiteral("Cortex-M3");
}

// ---------------------------------------------------------------- 参数构造

QStringList BuildEngine::compileArgs(const ProjFile& file, const QString& sourcePath,
                                     const QString& objectPath) const
{
    const Target* t = currentTarget();
    QStringList a;

    const FileKind kind = file.kind();

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
        if (kind == FileKind::CSource && !(t && t->c99))
            a << QStringLiteral("--c90");
        if (kind == FileKind::CSource && t && t->c99)
            a << QStringLiteral("--c99");
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

    // 宏定义
    QStringList defines = splitOptionList(t ? t->defines : QString());
    if (file.hasOwnOptions)
        defines += splitOptionList(file.defines);
    for (const QString& d : defines)
        a << (QStringLiteral("-D") + d);

    // 头文件路径
    QStringList incs = splitOptionList(t ? t->includePaths : QString());
    if (file.hasOwnOptions)
        incs += splitOptionList(file.includePaths);
    for (const QString& inc : incs) {
        const QString abs = toAbsolute(projectDir(), inc);
        a << (QStringLiteral("-I") + native(abs));
    }

    if (t && !t->miscControls.trimmed().isEmpty())
        a << splitArgsLoose(t->miscControls);
    if (file.hasOwnOptions && !file.misc.trimmed().isEmpty())
        a << splitArgsLoose(file.misc);

    a << QStringLiteral("-o") << native(objectPath);
    a << native(sourcePath);
    return a;
}

QStringList BuildEngine::assembleArgs(const ProjFile& file, const QString& sourcePath,
                                      const QString& objectPath) const
{
    const Target* t = currentTarget();
    QStringList a;

    a << QStringLiteral("--cpu") << cpuName();
    if (t && t->debugInformation)
        a << QStringLiteral("-g");

    QStringList defines = splitOptionList(t ? t->asmDefines : QString());
    if (file.hasOwnOptions)
        defines += splitOptionList(file.defines);
    for (const QString& d : defines) {
        const int eq = d.indexOf(QLatin1Char('='));
        if (eq > 0)
            a << QStringLiteral("--pd") << (d.left(eq) + QStringLiteral(" SETA ") + d.mid(eq + 1));
        else
            a << QStringLiteral("--pd") << (d + QStringLiteral(" SETA 1"));
    }

    QStringList incs = splitOptionList(t ? t->asmIncludePaths : QString());
    if (file.hasOwnOptions)
        incs += splitOptionList(file.includePaths);
    for (const QString& inc : incs)
        a << (QStringLiteral("-I") + native(toAbsolute(projectDir(), inc)));

    if (t && !t->asmMisc.trimmed().isEmpty())
        a << splitArgsLoose(t->asmMisc);

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
}

void BuildEngine::enqueue(const Job& job)
{
    m_jobs.append(job);
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
        emit output(QStringLiteral("*** 请到  Project -> Options for Target -> Tools，或菜单 Tools -> Configure Toolchain 选择 armcc.exe 所在目录"));
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
    finishRun(true);
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

    QStringList objects;
    QSet<QString> usedNames;
    int compiled = 0;
    int total = 0;

    for (const ProjGroup& g : t->groups) {
        for (const ProjFile& f : g.files) {
            const FileKind kind = f.kind();
            const QString src = toAbsolute(projectDir(), f.path);

            if (kind == FileKind::Object || kind == FileKind::Library) {
                objects << native(src);
                continue;
            }
            if (kind != FileKind::CSource && kind != FileKind::CppSource
                && kind != FileKind::Assembly)
                continue;

            ++total;

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

            if (f.excluded)
                continue;

            const bool upToDate = !rebuildAll && QFileInfo::exists(obj)
                                  && QFileInfo(obj).lastModified() >= QFileInfo(src).lastModified();
            if (upToDate)
                continue;

            ++compiled;

            Job job;
            if (kind == FileKind::Assembly) {
                job.program = m_toolchain.as;
                job.args = assembleArgs(f, src, obj);
                job.title = QStringLiteral("assembling %1...").arg(QFileInfo(src).fileName());
            } else {
                job.program = m_toolchain.cc;
                job.args = compileArgs(f, src, obj);
                job.title = QStringLiteral("compiling %1...").arg(QFileInfo(src).fileName());
            }
            if (job.program.isEmpty()) {
                emit output(QStringLiteral("*** Error: 找不到 armcc / armasm，请检查编译器路径"));
                m_errors++;
                return;
            }
            job.skipIfErrors = false;
            enqueue(job);
        }
    }

    if (compiled == 0 && total > 0)
        emit output(QStringLiteral("Target is up to date."));

    Job link;
    link.program = m_toolchain.ld;
    link.args = linkArgs(objects);
    link.title = QStringLiteral("linking...");
    link.skipIfErrors = true;
    link.isLink = true;
    enqueue(link);

    if (t->createHexFile) {
        Job hex;
        hex.program = m_toolchain.from;
        hex.args = QStringList{ QStringLiteral("--i32"),
                                QStringLiteral("--output=") + native(outputDir() + QLatin1Char('/')
                                                                     + (t->outputName.isEmpty()
                                                                            ? QStringLiteral("Project")
                                                                            : t->outputName)
                                                                     + QStringLiteral(".hex")),
                                native(axfPath()) };
        hex.title = QStringLiteral("creating hex file...");
        hex.skipIfErrors = true;
        enqueue(hex);
    }

    m_axfForSize = axfPath();
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

    ProjFile file;
    file.name = QFileInfo(absoluteFilePath).fileName();
    file.path = absoluteFilePath;

    QString base = QFileInfo(absoluteFilePath).completeBaseName();
    const QString obj = outputDir() + QLatin1Char('/') + base + QStringLiteral(".o");

    Job job;
    const FileKind kind = fileKindFromPath(absoluteFilePath);
    if (kind == FileKind::Assembly) {
        job.program = m_toolchain.as;
        job.args = assembleArgs(file, absoluteFilePath, obj);
        job.title = QStringLiteral("assembling %1...").arg(QFileInfo(absoluteFilePath).fileName());
    } else {
        job.program = m_toolchain.cc;
        job.args = compileArgs(file, absoluteFilePath, obj);
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
                                   QStringLiteral("*.hex"), QStringLiteral("*.bin"),
                                   QStringLiteral("*.map"), QStringLiteral("*.lst"),
                                   QStringLiteral("*.crf"), QStringLiteral("*.d"),
                                   QStringLiteral("*.lnp") };
        const QFileInfoList entries = dir.entryInfoList(filters, QDir::Files);
        for (const QFileInfo& fi : entries) {
            emit output(QStringLiteral("\".\\%1\" - Deleted.").arg(fi.fileName()));
            QFile::remove(fi.absoluteFilePath());
        }
    }
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
            emit output(QStringLiteral("*** Error: 编译器程序路径为空"));
            ++m_errors;
            continue;
        }

        m_proc = new QProcess(this);
        m_proc->setWorkingDirectory(projectDir());
        m_proc->setProcessChannelMode(QProcess::MergedChannels);
        m_proc->setProgram(job.program);
        m_proc->setArguments(job.args);

        m_jobErrorDiagnostics = 0;

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
                        // 编译器已经打印过 Error 行时不要重复计数，否则 Error(s) 会翻倍
                        if (m_jobErrorDiagnostics == 0)
                            ++m_errors;
                        if (job.isLink)
                            emit output(QStringLiteral("*** 链接失败"));
                    } else if (job.isLink && !m_axfForSize.isEmpty() && QFileInfo::exists(m_axfForSize)) {
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
        }
        else if (d.severity == Severity::Warning)
            ++m_warnings;
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
    // 也就是 6 列：Code、其中内联的 data、RO、RW、ZI、Debug。
    // 某些版本只给 5 列（没有 inc. data），所以要按列数分别解释。
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

        // "Grand Totals" 是最全的一行，遇到就可以收工
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

    if (m_mode == Mode::Build || m_mode == Mode::Translate) {
        const Target* t = currentTarget();
        const QString name = (t && !t->outputName.isEmpty()) ? t->outputName
                                                             : QStringLiteral("Project");
        QString axf = QStringLiteral(".\\Objects\\") + name + QStringLiteral(".axf");
        if (t && !t->outputDirectory.isEmpty()) {
            QString dir = t->outputDirectory;
            dir.replace(QLatin1Char('/'), QLatin1Char('\\'));
            if (!dir.endsWith(QLatin1Char('\\')))
                dir += QLatin1Char('\\');
            axf = dir + name + QStringLiteral(".axf");
        }
        emit output(QStringLiteral("\"%1\" - %2 Error(s), %3 Warning(s).")
                        .arg(axf).arg(m_errors).arg(m_warnings));
        emit output(QStringLiteral("Build Time Elapsed:  %1")
                        .arg(formatElapsed(m_timer.elapsed())));
    }

    emit finished(success && m_errors == 0, m_errors, m_warnings);
}

void BuildEngine::onProcessFinished(int, int)
{
}

void BuildEngine::cancel()
{
    m_cancelled = true;
    m_jobs.clear();
    if (m_proc) {
        m_proc->kill();
    }
    emit output(QStringLiteral("*** 构建已取消"));
}
