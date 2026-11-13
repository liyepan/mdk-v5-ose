#include "ui/ManageProjectDialog.h"

#include "core/Lang.h"
#include "ui/KeilStyle.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QToolButton>
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

QString toUvprojPath(const QString& projectDir, const QString& absolute)
{
    QString rel = QDir(projectDir).relativeFilePath(absolute);
    rel.replace(QLatin1Char('/'), QLatin1Char('\\'));
    if (!rel.startsWith(QLatin1Char('.')))
        rel.prepend(QStringLiteral(".\\"));
    return rel;
}

QString iconNameFor(const ProjFile& file)
{
    switch (file.kind()) {
    case FileKind::CSource:   return QStringLiteral("file_c");
    case FileKind::CppSource: return QStringLiteral("file_cpp");
    case FileKind::Assembly:  return QStringLiteral("file_asm");
    default:                  return QStringLiteral("file_h");
    }
}

// 小方块按钮：+ / x / ^ / v，和 uVision 那排一样大
QToolButton* makeMiniButton(const QString& text, const QString& tip)
{
    auto* button = new QToolButton;
    button->setText(text);
    button->setToolTip(tip);
    button->setFixedSize(20, 20);
    button->setAutoRaise(true);
    return button;
}

} // namespace

ManageProjectDialog::ManageProjectDialog(const QList<Target>& targets, int currentTarget,
                                         const QString& projectDir, QWidget* parent)
    : QDialog(parent)
    , m_targets(targets)
    , m_current(qBound(0, currentTarget, qMax(0, targets.size() - 1)))
    , m_projectDir(projectDir)
{
    setWindowTitle(Lang::t("Manage Project Items"));
    setModal(true);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(8);

    // ---- 页签：只做第一页，其它三页照 uVision 列着但先禁用 ----
    m_tabs = new QTabWidget;
    m_tabs->setDocumentMode(false);

    auto* page = new QWidget;
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(10, 10, 10, 10);
    pageLayout->setSpacing(6);

    // ---- 三列：Targets / Groups / Files ----
    auto* columns = new QHBoxLayout;
    columns->setSpacing(10);

    auto makeColumn = [](const QString& title, QWidget*& host, QGridLayout*& grid) {
        host = new QWidget;
        auto* v = new QVBoxLayout(host);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(3);
        auto* label = new QLabel(title);
        v->addWidget(label);
        grid = new QGridLayout;
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(2);
        v->addLayout(grid);
        auto* list = new QListWidget;
        list->setUniformItemSizes(true);
        v->addWidget(list, 1);
    };

    QWidget* targetHost = nullptr;
    QWidget* groupHost = nullptr;
    QWidget* fileHost = nullptr;
    QGridLayout* targetBar = nullptr;
    QGridLayout* groupBar = nullptr;
    QGridLayout* fileBar = nullptr;

    makeColumn(Lang::t("Project Targets:"), targetHost, targetBar);
    makeColumn(Lang::t("Groups:"), groupHost, groupBar);
    makeColumn(Lang::t("Files:"), fileHost, fileBar);

    m_targetList = targetHost->findChild<QListWidget*>();
    m_groupList = groupHost->findChild<QListWidget*>();
    m_fileList = fileHost->findChild<QListWidget*>();

    columns->addWidget(targetHost, 1);
    columns->addWidget(groupHost, 1);
    columns->addWidget(fileHost, 1);

    // 每列上面那排小按钮（顺序和 uVision 一致：新建 / 删除 / 上移 / 下移）
    auto newButton = makeMiniButton(QStringLiteral("+"), Lang::t("New Group"));
    newButton->setToolTip(Lang::t("New Group"));
    auto removeButton = makeMiniButton(QStringLiteral("\u2715"), Lang::t("Remove"));
    removeButton->setToolTip(Lang::t("Remove"));
    auto upButton = makeMiniButton(QStringLiteral("\u2191"), Lang::t("Move Up"));
    upButton->setToolTip(Lang::t("Move Up"));
    auto downButton = makeMiniButton(QStringLiteral("\u2193"), Lang::t("Move Down"));
    downButton->setToolTip(Lang::t("Move Down"));

    // Targets 列：uVision 也能新建/删除 target，这里先只保留删除和排序？
    // 新建 target 需要整段复制 <Target>，本轮先不做，按钮禁用并给出说明
    auto* targetNew = makeMiniButton(QStringLiteral("+"), Lang::t("New target"));
    auto* targetRemove = makeMiniButton(QStringLiteral("\u2715"), Lang::t("Remove target"));
    auto* targetUp = makeMiniButton(QStringLiteral("\u2191"), Lang::t("Move Up"));
    auto* targetDown = makeMiniButton(QStringLiteral("\u2193"), Lang::t("Move Down"));
    targetBar->addWidget(targetNew, 0, 0);
    targetBar->addWidget(targetRemove, 0, 1);
    targetBar->addWidget(targetUp, 0, 2);
    targetBar->addWidget(targetDown, 0, 3);
    targetBar->setColumnStretch(4, 1);

    groupBar->addWidget(newButton, 0, 0);
    groupBar->addWidget(removeButton, 0, 1);
    groupBar->addWidget(upButton, 0, 2);
    groupBar->addWidget(downButton, 0, 3);
    groupBar->setColumnStretch(4, 1);

    // Files 列没有"新建"（文件是从磁盘加进来的），留着空位对齐
    auto* fileRemove = makeMiniButton(QStringLiteral("\u2715"), Lang::t("Remove"));
    auto* fileUp = makeMiniButton(QStringLiteral("\u2191"), Lang::t("Move Up"));
    auto* fileDown = makeMiniButton(QStringLiteral("\u2193"), Lang::t("Move Down"));
    fileBar->addWidget(fileRemove, 0, 1);
    fileBar->addWidget(fileUp, 0, 2);
    fileBar->addWidget(fileDown, 0, 3);
    fileBar->setColumnStretch(4, 1);

    pageLayout->addLayout(columns, 1);

    // ---- 列表下面的两个按钮 ----
    auto* bottomRow = new QHBoxLayout;
    auto* setCurrent = new QPushButton(Lang::t("Set as Current Target"));
    auto* addFiles = new QPushButton(Lang::t("Add Files..."));
    bottomRow->addSpacing(2);
    bottomRow->addWidget(setCurrent);
    bottomRow->addStretch(1);
    bottomRow->addWidget(addFiles);
    bottomRow->addSpacing(2);
    pageLayout->addLayout(bottomRow);

    m_tabs->addTab(page, Lang::t("Project Items"));
    for (const QString& name : { QStringLiteral("Folders/Extensions"), QStringLiteral("Books"),
                                 QStringLiteral("Project Info/Layer") }) {
        auto* placeholder = new QWidget;
        auto* v = new QVBoxLayout(placeholder);
        auto* hint = new QLabel(QStringLiteral("%1 \u2014 %2")
                                    .arg(name, Lang::t("Not implemented yet")));
        hint->setAlignment(Qt::AlignCenter);
        hint->setStyleSheet(QStringLiteral("color: #8a8a8a;"));
        v->addWidget(hint);
        const int index = m_tabs->addTab(placeholder, name);
        m_tabs->setTabEnabled(index, false);
    }

    outer->addWidget(m_tabs, 1);

    // ---- OK / Cancel / Help ----
    auto* box = new QDialogButtonBox;
    box->addButton(QDialogButtonBox::Ok);
    box->addButton(QDialogButtonBox::Cancel);
    QPushButton* helpButton = box->addButton(Lang::t("Help"), QDialogButtonBox::HelpRole);
    outer->addWidget(box);

    connect(box, &QDialogButtonBox::accepted, this, &ManageProjectDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(helpButton, &QPushButton::clicked, this, [this]() {
        QMessageBox::information(
            this, Lang::t("Help"),
            QStringLiteral("左边选 Target，中间选分组，右边是这个分组的文件。\n"
                           "组上面的 + / ✕ / ↑ / ↓ 用来新建、删除、排序分组；\n"
                           "文件用「Add Files...」加进来，✕ / ↑ / ↓ 调整顺序。\n"
                           "「Set as Current Target」把当前选中的 target 设为活动配置。"));
    });

    // ---- Target 那一列 ----
    connect(targetNew, &QToolButton::clicked, this, [this]() {
        if (m_targets.isEmpty())
            return;
        const int row = qMax(0, m_targetList->currentRow());
        const QString source = m_targets.at(row).name;

        QString name = source + QStringLiteral("_copy");
        int n = 1;
        while (true) {
            bool taken = false;
            for (const Target& t : m_targets)
                if (t.name == name)
                    taken = true;
            if (!taken)
                break;
            name = source + QStringLiteral("_copy%1").arg(++n);
        }

        Target copy = m_targets.at(row);
        copy.name = name;
        m_targets.insert(row + 1, copy);
        m_cloneSources.insert(name, source);

        rebuildTargetList(row + 1);
    });
    connect(targetRemove, &QToolButton::clicked, this, [this]() {
        const int row = m_targetList->currentRow();
        if (row < 0 || m_targets.size() <= 1)
            return;
        const auto answer = QMessageBox::question(
            this, Lang::t("Remove"),
            QStringLiteral("删除 Target \"%1\"？").arg(m_targets.at(row).name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
        m_targets.removeAt(row);
        rebuildTargetList(qMax(0, row - 1));
    });
    connect(targetUp, &QToolButton::clicked, this, [this]() {
        const int row = m_targetList->currentRow();
        if (row <= 0)
            return;
        m_targets.move(row, row - 1);
        rebuildTargetList(row - 1);
    });
    connect(targetDown, &QToolButton::clicked, this, [this]() {
        const int row = m_targetList->currentRow();
        if (row < 0 || row + 1 >= m_targets.size())
            return;
        m_targets.move(row, row + 1);
        rebuildTargetList(row + 1);
    });

    connect(newButton, &QToolButton::clicked, this, &ManageProjectDialog::newGroup);
    connect(removeButton, &QToolButton::clicked, this, &ManageProjectDialog::removeGroup);
    connect(upButton, &QToolButton::clicked, this, [this]() { moveGroup(-1); });
    connect(downButton, &QToolButton::clicked, this, [this]() { moveGroup(1); });
    connect(fileRemove, &QToolButton::clicked, this, &ManageProjectDialog::removeFile);
    connect(fileUp, &QToolButton::clicked, this, [this]() { moveFile(-1); });
    connect(fileDown, &QToolButton::clicked, this, [this]() { moveFile(1); });
    connect(addFiles, &QPushButton::clicked, this, &ManageProjectDialog::addFiles);
    connect(setCurrent, &QPushButton::clicked, this, [this]() {
        if (m_targetList->currentRow() >= 0)
            m_current = m_targetList->currentRow();
        refreshButtons();
    });

    connect(m_targetList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0)
            m_current = row;
        m_group = -1;
        rebuildGroupList(0);
        refreshButtons();
    });
    connect(m_groupList, &QListWidget::currentRowChanged, this, [this](int row) {
        m_group = row;
        rebuildFileList(0);
        refreshButtons();
    });
    connect(m_groupList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem*) { renameGroup(); });
    connect(m_fileList, &QListWidget::itemSelectionChanged, this,
            &ManageProjectDialog::refreshButtons);
    connect(m_groupList, &QListWidget::itemSelectionChanged, this,
            &ManageProjectDialog::refreshButtons);

    for (const Target& target : m_targets)
        m_originalNames << target.name;

    rebuildTargetList(m_current);
    resize(820, 560);
}

