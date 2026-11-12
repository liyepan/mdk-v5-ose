#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QStringList>

#include "core/Diagnostic.h"
#include "core/Toolchain.h"
#include "core/UvprojModel.h"

class QProcess;

// 把 uVision 工程变成一串 armcc / armasm / armlink / armar / fromelf 调用，
// 按 uVision 的格式回放输出。
//
// 执行器按"阶段"跑：
//   [BeforeCompile 用户命令]（串行）
//   [编译 / 汇编]（并行，-j N）
//   [BeforeMake 用户命令]（串行）
//   [链接 或 armar]（串行）
//   [fromelf 生成 hex]（串行）
//   [AfterMake 用户命令]（串行）
// 串行任务必须等并行批次全部结束才开始，所以链接永远拿到完整的 .o 集合。
//
// 增量判断由三部分组成：
//   1. 目标文件比源文件新
//   2. 目标文件比所有被包含的本地头文件新（自己扫 #include）
//   3. 编译参数指纹没变（存在 Objects/.mdk-ose/<target>.sig）
class BuildEngine : public QObject
{
    Q_OBJECT

public:
    explicit BuildEngine(QObject* parent = nullptr);
    ~BuildEngine() override;

    void setToolchain(const Toolchain& tc);
    const Toolchain& toolchain() const { return m_toolchain; }

    void setProject(const UvProject& project, int targetIndex);
    bool hasProject() const { return !m_project.targets.isEmpty(); }

    // 并行编译的任务数，1 表示串行
    void setJobs(int jobs);
    int  jobs() const { return m_jobLimit; }

    bool isRunning() const { return m_running; }

    void build(bool rebuildAll);
    void translate(const QString& absoluteFilePath);
    void clean();

    void cancel();

signals:
    void output(const QString& line);
    void diagnostic(const Diagnostic& d);
    void finished(bool success, int errors, int warnings);
    void status(const QString& text);

private:
    enum class Mode { Build, Translate, Clean };

    struct Job
    {
        QString     program;
        QStringList args;
        QString     title;
        bool        skipIfErrors = false;
        bool        isLink = false;
        bool        isHook = false;
        bool        hookStopsOnError = false;
        bool        parallel = false;
        QString     hookCommand;
    };

    // 一条正在跑的编译命令。继承 QObject 只是为了能安全地 deleteLater：
    // 进程的 finished 信号可能在它之后才到。
    struct ActiveJob : public QObject
    {
        explicit ActiveJob(QObject* parent = nullptr) : QObject(parent) {}

        Job         job;
        QProcess*   proc = nullptr;
        QByteArray  buffer;
        QStringList lines;
        int         errorDiagnostics = 0;
        bool        handled = false;
    };

    void resetRun(Mode mode);
    void enqueue(const Job& job);
    void enqueueHook(const UserProgram& program, bool skipIfErrors);

    void startJobs();
    void launch(const Job& job);
    void pumpOutput(ActiveJob* active);
    void handleLine(ActiveJob* active, const QString& line);
    void appendLine(ActiveJob* active, const QString& line);
    void onJobFinished(ActiveJob* active, int exitCode, int exitStatus);
    void flushActive(ActiveJob* active);
    void removeActive(ActiveJob* active);
    void finishRun(bool success);

    void prepareBuild(bool rebuildAll);
    void prepareTranslate(const QString& absoluteFilePath);
    void prepareClean();

    const Target* currentTarget() const;
    QString projectDir() const;
    QString outputDir() const;
    QString axfPath() const;
    QString libPath() const;
    QString cpuName() const;

    QStringList targetIncludeDirs() const;
    QStringList targetAsmIncludeDirs() const;
    QStringList mergeIncludeDirs(const QStringList& base, const ProjGroup& group,
                                 const ProjFile& file) const;
    QStringList mergeDefines(const ProjGroup& group, const ProjFile& file) const;

    QStringList compileArgs(const ProjGroup& group, const ProjFile& file,
                            const QString& sourcePath, const QString& objectPath) const;
    QStringList assembleArgs(const ProjGroup& group, const ProjFile& file,
                             const QString& sourcePath, const QString& objectPath) const;
    QStringList linkArgs(const QStringList& objects) const;
    QStringList libArgs(const QStringList& objects) const;

    QStringList scanDependencies(const QString& sourcePath, const QStringList& includeDirs) const;
    bool        isUpToDate(const QString& source, const QString& object,
                           const QByteArray& signature, const QStringList& includeDirs) const;

    QHash<QString, QString> loadSignatures() const;
    void                    saveSignatures() const;
    void                    emitProgramSize();

    Toolchain   m_toolchain;
    UvProject   m_project;
    int         m_targetIndex = 0;
    Mode        m_mode = Mode::Build;

    QList<Job>          m_jobs;
    QList<ActiveJob*>   m_active;
    int                 m_nextIndex = 0;
    int                 m_jobLimit = 1;
    bool                m_bufferedOutput = false;

    bool        m_running = false;
    bool        m_cancelled = false;
    int         m_errors = 0;
    int         m_warnings = 0;
    int         m_totalCompile = 0;
    int         m_doneCompile = 0;

    QElapsedTimer m_timer;
    QString     m_axfForSize;

    QString                     m_signaturePath;
    QHash<QString, QString>     m_signatures;
    bool                        m_signaturesDirty = false;
};
