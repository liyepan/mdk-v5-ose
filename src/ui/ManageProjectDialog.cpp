#include "ui/ManageProjectDialog.h"

#include "core/Lang.h"
#include "ui/KeilStyle.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

// 扩展名 -> uVision 的 FileType
int fileTypeForPath(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("c"))
        return 1;
    if (suffix == QLatin1String("cpp") || suffix == QLatin1String("cc")
        || suffix == QLatin1String("cxx") || suffix == QLatin1String("h")
        || suffix == QLatin1String("hpp"))
        return 5;
    if (suffix == QLatin1String("s") || suffix == QLatin1String("asm"))
        return 2;
    if (suffix == QLatin1String("o") || suffix == QLatin1String("obj"))
        return 3;
    if (suffix == QLatin1String("lib") || suffix == QLatin1String("a"))
        return 7;
    return 5;
}

// 绝对路径 -> uVision 风格的相对路径（.\Src\main.c）
QString toUvprojPath(const QString& projectDir, const QString& absolute)
{
    QString rel = QDir(projectDir).relativeFilePath(absolute);
    rel.replace(QLatin1Char('/'), QLatin1Char('\\'));
    if (!rel.startsWith(QLatin1Char('.')))
        rel.prepend(QStringLiteral(".\\"));
    return rel;
}

} // namespace

ManageProjectDialog::ManageProjectDialog(const Target& target, const QString& projectDir,
                                         QWidget* parent)
    : QDialog(parent)
    , m_projectDir(projectDir)
    , m_groups(target.groups)
{
    setWindowTitle(Lang::t("Manage Project Items"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* hint = new QLabel(
        QStringLiteral("%1 — %2").arg(target.name,
                                      Lang::t("Groups and files of this target")));
    hint->setStyleSheet(QStringLiteral("color: #5b6270;"));
    layout->addWidget(hint);

    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setEditTriggers(QAbstractItemView::DoubleClicked);
    m_tree->setColumnCount(2);
    m_tree->setColumnHidden(1, true);      // 第二列偷偷存文件路径
    layout->addWidget(m_tree, 1);

    auto* buttons = new QHBoxLayout;
    m_btnNewGroup = new QPushButton(Lang::t("New Group"));
    m_btnAddFiles = new QPushButton(Lang::t("Add Files..."));
    m_btnRemove = new QPushButton(Lang::t("Remove"));
    m_btnUp = new QPushButton(Lang::t("Move Up"));
    m_btnDown = new QPushButton(Lang::t("Move Down"));

    buttons->addWidget(m_btnNewGroup);
    buttons->addWidget(m_btnAddFiles);
    buttons->addWidget(m_btnRemove);
    buttons->addStretch(1);
    buttons->addWidget(m_btnUp);
    buttons->addWidget(m_btnDown);
    layout->addLayout(buttons);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(box);

    connect(box, &QDialogButtonBox::accepted, this, &ManageProjectDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_btnNewGroup, &QPushButton::clicked, this, &ManageProjectDialog::newGroup);
    connect(m_btnAddFiles, &QPushButton::clicked, this, &ManageProjectDialog::addFiles);
    connect(m_btnRemove, &QPushButton::clicked, this, &ManageProjectDialog::removeSelected);
    connect(m_btnUp, &QPushButton::clicked, this, [this]() { moveSelected(-1); });
    connect(m_btnDown, &QPushButton::clicked, this, [this]() { moveSelected(1); });
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this,
            &ManageProjectDialog::refreshButtons);
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int) {
        // 组名就地改过之后同步回数据
        if (item->parent())
            return;
        const int index = m_tree->indexOfTopLevelItem(item);
        if (index >= 0 && index < m_groups.size())
            m_groups[index].name = item->text(0);
    });

    buildTree();
    refreshButtons();
    resize(680, 460);
}

void ManageProjectDialog::buildTree()
{
    m_tree->clear();
    for (const ProjGroup& group : m_groups) {
        auto* groupItem = new QTreeWidgetItem(m_tree);
        groupItem->setText(0, group.name);
        groupItem->setIcon(0, KeilStyle::icon(QStringLiteral("folder"), 16));
        groupItem->setFlags(groupItem->flags() | Qt::ItemIsEditable);
        groupItem->setExpanded(true);

        for (const ProjFile& file : group.files) {
            auto* fileItem = new QTreeWidgetItem(groupItem);
            fileItem->setText(0, file.name);
            fileItem->setText(1, file.path);
            fileItem->setToolTip(0, file.path);

            QString iconName = QStringLiteral("file_h");
            switch (file.kind()) {
            case FileKind::CSource:   iconName = QStringLiteral("file_c"); break;
            case FileKind::CppSource: iconName = QStringLiteral("file_cpp"); break;
            case FileKind::Assembly:  iconName = QStringLiteral("file_asm"); break;
            default:                  iconName = QStringLiteral("file_h"); break;
            }
            fileItem->setIcon(0, KeilStyle::icon(iconName, 16));
        }
    }
}

QTreeWidgetItem* ManageProjectDialog::currentGroupItem() const
{
    QTreeWidgetItem* item = m_tree->currentItem();
    while (item && item->parent())
        item = item->parent();
    return item;
}