Target* ManageProjectDialog::activeTarget()
{
    return (m_current >= 0 && m_current < m_targets.size()) ? &m_targets[m_current] : nullptr;
}

const Target* ManageProjectDialog::activeTarget() const
{
    return (m_current >= 0 && m_current < m_targets.size()) ? &m_targets.at(m_current) : nullptr;
}

int ManageProjectDialog::activeGroupIndex() const
{
    const Target* target = activeTarget();
    if (!target || m_group < 0 || m_group >= target->groups.size())
        return -1;
    return m_group;
}

void ManageProjectDialog::rebuildTargetList(int selectTarget)
{
    m_targetList->clear();
    for (const Target& target : m_targets) {
        auto* item = new QListWidgetItem(target.name, m_targetList);
        item->setIcon(KeilStyle::icon(QStringLiteral("target"), 16));
    }
    if (!m_targets.isEmpty())
        m_targetList->setCurrentRow(qBound(0, selectTarget, m_targets.size() - 1));
}

void ManageProjectDialog::rebuildGroupList(int selectGroup)
{
    m_groupList->clear();
    const Target* target = activeTarget();
    if (!target)
        return;

    for (const ProjGroup& group : target->groups)
        new QListWidgetItem(KeilStyle::icon(QStringLiteral("folder"), 16), group.name, m_groupList);

    if (!target->groups.isEmpty())
        m_groupList->setCurrentRow(qBound(0, selectGroup, target->groups.size() - 1));

    m_group = m_groupList->currentRow();
    rebuildFileList(0);
}

