#pragma once

#include <QObject>
#include <QString>

#include <atomic>

#include "core/SymbolIndex.h"
#include "core/UvprojModel.h"

// 后台线程里干的活：
//   1. 从 .uvprojx 读工程（打开工程时）；
//   2. 建符号表（打开工程、切 target 都要）。
// 用法：new 一个，把 run() 丢到工作线程里跑，接 finished 信号取结果。
// 信号都是从工作线程发出的，槽用默认 AutoConnection 就会排队回主线程。
class ProjectLoader : public QObject
{
    Q_OBJECT

public:
    // 打开工程：读文件 + 建符号表
    ProjectLoader(const QString& path, int targetIndex, QObject* parent = nullptr);
    // 只重建符号表（切 target），工程本身已经在了
    ProjectLoader(const UvProject& project, int targetIndex, QObject* parent = nullptr);

    void run();

    // true = 只是重建符号表（切 target），工程本身没变
    bool isReloadOnly() const { return !m_fromFile; }

    // 让 run() 尽快收尾（关窗口的时候用）
    void cancel() { m_abort.store(true); }

    bool      ok = false;
    QString   error;
    UvProject project;
    SymbolIndex symbols;

signals:
    void stage(const QString& title, const QString& detail);
    void progress(int done, int total);
    void finished();

private:
    bool             m_fromFile = false;
    QString          m_path;
    UvProject        m_input;
    int              m_targetIndex = 0;
    std::atomic_bool m_abort{false};
};
