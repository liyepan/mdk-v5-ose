#include "core/ProjectLoader.h"

#include "core/Lang.h"
#include "core/Timing.h"
#include "core/UvprojReader.h"

#include <QElapsedTimer>

ProjectLoader::ProjectLoader(const QString& path, int targetIndex, QObject* parent)
    : QObject(parent), m_fromFile(true), m_path(path), m_targetIndex(targetIndex)
{
}

ProjectLoader::ProjectLoader(const UvProject& project, int targetIndex, QObject* parent)
    : QObject(parent), m_fromFile(false), m_input(project), m_targetIndex(targetIndex)
{
}

void ProjectLoader::run()
{
    Timing::Scope runScope("ProjectLoader (worker)");

    UvProject loaded;

    if (m_fromFile) {
        emit stage(Lang::t("Loading..."), Lang::t("Reading project file..."));

        QString loadError;
        if (!Uvproj::load(m_path, loaded, &loadError)) {
            error = loadError;
            ok = false;
            emit finished();
            return;
        }
    } else {
        loaded = m_input;
    }

    if (m_abort.load()) {
        emit finished();
        return;
    }

    emit stage(Lang::t("Loading..."), Lang::t("Scanning source files..."));

    // 信号节流：工作线程里别把事件队列刷爆，40 ms 报一次够了
    QElapsedTimer throttle;
    throttle.start();

    SymbolIndex index;
    index.build(loaded, m_targetIndex, [this, &throttle](int done, int total) {
        if (m_abort.load())
            return false;
        if (throttle.elapsed() >= 40) {
            throttle.restart();
            emit progress(done, total);
        }
        return true;
    });

    // 排序也在这里做掉：几万个符号排一遍，主线程上要几十毫秒
    index.names();

    project = loaded;
    symbols = index;
    ok = true;
    emit finished();
}