void ManageProjectDialog::rebuildFileList(int selectFile)
{
    m_fileList->clear();
    const Target* target = activeTarget();
    const int groupIndex = activeGroupIndex();
    if (!target || groupIndex < 0)
        return;

    const ProjGroup& group = target->groups.at(groupIndex);
    for (const ProjFile& file : group.files) {
        auto* item = new QListWidgetItem(KeilStyle::icon(iconNameFor(file), 16), file.name, m_fileList);
        item->setToolTip(file.path);
    }
    if (!group.files.isEmpty())
        m_fileList->setCurrentRow(qBound(0, selectFile, group.files.size() - 1));
}

void ManageProjectDialog::refreshButtons()
{
    // 这里只控制文件相关的按钮，分组按钮在选中 target 时就是可用的
    Q_UNUSED(this);
}

void ManageProjectDialog::newGroup()
{
    Target* target = activeTarget();
    if (!target)
        return;

    QListWidgetItem* item = new QListWidgetItem(KeilStyle::icon(QStringLiteral("folder"), 16),
                                                Lang::t("NewGroup"), m_groupList);
    const int index = target->groups.size();
    target->groups.append(ProjGroup{ Lang::t("NewGroup"), false, false, QString(), QString(), QString(), {} });
    m_groupList->setCurrentItem(item);
    m_group = index;

    bool ok = false;
    const QString name = QInputDialog::getText(this, Lang::t("New Group"),
                                               Lang::t("Group name:"), QLineEdit::Normal,
                                               Lang::t("NewGroup"), &ok);
    if (ok && !name.trimmed().isEmpty()) {
        target->groups[index].name = name.trimmed();
        item->setText(name.trimmed());
    }
    rebuildFileList(0);
}

