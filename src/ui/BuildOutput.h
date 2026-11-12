#pragma once

#include <QString>
#include <QWidget>

#include "core/Diagnostic.h"

class QPlainTextEdit;
class QTabWidget;
class QTreeWidget;

// 底部输出窗口：Build Output 文本页 + Error List 列表页。
class BuildOutput : public QWidget
{
    Q_OBJECT

public:
    explicit BuildOutput(QWidget* parent = nullptr);

    void setProjectDir(const QString& dir);

    void clear();
    void appendLine(const QString& line);
    void addDiagnostic(const Diagnostic& d);
    void clearDiagnostics();

    void showErrorList();

signals:
    void jumpRequested(const QString& file, int line);

private:
    QTabWidget*     m_tabs = nullptr;
    QPlainTextEdit* m_log = nullptr;
    QTreeWidget*    m_errors = nullptr;
    QString         m_projectDir;
};