void ManageProjectDialog::refreshButtons()
{
    QTreeWidgetItem* item = m_tree->currentItem();
    m_btnRemove->setEnabled(item != nullptr);
    m_btnUp->setEnabled(item != nullptr);
    m_btnDown->setEnabled(item != nullptr);
    m_btnAddFiles->setEnabled(currentGroupItem() != nullptr);
}

void ManageProjectDialog::newGroup()
{
    QString name = Lang::t("NewGroup");
    int n = 1;
    QStringList existing;
    for (const ProjGroup& g : m_groups)
        existing << g.name;
    while (existing.contains(name))
        name = QStringLiteral("NewGroup%1").arg(++n);

    m_groups.append(ProjGroup{ name, false, false, QString(), QString(), QString(), {} });
    buildTree();

    QTreeWidgetItem* item = m_tree->topLevelItem(m_groups.size() - 1);
    if (item) {
        m_tree->setCurrentItem(item);
        m_tree->editItem(item);
    }
}

void ManageProjectDialog::addFiles()
{
    QTreeWidgetItem* groupItem = currentGroupItem();
    if (!groupItem)
        return;
    const int groupIndex = m_tree->indexOfTopLevelItem(groupItem);
    if (groupIndex < 0 || groupIndex >= m_groups.size())
        return;

    const QStringList paths = QFileDialog::getOpenFileNames(
        this, Lang::t("Add Files"), m_projectDir,
        QStringLiteral("Source Files (*.c *.cpp *.s *.S *.h *.hpp *.lib);;All Files (*)"));
    if (paths.isEmpty())
        return;

    ProjGroup& group = m_groups[groupIndex];
    QStringList existing;
    for (const ProjFile& f : group.files)
        existing << f.path;

    for (const QString& path : paths) {
        const QString rel = toUvprojPath(m_projectDir, path);
        if (existing.contains(rel))
            continue;

        ProjFile pf;
        pf.name = QFileInfo(path).fileName();
        pf.path = rel;
        pf.fileType = fileTypeForPath(path);
        pf.groupName = group.name;
        group.files.append(pf);
    }

    buildTree();
    groupItem = m_tree->topLevelItem(groupIndex);
    if (groupItem)
        groupItem->setExpanded(true);
}

void ManageProjectDialog::removeSelected()
{
    QList<QTreeWidgetItem*> items = m_tree->selectedItems();
    if (items.isEmpty())
        return;

    // 组是整组删，会连带里面的文件，先问一句
    bool hasGroup = false;
    for (QTreeWidgetItem* item : items)
        if (!item->parent())
            hasGroup = true;

    if (hasGroup) {
        const auto answer = QMessageBox::question(
            this, Lang::t("Remove"),
            Lang::t("Remove the selected group(s) and all files inside them?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }

    // 组的索引会随着删除变化，先把组都删了再处理文件
    for (QTreeWidgetItem* item : items) {
        if (item->parent())
            continue;
        const int index = m_tree->indexOfTopLevelItem(item);
        if (index >= 0 && index < m_groups.size())
            m_groups.removeAt(index);
        delete item;
    }

    for (QTreeWidgetItem* item : items) {
        if (!item->parent())
            continue;
        QTreeWidgetItem* parent = item->parent();
        const int groupIndex = m_tree->indexOfTopLevelItem(parent);
        const int fileIndex = parent->indexOfChild(item);
        if (groupIndex >= 0 && groupIndex < m_groups.size() && fileIndex >= 0
            && fileIndex < m_groups[groupIndex].files.size())
            m_groups[groupIndex].files.removeAt(fileIndex);
        delete item;
    }

    refreshButtons();
}

void ManageProjectDialog::moveSelected(int delta)
{
    QTreeWidgetItem* item = m_tree->currentItem();
    if (!item)
        return;

    if (!item->parent()) {
        const int index = m_tree->indexOfTopLevelItem(item);
        const int target = index + delta;
        if (index < 0 || target < 0 || target >= m_groups.size())
            return;
        m_groups.move(index, target);
        buildTree();
        m_tree->setCurrentItem(m_tree->topLevelItem(target));
        return;
    }

    QTreeWidgetItem* parent = item->parent();
    const int groupIndex = m_tree->indexOfTopLevelItem(parent);
    const int index = parent->indexOfChild(item);
    const int target = index + delta;
    if (groupIndex < 0 || groupIndex >= m_groups.size() || index < 0 || target < 0
        || target >= m_groups[groupIndex].files.size())
        return;

    m_groups[groupIndex].files.move(index, target);
    buildTree();
    QTreeWidgetItem* groupItem = m_tree->topLevelItem(groupIndex);
    if (groupItem)
        m_tree->setCurrentItem(groupItem->child(target));
}

void ManageProjectDialog::accept()
{
    // 空名字兜一下，别把工程写成无名的组
    for (ProjGroup& group : m_groups) {
        group.name = group.name.trimmed();
        if (group.name.isEmpty())
            group.name = Lang::t("NewGroup");
        for (ProjFile& file : group.files)
            file.groupName = group.name;
    }

    QDialog::accept();
}
