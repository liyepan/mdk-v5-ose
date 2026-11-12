#include "ui/ProjectTree.h"

#include "core/UvprojReader.h"
#include "ui/KeilStyle.h"

#include <QComboBox>
#include <QHeaderView>
#include <QMenu>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

QString iconNameFor(FileKind kind)
{
    switch (kind) {
    case FileKind::CSource:   return QStringLiteral("file_c");
    case FileKind::CppSource: return QStringLiteral("file_cpp");
    case FileKind::Assembly:  return QStringLiteral("file_asm");
    case FileKind::Library:   return QStringLiteral("book");
    default:                  return QStringLiteral("file_c");
    }
}

const int kRolePath = Qt::UserRole + 1;
const int kRoleKind = Qt::UserRole + 2;   // 0=target 1=group 2=file
const int kRoleExcluded = Qt::UserRole + 3;

} // namespace

ProjectTree::ProjectTree(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    m_targetCombo = new QComboBox(this);
    m_targetCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    layout->addWidget(m_targetCombo);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setAlternatingRowColors(false);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_tree, 1);

    connect(m_targetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                if (m_blockSignals)
                    return;
                m_activeTarget = index;
                rebuildTree();
                emit targetChanged(index);
            });

    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) { activateItem(item); });

    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &ProjectTree::showContextMenu);
}

void ProjectTree::clear()
{
    m_project = UvProject();
    m_targetCombo->clear();
    m_tree->clear();
}

void ProjectTree::setProject(const UvProject& project, int activeTarget)
{
    m_project = project;
    m_activeTarget = qBound(0, activeTarget, qMax(0, project.targets.size() - 1));

    m_blockSignals = true;
    m_targetCombo->clear();
    for (const Target& t : project.targets)
        m_targetCombo->addItem(t.name);
    m_targetCombo->setCurrentIndex(m_activeTarget);
    m_blockSignals = false;

    rebuildTree();
}

int ProjectTree::currentTargetIndex() const
{
    return m_activeTarget;
}

void ProjectTree::setCurrentTargetIndex(int index)
{
    m_targetCombo->setCurrentIndex(index);
}

void ProjectTree::rebuildTree()
{
    m_tree->clear();

    const Target* target = m_project.target(m_activeTarget);
    if (!target)
        return;

    auto* root = new QTreeWidgetItem(m_tree);
    root->setText(0, target->name);
    root->setIcon(0, KeilStyle::icon(QStringLiteral("target")));
    root->setData(0, kRoleKind, 0);
    root->setExpanded(true);

    for (const ProjGroup& group : target->groups) {
        auto* groupItem = new QTreeWidgetItem(root);
        groupItem->setText(0, group.name);
        groupItem->setIcon(0, KeilStyle::icon(QStringLiteral("folder")));
        groupItem->setData(0, kRoleKind, 1);
        groupItem->setExpanded(true);

        for (const ProjFile& file : group.files) {
            auto* fileItem = new QTreeWidgetItem(groupItem);
            fileItem->setText(0, file.name);
            fileItem->setIcon(0, KeilStyle::icon(iconNameFor(file.kind())));
            fileItem->setData(0, kRoleKind, 2);
            fileItem->setData(0, kRolePath, Uvproj::resolvePath(m_project.directory(), file.path));
            fileItem->setData(0, kRoleExcluded, file.excluded);
            if (file.excluded)
                fileItem->setForeground(0, QColor(0x90, 0x90, 0x90));
            fileItem->setToolTip(0, file.path);
        }
    }
}

void ProjectTree::activateItem(QTreeWidgetItem* item)
{
    if (!item)
        return;

    const int kind = item->data(0, kRoleKind).toInt();
    if (kind == 2) {
        const QString path = item->data(0, kRolePath).toString();
        if (!path.isEmpty())
            emit fileActivated(path);
    }
}

void ProjectTree::showContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    QMenu menu(this);

    const int kind = item ? item->data(0, kRoleKind).toInt() : -1;

    QAction* open = menu.addAction(QStringLiteral("Open"));
    menu.addSeparator();
    QAction* fileOptions = menu.addAction(QStringLiteral("Options for File..."));
    QAction* targetOptions = menu.addAction(QStringLiteral("Options for Target..."));

    open->setEnabled(kind == 2);
    fileOptions->setEnabled(kind == 2);

    QAction* chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == open && kind == 2) {
        activateItem(item);
    } else if (chosen == fileOptions && kind == 2) {
        emit optionsForFileRequested(item->data(0, kRolePath).toString());
    } else if (chosen == targetOptions) {
        emit optionsForTargetRequested();
    }
}
