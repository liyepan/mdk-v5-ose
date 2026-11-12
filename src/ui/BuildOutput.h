#pragma once

#include <QString>
#include <QWidget>

#include "core/Diagnostic.h"

class QPlainTextEdit;
class QPoint;
class QTabWidget;
class QTreeWidget;

// 底部输出窗口：Build Output 文本页 + Error List 列表页。
// 两页都支持 Ctrl+C / Ctrl+A / Ctrl+Shift+C 和右键菜单，
// 因为编译失败时用户第一件事就是把错误贴给别人。
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
    void showLog();

    // 供菜单/快捷键调用
    void copyLogSelection();
    void copyLogAll();
    void copyDiagnostics(bool onlySelected);
    void saveLogAs();

signals:
    void jumpRequested(const QString& file, int line);

private:
    void buildLogMenu(const QPoint& pos);
    void buildErrorMenu(const QPoint& pos);
    QString diagnosticsAsText(bool onlySelected) const;
    void putOnClipboard(const QString& text);

    QTabWidget*     m_tabs = nullptr;
    QPlainTextEdit* m_log = nullptr;
    QTreeWidget*    m_errors = nullptr;
    QString         m_projectDir;
};
