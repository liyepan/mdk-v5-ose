#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QSet>
#include <QStringList>

#include "core/Diagnostic.h"
#include "core/Toolchain.h"
#include "core/UvprojModel.h"

class QProcess;

// 负责把 uVision 工程变成一串 armcc / armasm / armlink / fromelf 调用，
// 并把输出按 uVision 的格式回放到 Build Output 窗口。
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
        QString     title;          // 回放为 "compiling main.c..."
        bool        skipIfErrors = false;
        bool        isLink = false;
    };

    void resetRun(Mode mode);
    void enqueue(const Job& job);
    void startNext();
    void pumpOutput();
    void handleLine(const QString& line);
    void finishRun(bool success);
    void onProcessFinished(int exitCode, int status);

    void prepareBuild(bool rebuildAll);
    void prepareTranslate(const QString& absoluteFilePath);
    void prepareClean();

    const Target* currentTarget() const;
    QString projectDir() const;
    QString outputDir() const;
    QString axfPath() const;
    QString cpuName() const;

    QStringList compileArgs(const ProjFile& file, const QString& sourcePath,
                            const QString& objectPath) const;
    QStringList assembleArgs(const ProjFile& file, const QString& sourcePath,
                             const QString& objectPath) const;
    QStringList linkArgs(const QStringList& objects) const;

    void emitProgramSize();

    Toolchain   m_toolchain;
    UvProject   m_project;
    int         m_targetIndex = 0;
    Mode        m_mode = Mode::Build;

    QList<Job>  m_jobs;
    QProcess*   m_proc = nullptr;
    bool        m_running = false;
    bool        m_cancelled = false;
    int         m_errors = 0;
    int         m_warnings = 0;
    int         m_jobErrorDiagnostics = 0;   // 当前这条命令自己报出的错误数

    QByteArray  m_buffer;
    QElapsedTimer m_timer;
    QString     m_axfForSize;
};