void ManageProjectDialog::removeGroup()
{
    Target* target = activeTarget();
    const int index = activeGroupIndex();
    if (!target || index < 0)
        return;

    const auto answer = QMessageBox::question(
        this, Lang::t("Remove"),
        Lang::t("Remove the selected group(s) and all files inside them?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    target->groups.removeAt(index);
    rebuildGroupList(qMax(0, index - 1));
}

void ManageProjectDialog::moveGroup(int delta)
{
    Target* target = activeTarget();
    const int index = activeGroupIndex();
    if (!target || index < 0)
        return;
    const int targetIndex = index + delta;
    if (targetIndex < 0 || targetIndex >= target->groups.size())
        return;

    target->groups.move(index, targetIndex);
    rebuildGroupList(targetIndex);
}

void ManageProjectDialog::renameGroup()
{
    Target* target = activeTarget();
    const int index = activeGroupIndex();
    QListWidgetItem* item = m_groupList->currentItem();
    if (!target || index < 0 || !item)
        return;

    bool ok = false;
    const QString name = QInputDialog::getText(this, Lang::t("New Group"),
                                               Lang::t("Group name:"), QLineEdit::Normal,
                                               target->groups.at(index).name, &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    target->groups[index].name = name.trimmed();
    item->setText(name.trimmed());
}

void ManageProjectDialog::addFiles()
{
    const int groupIndex = activeGroupIndex();
    Target* target = activeTarget();
    if (!target || groupIndex < 0)
        return;

    const QStringList paths = QFileDialog::getOpenFileNames(
        this, Lang::t("Add Files"), m_projectDir,
        QStringLiteral("Source Files (*.c *.cpp *.s *.S *.h *.hpp *.lib);;All Files (*)"));
    if (paths.isEmpty())
        return;

    ProjGroup& group = target->groups[groupIndex];
    QStringList existing;
    for (const ProjFile& file : group.files)
        existing << file.path;

    for (const QString& path : paths) {
        const QString rel = toUvprojPath(m_projectDir, path);
        if (existing.contains(rel))
            continue;
        existing << rel;

        ProjFile file;
        file.name = QFileInfo(path).fileName();
        file.path = rel;
        file.fileType = fileTypeForPath(path);
        file.groupName = group.name;
        group.files.append(file);
    }

    rebuildFileList(group.files.size() - 1);
}

void ManageProjectDialog::removeFile()
{
    Target* target = activeTarget();
    const int groupIndex = activeGroupIndex();
    const int row = m_fileList->currentRow();
    if (!target || groupIndex < 0 || row < 0)
        return;

    ProjGroup& group = target->groups[groupIndex];
    if (row >= group.files.size())
        return;

    group.files.removeAt(row);
    rebuildFileList(qMax(0, row - 1));
}

void ManageProjectDialog::moveFile(int delta)
{
    Target* target = activeTarget();
    const int groupIndex = activeGroupIndex();
    const int row = m_fileList->currentRow();
    if (!target || groupIndex < 0 || row < 0)
        return;

    ProjGroup& group = target->groups[groupIndex];
    const int targetRow = row + delta;
    if (targetRow < 0 || targetRow >= group.files.size())
        return;

    group.files.move(row, targetRow);
    rebuildFileList(targetRow);
}

void ManageProjectDialog::accept()
{
    for (Target& target : m_targets) {
        for (ProjGroup& group : target.groups) {
            group.name = group.name.trimmed();
            if (group.name.isEmpty())
                group.name = Lang::t("NewGroup");
            for (ProjFile& file : group.files)
                file.groupName = group.name;
        }
    }
    QDialog::accept();
}
