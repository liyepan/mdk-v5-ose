#pragma once

#include <QWidget>

#include "core/UvprojModel.h"

class QComboBox;
class QTreeWidget;
class QTreeWidgetItem;

// uVision 左侧的 Project 窗口：顶部一个 Target 下拉框，下面是 目标 -> 分组 -> 文件。
class ProjectTree : public QWidget
{
    Q_OBJECT

public:
    explicit ProjectTree(QWidget* parent = nullptr);

    void setProject(const UvProject& project, int activeTarget);
    void clear();

    int  currentTargetIndex() const;
    void setCurrentTargetIndex(int index);

signals:
    void targetChanged(int index);
    void fileActivated(const QString& absolutePath);
    void optionsForTargetRequested();
    void optionsForFileRequested(const QString& absolutePath);
    void openWithEncodingRequested(const QString& absolutePath, const QString& encoding);
    // 右键菜单里切换"参与编译"，include=true 表示加回构建
    void includeInBuildToggled(const QString& absolutePath, bool include);

private:
    void rebuildTree();
    void showContextMenu(const QPoint& pos);
    void activateItem(QTreeWidgetItem* item);

    QComboBox*   m_targetCombo = nullptr;
    QTreeWidget* m_tree = nullptr;
    UvProject    m_project;
    int          m_activeTarget = 0;
    bool         m_blockSignals = false;
};
