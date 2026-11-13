#pragma once

#include <QDialog>
#include <QHash>
#include <QList>

#include "core/UvprojModel.h"

class QListWidget;
class QTabWidget;

// Tools -> Manage Project Items，布局照 uVision：
//
//   [Project Items][Folders/Extensions][Books][Project Info/Layer]
//    Project Targets:      Groups:              Files:
//    [new][del][^][v]      [new][del][^][v]     [del][^][v]
//    +----------------+    +---------------+    +----------------+
//    | Debug          |    | apl           |    | main.c         |
//    | Release        |    | bsp           |    | sysPara.c      |
//    +----------------+    +---------------+    +----------------+
//    [Set as Current Target]                    [Add Files...]
//                                       [OK] [Cancel] [Help]
class ManageProjectDialog : public QDialog
{
    Q_OBJECT

public:
    ManageProjectDialog(const QList<Target>& targets, int currentTarget,
                        const QString& projectDir, QWidget* parent = nullptr);

    QList<Target> targets() const { return m_targets; }
    int currentTarget() const { return m_current; }

    // 新建出来的 target 是从哪个复制来的（同步文件时要用）
    QHash<QString, QString> cloneSources() const { return m_cloneSources; }
    // 原来的 target 名字顺序，用来判断 target 列表有没有变
    QStringList originalNames() const { return m_originalNames; }

private:
    Target*       activeTarget();
    const Target* activeTarget() const;
    int           activeGroupIndex() const;

    void rebuildTargetList(int selectTarget);
    void rebuildGroupList(int selectGroup);
    void rebuildFileList(int selectFile);
    void refreshButtons();

    void newGroup();
    void removeGroup();
    void moveGroup(int delta);
    void renameGroup();

    void addFiles();
    void removeFile();
    void moveFile(int delta);

    void accept() override;

    QList<Target> m_targets;
    QHash<QString, QString> m_cloneSources;
    QStringList   m_originalNames;
    int           m_current = 0;
    int           m_group = -1;
    QString       m_projectDir;

    QListWidget*  m_targetList = nullptr;
    QListWidget*  m_groupList = nullptr;
    QListWidget*  m_fileList = nullptr;
    QTabWidget*   m_tabs = nullptr;
};
