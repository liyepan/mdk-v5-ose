#pragma once

#include <QDialog>
#include <QList>

#include "core/UvprojModel.h"

class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;

// Tools -> Manage Project Items：增删分组、加删文件、调整顺序。
// 只改内存里的 Target::groups，点 OK 之后由主窗口写回 .uvprojx。
class ManageProjectDialog : public QDialog
{
    Q_OBJECT

public:
    ManageProjectDialog(const Target& target, const QString& projectDir,
                        QWidget* parent = nullptr);

    QList<ProjGroup> groups() const { return m_groups; }

private:
    void buildTree();
    void refreshButtons();
    QTreeWidgetItem* currentGroupItem() const;   // 选中项所属的组
    void newGroup();
    void addFiles();
    void removeSelected();
    void moveSelected(int delta);
    void accept() override;

    QString            m_projectDir;
    QList<ProjGroup>   m_groups;
    QTreeWidget*       m_tree = nullptr;
    QPushButton*       m_btnNewGroup = nullptr;
    QPushButton*       m_btnAddFiles = nullptr;
    QPushButton*       m_btnRemove = nullptr;
    QPushButton*       m_btnUp = nullptr;
    QPushButton*       m_btnDown = nullptr;
};
