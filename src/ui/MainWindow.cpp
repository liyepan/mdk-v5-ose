#include "ui/MainWindow.h"
#include "core/Lang.h"

#include "core/Formatter.h"
#include "core/Settings.h"
#include "core/UvprojReader.h"
#include "ui/BuildOutput.h"
#include "ui/CodeEditor.h"
#include "ui/KeilStyle.h"
#include "ui/OptionsDialog.h"
#include "ui/ProjectTree.h"

#include <QAction>
#include <QActionGroup>
#include <QPainter>
#include <QTabBar>
#include <QToolButton>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>
#include <QLayout>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextStream>
#include <QThread>
#include <QToolBar>
#include <QToolButton>

namespace {

// QTabWidget::setTabBar 是 protected，这里暴露出来
class FileTabWidget : public QTabWidget
{
public:
    explicit FileTabWidget(QWidget* parent = nullptr)
        : QTabWidget(parent)
    {
    }

    using QTabWidget::setTabBar;
};

// 每个页签顶部一条彩色条，颜色按文件分配，这样一堆标签一眼能分出来
class FileTabBar : public QTabBar
{
public:
    explicit FileTabBar(QWidget* parent = nullptr)
        : QTabBar(parent)
    {
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QTabBar::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);

        for (int i = 0; i < count(); ++i) {
            const QColor color = tabData(i).value<QColor>();
            if (!color.isValid())
                continue;

            const QRect rect = tabRect(i);
            if (!rect.isValid())
                continue;

            QColor accent = color;
            accent.setAlpha(i == currentIndex() ? 255 : 110);
            painter.fillRect(QRect(rect.left() + 7, rect.top() + 1, rect.width() - 14, 3),
                             accent);
        }
    }
};

const char* kTemplateXml = R"XML(<?xml version="1.0" encoding="UTF-8" standalone="no" ?>
<Project xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="project_projx.xsd">
  <SchemaVersion>2.1</SchemaVersion>
  <Header>### uVision Project, (C) Keil Software</Header>
  <Targets>
    <Target>
      <TargetName>Target 1</TargetName>
      <ToolsetNumber>0x4</ToolsetNumber>
      <ToolsetName>ARM-ADS</ToolsetName>
      <uAC6>0</uAC6>
      <TargetOption>
        <TargetCommonOption>
          <Device></Device>
          <Vendor></Vendor>
          <PackID></PackID>
          <Cpu>IRAM(0x20000000,0x5000) IROM(0x8000000,0x10000) CLOCK(8000000) CPUTYPE("Cortex-M3")</Cpu>
          <OutputDirectory>.\Objects\</OutputDirectory>
          <OutputName>Project</OutputName>
          <CreateExecutable>1</CreateExecutable>
          <CreateLib>0</CreateLib>
          <CreateHexFile>1</CreateHexFile>
          <DebugInformation>1</DebugInformation>
          <BrowseInformation>1</BrowseInformation>
          <ListingPath>.\Listings\</ListingPath>
        </TargetCommonOption>
        <TargetArmAds>
          <ArmAdsMisc>
            <useUlib>0</useUlib>
          </ArmAdsMisc>
          <Cads>
            <interw>1</interw>
            <Optim>2</Optim>
            <oTime>0</oTime>
            <SplitLS>0</SplitLS>
            <OneElfS>0</OneElfS>
            <Strict>0</Strict>
            <EnumInt>0</EnumInt>
            <PlainCh>0</PlainCh>
            <wLevel>2</wLevel>
            <uC99>1</uC99>
            <VariousControls>
              <MiscControls></MiscControls>
              <Define></Define>
              <Undefine></Undefine>
              <IncludePath>.\Inc</IncludePath>
            </VariousControls>
          </Cads>
          <Aads>
            <VariousControls>
              <MiscControls></MiscControls>
              <Define></Define>
              <Undefine></Undefine>
              <IncludePath>.\Inc</IncludePath>
            </VariousControls>
          </Aads>
          <LDads>
            <umfTarg>1</umfTarg>
            <noStLib>0</noStLib>
            <TextAddressRange>0x08000000</TextAddressRange>
            <DataAddressRange>0x20000000</DataAddressRange>
            <ScatterFile></ScatterFile>
            <Misc></Misc>
          </LDads>
        </TargetArmAds>
      </TargetOption>
      <Groups>
        <Group>
          <GroupName>Source Group 1</GroupName>
          <Files>
          </Files>
        </Group>
      </Groups>
    </Target>
  </Targets>
</Project>
)XML";

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowIcon(KeilStyle::icon(QStringLiteral("target"), 32));

    auto* tabs = new FileTabWidget(this);
    m_tabs = tabs;
    tabs->setTabBar(new FileTabBar(tabs));
    m_tabs->setIconSize(QSize(12, 12));
    // 用 Qt 自带的关闭按钮（位置由 QTabBar 自己算，天然居中），
    // 图标已经在 AppStyle::standardIcon 里换成我们画的细叉号
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    setCentralWidget(m_tabs);

    m_engine = new BuildEngine(this);

    createActions();
    createMainToolbarActions();
    createMenus();
    createToolBars();
    createDocks();
    createStatusBar();

    connect(m_engine, &BuildEngine::output, this, &MainWindow::onEngineOutput);
    connect(m_engine, &BuildEngine::diagnostic, this, &MainWindow::onEngineDiagnostic);
    connect(m_engine, &BuildEngine::finished, this, &MainWindow::onEngineFinished);
    connect(m_engine, &BuildEngine::status, this, [this](const QString& text) {
        m_statusMessage->setText(text);
    });
    connect(m_engine, &BuildEngine::mapFileReady, this, [this](const QString& path) {
        m_buildOutput->showMap(path);
    });

    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::onTabCloseRequested);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) { updateEncodingLabel(); });

    m_engine->setJobs(Settings::instance().buildJobs());

    reloadToolchain();
    restoreWindowState();
    applyToolBarLayout();     // restoreState 可能把工具栏摆回旧布局，这里纠正
    updateWindowTitle();

    setBusy(false);
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------- 界面搭建

void MainWindow::createActions()
{
    m_actNewFile = new QAction(KeilStyle::icon(QStringLiteral("new")),
                               Lang::t("New File"), this);
    m_actNewFile->setShortcut(QKeySequence::New);
    m_actNewFile->setToolTip(Lang::t("New File"));
    connect(m_actNewFile, &QAction::triggered, this, &MainWindow::onNewFile);

    m_actOpenFile = new QAction(KeilStyle::icon(QStringLiteral("open")),
                                Lang::t("Open File"), this);
    m_actOpenFile->setShortcut(QKeySequence::Open);
    m_actOpenFile->setToolTip(Lang::t("Open File"));
    connect(m_actOpenFile, &QAction::triggered, this, &MainWindow::onOpenFile);

    m_actBatchBuild = new QAction(KeilStyle::icon(QStringLiteral("batchbuild")),
                                  Lang::t("Batch Build"), this);
    m_actBatchBuild->setEnabled(false);
    m_actBatchBuild->setToolTip(QStringLiteral("Batch Build (not implemented)"));

    m_actBuild = new QAction(KeilStyle::icon(QStringLiteral("build")),
                             Lang::t("Build Target"), this);
    m_actBuild->setShortcut(QKeySequence(Qt::Key_F7));
    m_actBuild->setToolTip(Lang::t("Build target files (F7)"));
    connect(m_actBuild, &QAction::triggered, this, &MainWindow::onBuild);

    m_actRebuild = new QAction(KeilStyle::icon(QStringLiteral("rebuild")),
                               Lang::t("Rebuild all target files"), this);
    m_actRebuild->setToolTip(Lang::t("Rebuild all target files"));
    connect(m_actRebuild, &QAction::triggered, this, &MainWindow::onRebuild);

    m_actTranslate = new QAction(KeilStyle::icon(QStringLiteral("translate")),
                                 Lang::t("Translate"), this);
    m_actTranslate->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_F7));
    m_actTranslate->setToolTip(Lang::t("Translate current file (Ctrl+F7)"));
    connect(m_actTranslate, &QAction::triggered, this, &MainWindow::onTranslate);

    m_actClean = new QAction(KeilStyle::icon(QStringLiteral("clean")),
                             Lang::t("Clean targets"), this);
    m_actClean->setToolTip(Lang::t("Clean targets"));
    connect(m_actClean, &QAction::triggered, this, &MainWindow::onClean);

    m_actStop = new QAction(KeilStyle::icon(QStringLiteral("stop")),
                            Lang::t("Stop build"), this);
    m_actStop->setToolTip(Lang::t("Stop build"));
    connect(m_actStop, &QAction::triggered, this, &MainWindow::onStopBuild);

    m_actSave = new QAction(KeilStyle::icon(QStringLiteral("save")),
                            Lang::t("Save"), this);
    m_actSave->setShortcut(QKeySequence::Save);
    connect(m_actSave, &QAction::triggered, this, &MainWindow::onSave);

    m_actSaveAll = new QAction(KeilStyle::icon(QStringLiteral("saveall")),
                               Lang::t("Save All"), this);
    connect(m_actSaveAll, &QAction::triggered, this, &MainWindow::onSaveAll);

    m_actOptionsTarget = new QAction(KeilStyle::icon(QStringLiteral("wand")),
                                     Lang::t("Options for Target..."), this);
    m_actOptionsTarget->setToolTip(Lang::t("Options for Target..."));
    m_actOptionsTarget->setShortcut(QKeySequence(Qt::ALT + Qt::Key_F7));
    connect(m_actOptionsTarget, &QAction::triggered, this, &MainWindow::onOptionsForTarget);

    m_actCloseProject = new QAction(Lang::t("Close Project"), this);
    connect(m_actCloseProject, &QAction::triggered, this, &MainWindow::onCloseProject);

    m_actDownload = new QAction(KeilStyle::icon(QStringLiteral("download")),
                                Lang::t("Download to Flash"), this);
    m_actDownload->setToolTip(Lang::t("Download to Flash"));
    m_actDownload->setShortcut(QKeySequence(Qt::Key_F8));
    connect(m_actDownload, &QAction::triggered, this, &MainWindow::onDownload);
}

void MainWindow::createMainToolbarActions()
{
    // 顺序完全照 uVision：nullptr 代表分隔线
    m_mainBarActions.clear();

    auto add = [this](const QString& icon, const QString& text, const QKeySequence& keys,
                      void (MainWindow::*handler)()) {
        auto* action = new QAction(KeilStyle::icon(icon), Lang::t(text.toUtf8().constData()), this);
        action->setToolTip(Lang::t(text.toUtf8().constData()));
        if (!keys.isEmpty())
            action->setShortcut(keys);
        connect(action, &QAction::triggered, this, handler);
        m_mainBarActions << action;
        return action;
    };

    // 文件
    m_mainBarActions << m_actNewFile;
    m_mainBarActions << m_actOpenFile;
    m_mainBarActions << m_actSave;
    m_mainBarActions << m_actSaveAll;
    m_mainBarActions << nullptr;

    // 剪贴板
    add(QStringLiteral("cut"), QStringLiteral("Cut"), QKeySequence::Cut, &MainWindow::onCut);
    add(QStringLiteral("copy"), QStringLiteral("Copy"), QKeySequence::Copy, &MainWindow::onCopy);
    add(QStringLiteral("paste"), QStringLiteral("Paste"), QKeySequence::Paste, &MainWindow::onPaste);
    m_mainBarActions << nullptr;

    // 撤销 / 恢复
    add(QStringLiteral("undo"), QStringLiteral("Undo"), QKeySequence::Undo, &MainWindow::onUndo);
    add(QStringLiteral("redo"), QStringLiteral("Redo"), QKeySequence::Redo, &MainWindow::onRedo);
    m_mainBarActions << nullptr;

    // 跳转历史
    add(QStringLiteral("back"), QStringLiteral("Navigate Back"),
        QKeySequence(Qt::ALT + Qt::Key_Left), &MainWindow::onNavigateBack);
    add(QStringLiteral("forward"), QStringLiteral("Navigate Forward"),
        QKeySequence(Qt::ALT + Qt::Key_Right), &MainWindow::onNavigateForward);
    m_mainBarActions << nullptr;

    // 鼠标模式（占位）
    auto* mouseMode = new QAction(KeilStyle::icon(QStringLiteral("mouse")),
                                  QStringLiteral("Mouse Mode"), this);
    mouseMode->setCheckable(true);
    mouseMode->setToolTip(QStringLiteral("Mouse Mode"));
    m_mainBarActions << mouseMode;
    m_mainBarActions << nullptr;

    // 书签
    add(QStringLiteral("bookmark_prev"), QStringLiteral("Previous Bookmark"), QKeySequence(),
        &MainWindow::onBookmarkPrev);
    add(QStringLiteral("bookmark_next"), QStringLiteral("Next Bookmark"), QKeySequence(),
        &MainWindow::onBookmarkNext);
    add(QStringLiteral("bookmark_delete"), QStringLiteral("Delete Bookmark"),
        QKeySequence(Qt::CTRL + Qt::Key_F2), &MainWindow::onBookmarkDelete);
    m_mainBarActions << nullptr;

    // 缩进 / 注释
    add(QStringLiteral("indent"), QStringLiteral("Indent Selection"),
        QKeySequence(Qt::Key_Tab), &MainWindow::onIndent);
    add(QStringLiteral("unindent"), QStringLiteral("Unindent Selection"),
        QKeySequence(Qt::SHIFT + Qt::Key_Tab), &MainWindow::onUnindent);
    add(QStringLiteral("comment"), QStringLiteral("Comment Selection"),
        QKeySequence(Qt::CTRL + Qt::Key_K), &MainWindow::onComment);
    add(QStringLiteral("uncomment"), QStringLiteral("Uncomment Selection"),
        QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_K), &MainWindow::onUncomment);
    m_mainBarActions << nullptr;

    // 搜索
    add(QStringLiteral("find_all"), QStringLiteral("Find in Files"),
        QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_F), &MainWindow::onFindInFiles);
    // 符号下拉框插在 Find Next 之前（uVision 就是这么排的）
    m_symbolComboAnchor = add(QStringLiteral("find"), QStringLiteral("Find Next"),
                              QKeySequence(Qt::Key_F3), &MainWindow::onFindNext);
    add(QStringLiteral("search"), QStringLiteral("Find"), QKeySequence::Find, &MainWindow::onFind);
    m_mainBarActions << nullptr;

    // 调试
    add(QStringLiteral("debug_start"), QStringLiteral("Start/Stop Debug Session"),
        QKeySequence(Qt::CTRL + Qt::Key_F5), &MainWindow::onDebugPlaceholder);
    m_mainBarActions << nullptr;

    // 断点
    add(QStringLiteral("breakpoint_insert"), QStringLiteral("Insert Breakpoint"),
        QKeySequence(Qt::Key_F9), &MainWindow::onBreakpointToggle);
    add(QStringLiteral("breakpoint_delete"), QStringLiteral("Delete Breakpoint"), QKeySequence(),
        &MainWindow::onBreakpointDelete);
    add(QStringLiteral("breakpoint_clear"), QStringLiteral("Delete All Breakpoints (Target)"),
        QKeySequence(), &MainWindow::onBreakpointClearTarget);
    add(QStringLiteral("breakpoint_clear_all"), QStringLiteral("Delete All Breakpoints (All)"),
        QKeySequence(), &MainWindow::onBreakpointClearAll);
    m_mainBarActions << nullptr;
}

void MainWindow::createMenus()
{
    QMenu* fileMenu = menuBar()->addMenu(Lang::t("&File"));
    fileMenu->addAction(m_actNewFile);
    fileMenu->addAction(m_actOpenFile);
    fileMenu->addSeparator();
    fileMenu->addAction(Lang::t("New Project..."), this, &MainWindow::onNewProject);
    fileMenu->addAction(Lang::t("Open Project..."), this, &MainWindow::onOpenProject);
    m_recentMenu = fileMenu->addMenu(Lang::t("Recent Projects"));
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::buildRecentMenu);
    fileMenu->addSeparator();
    fileMenu->addAction(m_actCloseProject);
    fileMenu->addSeparator();
    fileMenu->addAction(m_actSave);
    fileMenu->addAction(m_actSaveAll);
    fileMenu->addAction(Lang::t("Save As..."), this, &MainWindow::onSaveAs);
    fileMenu->addSeparator();
    fileMenu->addAction(Lang::t("Exit"), this, &QWidget::close);

    QMenu* editMenu = menuBar()->addMenu(Lang::t("&Edit"));
    QAction* undo = editMenu->addAction(Lang::t("Undo"));
    undo->setShortcut(QKeySequence::Undo);
    connect(undo, &QAction::triggered, this, [this]() {
        if (currentEditor())
            currentEditor()->undo();
    });
    QAction* redo = editMenu->addAction(Lang::t("Redo"));
    redo->setShortcut(QKeySequence::Redo);
    connect(redo, &QAction::triggered, this, [this]() {
        if (currentEditor())
            currentEditor()->redo();
    });
    editMenu->addSeparator();
    QAction* cut = editMenu->addAction(Lang::t("Cut"));
    cut->setShortcut(QKeySequence::Cut);
    connect(cut, &QAction::triggered, this, [this]() {
        if (currentEditor())
            currentEditor()->cut();
    });
    QAction* copy = editMenu->addAction(Lang::t("Copy"));
    copy->setShortcut(QKeySequence::Copy);
    connect(copy, &QAction::triggered, this, [this]() {
        if (currentEditor())
            currentEditor()->copy();
    });
    QAction* paste = editMenu->addAction(Lang::t("Paste"));
    paste->setShortcut(QKeySequence::Paste);
    connect(paste, &QAction::triggered, this, [this]() {
        if (currentEditor())
            currentEditor()->paste();
    });
    editMenu->addSeparator();
    QAction* find = editMenu->addAction(Lang::t("Find..."));
    find->setShortcut(QKeySequence::Find);
    find->setEnabled(false);
    editMenu->addAction(Lang::t("Go to Line..."), this, [this]() {
        bool ok = false;
        const int line = QInputDialog::getInt(this, Lang::t("Go to Line"),
                                              Lang::t("Line:"), 1, 1, 1 << 20, 1, &ok);
        if (ok && currentEditor())
            currentEditor()->gotoLine(line);
    });

    QMenu* viewMenu = menuBar()->addMenu(Lang::t("&View"));
    viewMenu->addAction(Lang::t("Project Window"), this, [this]() {
        m_projectDock->show();
        m_projectDock->raise();
    });
    viewMenu->addAction(Lang::t("Build Output Window"), this, [this]() {
        m_outputDock->show();
        m_outputDock->raise();
    });
    viewMenu->addAction(Lang::t("Error List Window"), this, [this]() {
        m_outputDock->show();
        m_buildOutput->showErrorList();
    });
    viewMenu->addSeparator();

    QAction* zoomIn = viewMenu->addAction(Lang::t("Zoom In"));
    zoomIn->setShortcuts({ QKeySequence::ZoomIn,
                           QKeySequence(Qt::CTRL + Qt::Key_Plus),
                           QKeySequence(Qt::CTRL + Qt::Key_Equal) });
    connect(zoomIn, &QAction::triggered, this, [this]() { changeEditorZoom(1); });

    QAction* zoomOut = viewMenu->addAction(Lang::t("Zoom Out"));
    zoomOut->setShortcuts({ QKeySequence::ZoomOut,
                            QKeySequence(Qt::CTRL + Qt::Key_Minus) });
    connect(zoomOut, &QAction::triggered, this, [this]() { changeEditorZoom(-1); });

    QAction* zoomReset = viewMenu->addAction(Lang::t("Reset Zoom"));
    zoomReset->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_0));
    connect(zoomReset, &QAction::triggered, this, &MainWindow::resetEditorZoom);

    viewMenu->addSeparator();
    for (const QString& name : { Lang::t("Functions Window"),
                                 Lang::t("Source Browser"),
                                 Lang::t("Books Window") }) {
        QAction* a = viewMenu->addAction(name);
        a->setEnabled(false);
        a->setToolTip(QStringLiteral("尚未实现"));
    }

    QMenu* projectMenu = menuBar()->addMenu(Lang::t("&Project"));
    projectMenu->addAction(m_actOptionsTarget);
    projectMenu->addSeparator();
    projectMenu->addAction(m_actBuild);
    projectMenu->addAction(m_actRebuild);
    projectMenu->addAction(m_actTranslate);
    projectMenu->addAction(m_actClean);
    projectMenu->addSeparator();
    projectMenu->addAction(m_actCloseProject);

    QMenu* flashMenu = menuBar()->addMenu(Lang::t("&Flash"));
    flashMenu->addAction(m_actDownload);
    QAction* erase = flashMenu->addAction(Lang::t("Erase"));
    erase->setEnabled(false);
    flashMenu->addSeparator();
    flashMenu->addAction(Lang::t("Configure Toolchain..."),
                         this, &MainWindow::onConfigureToolchain);

    QMenu* debugMenu = menuBar()->addMenu(Lang::t("&Debug"));
    QAction* startDebug = debugMenu->addAction(Lang::t("Start/Stop Debug Session"));
    startDebug->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_F5));
    connect(startDebug, &QAction::triggered, this, &MainWindow::onDebugPlaceholder);

    auto addDebugMenuAction = [this, debugMenu](const QString& text, const QKeySequence& keys) {
        QAction* a = debugMenu->addAction(text);
        if (!keys.isEmpty())
            a->setShortcut(keys);
        connect(a, &QAction::triggered, this, &MainWindow::onDebugPlaceholder);
        return a;
    };

    debugMenu->addSeparator();
    addDebugMenuAction(Lang::t("Run"), QKeySequence(Qt::Key_F5));
    addDebugMenuAction(Lang::t("Stop"), QKeySequence());
    addDebugMenuAction(Lang::t("Step"), QKeySequence(Qt::Key_F11));
    addDebugMenuAction(Lang::t("Step Over"), QKeySequence(Qt::Key_F10));
    addDebugMenuAction(Lang::t("Step Out"), QKeySequence(Qt::SHIFT + Qt::Key_F11));
    addDebugMenuAction(Lang::t("Run to Cursor Line"), QKeySequence(Qt::CTRL + Qt::Key_F10));
    debugMenu->addSeparator();
    for (const QString& name : { Lang::t("Breakpoints..."),
                                 Lang::t("Watch Windows"),
                                 Lang::t("Call Stack Window"),
                                 Lang::t("Register Window"),
                                 Lang::t("Memory Window") }) {
        QAction* a = debugMenu->addAction(name);
        a->setEnabled(false);
        a->setToolTip(QStringLiteral("尚未实现"));
    }

    QMenu* peripheralsMenu = menuBar()->addMenu(Lang::t("&Peripherals"));
    QMenu* systemViewer = peripheralsMenu->addMenu(Lang::t("System Viewer"));
    QAction* svNote = systemViewer->addAction(QStringLiteral("(需要调试会话，尚未实现)"));
    svNote->setEnabled(false);

    QMenu* toolsMenu = menuBar()->addMenu(Lang::t("&Tools"));
    toolsMenu->addAction(Lang::t("Configure Toolchain..."),
                         this, &MainWindow::onConfigureToolchain);

    // 并行编译任务数：Auto = 本机逻辑处理器数
    QMenu* jobsMenu = toolsMenu->addMenu(Lang::t("Parallel Build"));
    auto* jobsGroup = new QActionGroup(this);
    jobsGroup->setExclusive(true);

    auto addJobsAction = [this, jobsMenu, jobsGroup](const QString& text, int value) {
        QAction* a = jobsMenu->addAction(text);
        a->setCheckable(true);
        a->setData(value);
        jobsGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, value]() {
            const int n = (value <= 0) ? qMax(1, QThread::idealThreadCount()) : value;
            m_engine->setJobs(n);
            Settings::instance().setBuildJobs(n);
            Settings::instance().sync();
            updateCompilerStatus();
        });
    };

    addJobsAction(Lang::t("Auto  (%1 logical threads)")
                      .arg(QThread::idealThreadCount()), 0);
    jobsMenu->addSeparator();
    for (int n : { 1, 2, 4, 8, 16, 32, 64 })
        addJobsAction(QString::number(n), n);

    connect(jobsMenu, &QMenu::aboutToShow, this, [this, jobsGroup]() {
        for (QAction* a : jobsGroup->actions()) {
            const int v = a->data().toInt();
            const int eff = (v <= 0) ? qMax(1, QThread::idealThreadCount()) : v;
            a->setChecked(eff == m_engine->jobs());
        }
    });

    toolsMenu->addSeparator();
    QAction* formatDoc = toolsMenu->addAction(Lang::t("Format Document"));
    formatDoc->setShortcut(QKeySequence(Qt::SHIFT + Qt::ALT + Qt::Key_F));
    connect(formatDoc, &QAction::triggered, this, &MainWindow::onFormatDocument);

    QAction* formatOnSave = toolsMenu->addAction(Lang::t("Format on Save"));
    formatOnSave->setCheckable(true);
    formatOnSave->setChecked(Settings::instance().formatOnSave());
    connect(formatOnSave, &QAction::toggled, this, [](bool enabled) {
        Settings::instance().setFormatOnSave(enabled);
        Settings::instance().sync();
    });

    QAction* customTools = toolsMenu->addAction(Lang::t("Customize Tools Menu..."));
    customTools->setEnabled(false);

    QMenu* windowMenu = menuBar()->addMenu(Lang::t("&Window"));
    windowMenu->addAction(Lang::t("Close All"), this, [this]() {
        while (m_tabs->count() > 0)
            onTabCloseRequested(m_tabs->count() - 1);
    });
    windowMenu->addAction(Lang::t("Reset Window Layout"), this, [this]() {
        m_projectDock->show();
        m_outputDock->show();
        addDockWidget(Qt::LeftDockWidgetArea, m_projectDock);
        addDockWidget(Qt::BottomDockWidgetArea, m_outputDock);
        resizeDocks({ m_projectDock }, { 260 }, Qt::Horizontal);
        resizeDocks({ m_outputDock }, { 180 }, Qt::Vertical);
    });

    windowMenu->addSeparator();

    QMenu* languageMenu = windowMenu->addMenu(Lang::t("Language"));
    auto* langGroup = new QActionGroup(this);
    langGroup->setExclusive(true);

    auto addLangAction = [this, languageMenu, langGroup](const QString& text, Lang::Id id) {
        QAction* a = languageMenu->addAction(text);
        a->setCheckable(true);
        a->setChecked(Lang::current() == id);
        langGroup->addAction(a);
        connect(a, &QAction::triggered, this,
                [this, id]() { switchLanguage(static_cast<int>(id)); });
    };
    addLangAction(QStringLiteral("English"), Lang::Id::English);
    addLangAction(QStringLiteral("简体中文"), Lang::Id::Chinese);

    QMenu* helpMenu = menuBar()->addMenu(Lang::t("&Help"));
    helpMenu->addAction(Lang::t("About uVision5-OSE"), this, &MainWindow::onAbout);
}

void MainWindow::createToolBars()
{
    m_toolBars.clear();

    // uVision 的布局：三条独立横排、左对齐、每排自己一个拖动柄。
    // 用 addToolBarBreak() 保证每条占一行，而不是挤在同一行里。
    auto makeBar = [this](const QString& title) {
        auto* bar = new QToolBar(title, this);
        bar->setObjectName(title + QStringLiteral("Bar"));
        bar->setIconSize(QSize(16, 16));
        bar->setToolButtonStyle(Qt::ToolButtonIconOnly);
        bar->setMovable(false);       // 固定住：不给拖，也不给浮动
        bar->setFloatable(false);
        bar->setAllowedAreas(Qt::TopToolBarArea);
        m_toolBars.append(bar);

        addToolBar(bar);
        addToolBarBreak();
        return bar;
    };

    // ---- 第 1 排：完全按 uVision 的顺序 ----
    QToolBar* mainBar = makeBar(QStringLiteral("Main"));
    for (QAction* action : m_mainBarActions) {
        if (action && action == m_symbolComboAnchor) {
            m_symbolCombo = new QComboBox(mainBar);
            m_symbolCombo->setFixedWidth(170);          // 固定长度，不跟着符号名变长
            m_symbolCombo->setFixedHeight(21);
            m_symbolCombo->setEditable(true);           // 空的时候也能显示占位文字
            m_symbolCombo->setInsertPolicy(QComboBox::NoInsert);
            m_symbolCombo->lineEdit()->setPlaceholderText(Lang::t("Symbols"));
            m_symbolCombo->setToolTip(Lang::t("Symbols"));
            connect(m_symbolCombo, QOverload<int>::of(&QComboBox::activated),
                    this, &MainWindow::onSymbolChosen);
            mainBar->addWidget(m_symbolCombo);
            populateSymbolCombo();
        }
        if (!action)
            mainBar->addSeparator();
        else
            mainBar->addAction(action);
    }

    // ---- 第 2 排：检查 / 编译 / 重新编译 / 清理 / 下载 / Target / 魔术棒 / 工程管理 ----
    QToolBar* buildBar = makeBar(QStringLiteral("Build"));

    buildBar->addAction(m_actTranslate);      // 检查代码：编译当前文件
    buildBar->addAction(m_actBuild);          // 增量编译 F7
    buildBar->addAction(m_actRebuild);        // 重新编译
    buildBar->addAction(m_actClean);          // 清理
    buildBar->addSeparator();
    buildBar->addAction(m_actDownload);       // 下载到 Flash F8
    buildBar->addSeparator();

    m_targetCombo = new QComboBox(buildBar);
    m_targetCombo->setFixedWidth(150);
    m_targetCombo->setFixedHeight(21);
    m_targetCombo->setToolTip(Lang::t("Target"));
    connect(m_targetCombo, QOverload<int>::of(&QComboBox::activated), this,
            &MainWindow::onTargetChanged);
    buildBar->addWidget(m_targetCombo);

    buildBar->addAction(m_actOptionsTarget);  // 魔术棒
    buildBar->addSeparator();

    auto* manage = new QAction(KeilStyle::icon(QStringLiteral("manage")),
                               Lang::t("Manage Project Items"), this);
    manage->setToolTip(Lang::t("Manage Project Items"));
    connect(manage, &QAction::triggered, this, [this]() {
        QMessageBox::information(
            this, Lang::t("Manage Project Items"),
            QStringLiteral("工程的分组/文件增删还没实现，先在 Keil 里改 .uvprojx。"));
    });
    buildBar->addAction(manage);

    populateTargetCombo();
}

void MainWindow::applyToolBarLayout()
{
    if (m_toolBars.isEmpty())
        return;

    // 先全部摘下来（QMainWindow 的 saveState/restoreState 会把工具栏塞回旧位置，
    // 包括几次改版之前"三条挤一行"的布局），再按固定顺序两行摆回去
    for (QToolBar* bar : m_toolBars) {
        removeToolBarBreak(bar);
        removeToolBar(bar);
    }

    for (QToolBar* bar : m_toolBars) {
        addToolBar(Qt::TopToolBarArea, bar);
        addToolBarBreak();
        bar->setVisible(true);
        bar->setMovable(false);
        bar->setFloatable(false);
    }
}

void MainWindow::createDocks()
{
    m_projectDock = new QDockWidget(Lang::t("Project"), this);
    m_projectDock->setObjectName(QStringLiteral("projectDock"));
    // 可关闭、可拖动，但不许浮动（用户明确不要浮动窗口）
    m_projectDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    m_projectTree = new ProjectTree(m_projectDock);
    m_projectDock->setWidget(m_projectTree);
    addDockWidget(Qt::LeftDockWidgetArea, m_projectDock);

    connect(m_projectTree, &ProjectTree::targetChanged, this, &MainWindow::onTargetChanged);
    connect(m_projectTree, &ProjectTree::fileActivated, this, &MainWindow::onFileActivated);
    connect(m_projectTree, &ProjectTree::optionsForTargetRequested,
            this, &MainWindow::onOptionsForTarget);
    connect(m_projectTree, &ProjectTree::optionsForFileRequested,
            this, &MainWindow::onOptionsForFile);
    connect(m_projectTree, &ProjectTree::openWithEncodingRequested,
            this, &MainWindow::onOpenWithEncoding);

    m_outputDock = new QDockWidget(Lang::t("Build Output"), this);
    m_outputDock->setObjectName(QStringLiteral("outputDock"));
    m_outputDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    m_buildOutput = new BuildOutput(m_outputDock);
    m_outputDock->setWidget(m_buildOutput);
    addDockWidget(Qt::BottomDockWidgetArea, m_outputDock);

    connect(m_buildOutput, &BuildOutput::jumpRequested, this, &MainWindow::onJumpTo);

    resizeDocks({ m_projectDock }, { 260 }, Qt::Horizontal);
    resizeDocks({ m_outputDock }, { 180 }, Qt::Vertical);
}

void MainWindow::createStatusBar()
{
    m_statusMessage = new QLabel(Lang::t("Ready"), this);
    statusBar()->addWidget(m_statusMessage, 1);

    m_statusToolchain = new QLabel(this);
    statusBar()->addPermanentWidget(m_statusToolchain);

    m_statusEncoding = new QLabel(this);
    statusBar()->addPermanentWidget(m_statusEncoding);

    m_statusPosition = new QLabel(Lang::t("Ln 1, Col 1"), this);
    statusBar()->addPermanentWidget(m_statusPosition);
}

// ---------------------------------------------------------------- 工程

void MainWindow::onNewFile()
{
    static int untitledCounter = 0;

    auto* editor = new CodeEditor(m_tabs);
    connect(editor, &CodeEditor::cursorPositionChanged, this, &MainWindow::onCursorMoved);
    connect(editor, &CodeEditor::zoomRequested, this, &MainWindow::changeEditorZoom);
    connect(editor, &CodeEditor::wordActivated, this, &MainWindow::onWordActivated);
    connect(editor, &CodeEditor::includeActivated, this, &MainWindow::onIncludeActivated);
    connect(editor, &CodeEditor::encodingChanged, this,
            [this](const QString&) { updateEncodingLabel(); });

    if (m_editorZoom > 0.0)
        editor->setZoomPointSize(m_editorZoom);

    editor->setSymbolChecker([this](const QString& word) {
        ensureSymbolIndex();
        return !m_symbols.lookup(word).isEmpty();
    });

    const int index = m_tabs->addTab(editor, QStringLiteral("untitled%1").arg(++untitledCounter));
    m_tabs->setCurrentIndex(index);
    m_tabs->tabBar()->setTabData(index, nextTabColor());
    editor->setFocus();
    updateEncodingLabel();
}

void MainWindow::onOpenFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, Lang::t("Open File"),
        m_project.isValid() ? m_project.directory() : QString(),
        QStringLiteral("Source Files (*.c *.h *.cpp *.hpp *.s *.S);;All Files (*)"));
    if (!path.isEmpty())
        openFile(path);
}

void MainWindow::onSaveAs()
{
    CodeEditor* editor = currentEditor();
    if (!editor)
        return;

    QString suggested = editor->filePath();
    if (suggested.isEmpty() && m_project.isValid())
        suggested = m_project.directory() + QLatin1Char('/')
                    + m_tabs->tabText(m_tabs->currentIndex());

    const QString path = QFileDialog::getSaveFileName(this, Lang::t("Save As"), suggested,
                                                     QStringLiteral("C Source (*.c);;All Files (*)"));
    if (path.isEmpty())
        return;

    editor->setFilePath(path);
    if (!saveEditor(editor))
        return;

    const int index = m_tabs->indexOf(editor);
    if (index >= 0) {
        m_tabs->setTabText(index, QFileInfo(path).fileName());
        m_tabs->setTabToolTip(index, path);
    }
}

void MainWindow::onNewProject()
{
    const QString path = QFileDialog::getSaveFileName(
        this, Lang::t("New uVision Project"), QString(),
        Lang::t("uVision Project (*.uvprojx)"));
    if (path.isEmpty())
        return;

    if (!createProjectTemplate(path)) {
        QMessageBox::warning(this, Lang::t("New Project"),
                             QStringLiteral("无法创建工程文件"));
        return;
    }
    openProject(path);
}

bool MainWindow::createProjectTemplate(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    QTextStream ts(&f);
    ts.setCodec("UTF-8");
    ts << QString::fromUtf8(kTemplateXml);
    ts.flush();
    f.close();
    return true;
}

void MainWindow::onOpenProject()
{
    const QString path = QFileDialog::getOpenFileName(
        this, Lang::t("Open uVision Project"),
        m_project.isValid() ? m_project.directory() : QString(),
        Lang::t("uVision Project (*.uvprojx *.uvproj);;All Files (*)"));
    if (!path.isEmpty())
        openProject(path);
}

void MainWindow::openProject(const QString& path)
{
    UvProject project;
    QString error;
    if (!Uvproj::load(path, project, &error)) {
        QMessageBox::critical(this, Lang::t("Open Project"), error);
        return;
    }

    m_project = project;
    m_activeTarget = 0;

    Settings::instance().setLastProject(m_project.filePath);
    Settings::instance().addRecentProject(m_project.filePath);
    Settings::instance().sync();

    applyProject();
}

void MainWindow::applyProject()
{
    m_engine->setProject(m_project, m_activeTarget);

    m_projectTree->setProject(m_project, m_activeTarget);
    m_buildOutput->setProjectDir(m_project.directory());
    m_symbols.clear();
    m_navHistory.clear();
    m_navIndex = -1;
    populateTargetCombo();
    populateSymbolCombo();

    updateWindowTitle();
    setBusy(false);

    m_statusMessage->setText(Lang::t("Project '%1' loaded.")
                                 .arg(QFileInfo(m_project.filePath).fileName()));
}

void MainWindow::onCloseProject()
{
    while (m_tabs->count() > 0)
        onTabCloseRequested(m_tabs->count() - 1);

    m_project = UvProject();
    m_projectTree->clear();
    m_engine->setProject(m_project, 0);
    m_buildOutput->clear();
    m_buildOutput->clearDiagnostics();
    updateWindowTitle();
}

void MainWindow::onTargetChanged(int index)
{
    if (m_updatingTargets || index < 0)
        return;

    m_updatingTargets = true;
    m_activeTarget = index;
    m_symbols.clear();
    m_engine->setProject(m_project, m_activeTarget);

    // 工程树和工具栏的两个下拉框互相同步
    if (m_projectTree)
        m_projectTree->setCurrentTargetIndex(index);
    if (m_targetCombo && m_targetCombo->currentIndex() != index)
        m_targetCombo->setCurrentIndex(index);

    populateSymbolCombo();
    updateWindowTitle();
    m_updatingTargets = false;
}

void MainWindow::populateTargetCombo()
{
    if (!m_targetCombo)
        return;

    m_targetCombo->clear();
    for (const Target& target : m_project.targets)
        m_targetCombo->addItem(target.name);
    m_targetCombo->setCurrentIndex(m_project.isValid() ? m_activeTarget : -1);
}

void MainWindow::updateWindowTitle()
{
    QString title;
    if (m_project.isValid()) {
        const Target* t = m_project.target(m_activeTarget);
        title = QStringLiteral("%1 - %2").arg(m_project.baseName(), t ? t->name : QString());
    } else {
        title = Lang::t("No Project");
    }
    setWindowTitle(title + QStringLiteral(" - uVision5-OSE"));
}

// ---------------------------------------------------------------- 编辑器

void MainWindow::onFileActivated(const QString& path)
{
    openFile(path);
}

CodeEditor* MainWindow::openFile(const QString& absolutePath)
{
    const QString abs = QFileInfo(absolutePath).absoluteFilePath();

    for (int i = 0; i < m_tabs->count(); ++i) {
        auto* ed = qobject_cast<CodeEditor*>(m_tabs->widget(i));
        if (ed && ed->filePath() == abs) {
            m_tabs->setCurrentIndex(i);
            return ed;
        }
    }

    auto* editor = new CodeEditor(m_tabs);
    QString error;
    if (!editor->loadFile(abs, &error)) {
        QMessageBox::warning(this, Lang::t("Open File"),
                             QStringLiteral("无法打开 %1\n%2").arg(abs, error));
        delete editor;
        return nullptr;
    }

    connect(editor, &CodeEditor::cursorPositionChanged, this, &MainWindow::onCursorMoved);
    connect(editor, &CodeEditor::zoomRequested, this, &MainWindow::changeEditorZoom);
    connect(editor, &CodeEditor::wordActivated, this, &MainWindow::onWordActivated);
    connect(editor, &CodeEditor::includeActivated, this, &MainWindow::onIncludeActivated);
    connect(editor, &CodeEditor::encodingChanged, this,
            [this](const QString&) { updateEncodingLabel(); });

    if (m_editorZoom > 0.0)
        editor->setZoomPointSize(m_editorZoom);

    // Ctrl 悬停时判断这个词能不能跳，能跳才画下划线
    editor->setSymbolChecker([this](const QString& word) {
        ensureSymbolIndex();
        return !m_symbols.lookup(word).isEmpty();
    });

    const int index = m_tabs->addTab(editor, QFileInfo(abs).fileName());
    m_tabs->setTabToolTip(index, abs);

    // 同一个文件始终同一种颜色，新文件取下一种
    if (!m_fileTabColors.contains(abs))
        m_fileTabColors.insert(abs, nextTabColor());
    m_tabs->tabBar()->setTabData(index, m_fileTabColors.value(abs));
    m_tabs->setCurrentIndex(index);
    updateEncodingLabel();

    return editor;
}

void MainWindow::onOpenWithEncoding(const QString& path, const QString& encoding)
{
    CodeEditor* editor = openFile(path);
    if (!editor)
        return;

    editor->setEncoding(encoding, true);
    updateEncodingLabel();
}

void MainWindow::updateEncodingLabel()
{
    if (!m_statusEncoding)
        return;

    if (CodeEditor* editor = currentEditor())
        m_statusEncoding->setText(Lang::t("Encoding: %1").arg(editor->encoding()));
    else
        m_statusEncoding->clear();
}

CodeEditor* MainWindow::currentEditor() const
{
    return qobject_cast<CodeEditor*>(m_tabs->currentWidget());
}

void MainWindow::onTabCloseRequested(int index)
{
    auto* editor = qobject_cast<CodeEditor*>(m_tabs->widget(index));
    if (editor && editor->document()->isModified()) {
        const auto answer = QMessageBox::question(
            this, Lang::t("Save Changes"),
            Lang::t("%1 has been modified. Save changes?").arg(m_tabs->tabText(index)),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

        if (answer == QMessageBox::Cancel)
            return;
        if (answer == QMessageBox::Save) {
            QString error;
            if (!editor->saveFile(&error)) {
                QMessageBox::warning(this, Lang::t("Save"), error);
                return;
            }
        }
    }

    m_tabs->removeTab(index);
    if (editor)
        editor->deleteLater();
}

void MainWindow::revealBuildOutput()
{
    // 构建/清理一触发就把输出窗口顶出来，哪怕上一次被手动关掉过
    m_outputDock->show();
    m_outputDock->raise();
}

// ---------------------------------------------------------------- 第一排操作

void MainWindow::onCut()
{
    if (CodeEditor* editor = currentEditor())
        editor->cut();
}

void MainWindow::onCopy()
{
    if (CodeEditor* editor = currentEditor())
        editor->copy();
}

void MainWindow::onPaste()
{
    if (CodeEditor* editor = currentEditor())
        editor->paste();
}

void MainWindow::onUndo()
{
    if (CodeEditor* editor = currentEditor())
        editor->undo();
}

void MainWindow::onRedo()
{
    if (CodeEditor* editor = currentEditor())
        editor->redo();
}

void MainWindow::recordNavigation(const QString& file, int line)
{
    if (file.isEmpty())
        return;

    if (m_navIndex >= 0 && m_navIndex < m_navHistory.size()) {
        const NavPoint& current = m_navHistory.at(m_navIndex);
        if (current.file == file && current.line == line)
            return;
    }

    while (m_navHistory.size() > m_navIndex + 1)
        m_navHistory.removeLast();

    NavPoint point;
    point.file = file;
    point.line = line;
    m_navHistory.append(point);

    while (m_navHistory.size() > 200) {
        m_navHistory.removeFirst();
        --m_navIndex;
    }
    m_navIndex = m_navHistory.size() - 1;
}

void MainWindow::onNavigateBack()
{
    if (m_navIndex <= 0)
        return;
    --m_navIndex;
    const NavPoint point = m_navHistory.at(m_navIndex);
    if (CodeEditor* editor = openFile(point.file))
        editor->gotoLine(point.line);
}

void MainWindow::onNavigateForward()
{
    if (m_navIndex < 0 || m_navIndex + 1 >= m_navHistory.size())
        return;
    ++m_navIndex;
    const NavPoint point = m_navHistory.at(m_navIndex);
    if (CodeEditor* editor = openFile(point.file))
        editor->gotoLine(point.line);
}

void MainWindow::onBookmarkPrev()
{
    if (CodeEditor* editor = currentEditor())
        editor->gotoPrevBookmark();
}

void MainWindow::onBookmarkNext()
{
    if (CodeEditor* editor = currentEditor())
        editor->gotoNextBookmark();
}

void MainWindow::onBookmarkDelete()
{
    if (CodeEditor* editor = currentEditor())
        editor->toggleBookmarkAtCursor();
}

void MainWindow::onIndent()
{
    if (CodeEditor* editor = currentEditor())
        editor->indentSelection();
}

void MainWindow::onUnindent()
{
    if (CodeEditor* editor = currentEditor())
        editor->unindentSelection();
}

void MainWindow::onComment()
{
    if (CodeEditor* editor = currentEditor())
        editor->commentSelection();
}

void MainWindow::onUncomment()
{
    if (CodeEditor* editor = currentEditor())
        editor->uncommentSelection();
}

void MainWindow::onFindNext()
{
    CodeEditor* editor = currentEditor();
    if (!editor)
        return;

    QString needle = editor->textCursor().selectedText();
    if (needle.isEmpty()) {
        QTextCursor cursor = editor->textCursor();
        cursor.select(QTextCursor::WordUnderCursor);
        needle = cursor.selectedText();
    }
    if (needle.isEmpty())
        return;

    if (!editor->findNextText(needle))
        m_statusMessage->setText(QStringLiteral("not found: %1").arg(needle));
}

void MainWindow::onFind()
{
    CodeEditor* editor = currentEditor();
    if (!editor)
        return;

    bool ok = false;
    const QString needle = QInputDialog::getText(this, Lang::t("Find"),
                                                 QStringLiteral("Find:"), QLineEdit::Normal,
                                                 editor->textCursor().selectedText(), &ok);
    if (!ok || needle.isEmpty())
        return;

    if (!editor->findNextText(needle))
        m_statusMessage->setText(QStringLiteral("not found: %1").arg(needle));
}

void MainWindow::onFindInFiles()
{
    if (!m_project.isValid())
        return;

    bool ok = false;
    const QString needle = QInputDialog::getText(this, Lang::t("Find in Files"),
                                                 QStringLiteral("Find:"), QLineEdit::Normal,
                                                 QString(), &ok);
    if (!ok || needle.isEmpty())
        return;

    const Target* target = m_project.target(m_activeTarget);
    if (!target)
        return;

    QDialog dialog(this);
    dialog.setWindowTitle(Lang::t("Find in Files"));
    dialog.resize(720, 420);
    auto* layout = new QVBoxLayout(&dialog);
    auto* list = new QListWidget(&dialog);
    layout->addWidget(list);

    int hits = 0;
    for (const ProjGroup& group : target->groups) {
        for (const ProjFile& file : group.files) {
            if (file.excluded)
                continue;
            QString path = file.path;
            path.replace(QLatin1Char('\\'), QLatin1Char('/'));
            const QString abs = QDir::cleanPath(m_project.directory() + QLatin1Char('/') + path);

            QFile input(abs);
            if (!input.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;

            QTextStream stream(&input);
            stream.setCodec("UTF-8");
            int lineNumber = 0;
            while (!stream.atEnd()) {
                const QString line = stream.readLine();
                ++lineNumber;
                if (!line.contains(needle))
                    continue;

                auto* item = new QListWidgetItem(
                    QStringLiteral("%1 : %2 : %3")
                        .arg(QFileInfo(abs).fileName())
                        .arg(lineNumber)
                        .arg(line.trimmed().left(120)));
                item->setData(Qt::UserRole, abs);
                item->setData(Qt::UserRole + 1, lineNumber);
                list->addItem(item);
                ++hits;
            }
        }
    }

    connect(list, &QListWidget::itemDoubleClicked, &dialog, [this, &dialog](QListWidgetItem* item) {
        if (!item)
            return;
        const QString file = item->data(Qt::UserRole).toString();
        const int line = item->data(Qt::UserRole + 1).toInt();
        dialog.accept();
        if (CodeEditor* editor = openFile(file))
            editor->gotoLine(line);
    });

    m_statusMessage->setText(QStringLiteral("%1 hit(s)").arg(hits));
    dialog.exec();
}

void MainWindow::populateSymbolCombo()
{
    if (!m_symbolCombo)
        return;

    m_symbolCombo->clear();
    if (!m_project.isValid())
        return;

    ensureSymbolIndex();
    const QStringList names = m_symbols.names();
    m_symbolCombo->addItems(names);
    m_symbolCombo->setCurrentIndex(-1);
    m_symbolCombo->setToolTip(QStringLiteral("%1 symbols").arg(names.size()));
}

void MainWindow::onSymbolChosen(int index)
{
    if (!m_symbolCombo || index < 0)
        return;

    const QString name = m_symbolCombo->itemText(index);
    const QList<SymbolLocation> hits = m_symbols.lookup(name);
    if (hits.isEmpty())
        return;

    const SymbolLocation& target = hits.first();
    if (CodeEditor* editor = openFile(target.file)) {
        editor->gotoLine(target.line);
        recordNavigation(target.file, target.line);
    }
}

void MainWindow::onBreakpointToggle()
{
    if (CodeEditor* editor = currentEditor())
        editor->toggleBreakpointAtCursor();
}

void MainWindow::onBreakpointDelete()
{
    if (CodeEditor* editor = currentEditor())
        editor->removeBreakpointAtCursor();
}

void MainWindow::onBreakpointClearTarget()
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto* editor = qobject_cast<CodeEditor*>(m_tabs->widget(i)))
            editor->clearBreakpoints();
    }
    m_statusMessage->setText(QStringLiteral("breakpoints cleared"));
}

void MainWindow::onBreakpointClearAll()
{
    onBreakpointClearTarget();
}

QColor MainWindow::nextTabColor()
{
    static const QColor palette[] = {
        QColor(0x0b, 0x57, 0xd0), QColor(0x0a, 0x7d, 0x33), QColor(0xa6, 0x26, 0xa4),
        QColor(0xb3, 0x5c, 0x00), QColor(0x0e, 0x74, 0x90), QColor(0xc2, 0x41, 0x0c),
        QColor(0x6d, 0x28, 0xd9), QColor(0x15, 0x80, 0x3d)
    };
    const int count = int(sizeof(palette) / sizeof(palette[0]));
    return palette[m_tabColorCursor++ % count];
}

void MainWindow::onSave()
{
    CodeEditor* editor = currentEditor();
    if (!editor)
        return;

    // 新建的未命名文件，Ctrl+S 先弹另存为
    if (editor->filePath().isEmpty()) {
        onSaveAs();
        return;
    }

    // Ctrl+S = 先格式化再存盘
    if (Settings::instance().formatOnSave())
        formatEditor(editor);

    saveEditor(editor);
}

bool MainWindow::saveEditor(CodeEditor* editor)
{
    if (!editor)
        return false;

    QString error;
    if (!editor->saveFile(&error)) {
        QMessageBox::warning(this, Lang::t("Save"), error);
        return false;
    }
    return true;
}

void MainWindow::formatEditor(CodeEditor* editor)
{
    if (!editor || editor->filePath().isEmpty())
        return;

    const Formatter::Result result = Formatter::format(editor->toPlainText(), editor->filePath());
    if (!result.ok) {
        m_statusMessage->setText(QStringLiteral("格式化失败: %1").arg(result.error));
        return;
    }

    editor->applyFormattedText(result.text);

    if (result.engine == QLatin1String("clang-format"))
        m_statusMessage->setText(QStringLiteral("已用 clang-format 格式化"));
    else
        m_statusMessage->setText(
            QStringLiteral("已用内置缩进器格式化（%1）").arg(result.error));
}

void MainWindow::onFormatDocument()
{
    if (CodeEditor* editor = currentEditor())
        formatEditor(editor);
}

void MainWindow::onSaveAll()
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto* editor = qobject_cast<CodeEditor*>(m_tabs->widget(i));
        if (editor && editor->document()->isModified()) {
            QString error;
            if (!editor->saveFile(&error))
                QMessageBox::warning(this, Lang::t("Save"), error);
        }
    }
}

void MainWindow::changeEditorZoom(int delta)
{
    if (m_editorZoom <= 0.0)
        m_editorZoom = KeilStyle::editorFont().pointSizeF();

    m_editorZoom = qBound(6.0, m_editorZoom + delta, 48.0);

    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto* editor = qobject_cast<CodeEditor*>(m_tabs->widget(i)))
            editor->setZoomPointSize(m_editorZoom);
    }
}

void MainWindow::resetEditorZoom()
{
    m_editorZoom = KeilStyle::editorFont().pointSizeF();
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto* editor = qobject_cast<CodeEditor*>(m_tabs->widget(i)))
            editor->setZoomPointSize(m_editorZoom);
    }
}

void MainWindow::onCursorMoved(int line, int column)
{
    m_statusPosition->setText(Lang::t("Ln %1, Col %2").arg(line).arg(column));
}

void MainWindow::ensureSymbolIndex()
{
    if (!m_symbols.isBuilt())
        m_symbols.build(m_project, m_activeTarget);
}

QStringList MainWindow::currentIncludeDirs() const
{
    QStringList dirs;
    const Target* target = m_project.target(m_activeTarget);
    if (!target)
        return dirs;

    const QString base = m_project.directory();
    for (const QString& inc : splitOptionList(target->includePaths)) {
        QString p = inc;
        p.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (!QFileInfo(p).isAbsolute())
            p = QDir::cleanPath(base + QLatin1Char('/') + p);
        dirs << p;
    }
    return dirs;
}

void MainWindow::onWordActivated(const QString& word, const QString& fromFile)
{
    ensureSymbolIndex();

    const QList<SymbolLocation> hits = m_symbols.lookup(word);
    if (hits.isEmpty()) {
        m_statusMessage->setText(QStringLiteral("找不到符号: %1").arg(word));
        return;
    }

    // 优先跳到别的文件里的定义，避免原地打转
    SymbolLocation target = hits.first();
    for (const SymbolLocation& loc : hits) {
        if (loc.file != fromFile) {
            target = loc;
            break;
        }
    }

    if (CodeEditor* editor = openFile(target.file))
        editor->gotoLine(target.line);

    recordNavigation(target.file, target.line);

    m_statusMessage->setText(QStringLiteral("%1 → %2 : %3")
                                 .arg(word, QFileInfo(target.file).fileName())
                                 .arg(target.line));
}

void MainWindow::onIncludeActivated(const QString& includeName, const QString& fromFile)
{
    const QString local = QDir::cleanPath(QFileInfo(fromFile).absolutePath()
                                          + QLatin1Char('/') + includeName);
    if (QFileInfo::exists(local)) {
        openFile(local);
        return;
    }

    for (const QString& dir : currentIncludeDirs()) {
        const QString candidate = QDir::cleanPath(dir + QLatin1Char('/') + includeName);
        if (QFileInfo::exists(candidate)) {
            openFile(candidate);
            return;
        }
    }

    m_statusMessage->setText(QStringLiteral("找不到头文件: %1").arg(includeName));
}

void MainWindow::onJumpTo(const QString& file, int line)
{
    if (!QFileInfo::exists(file))
        return;
    if (CodeEditor* editor = openFile(file))
        editor->gotoLine(line);
}

// ---------------------------------------------------------------- 构建

void MainWindow::buildRecentMenu()
{
    if (!m_recentMenu)
        return;
    m_recentMenu->clear();

    const QStringList recent = Settings::instance().recentProjects();
    for (const QString& path : recent) {
        QAction* a = m_recentMenu->addAction(QFileInfo(path).fileName());
        a->setToolTip(path);
        connect(a, &QAction::triggered, this, [this, path]() { openProject(path); });
    }
    if (recent.isEmpty()) {
        QAction* empty = m_recentMenu->addAction(Lang::t("(empty)"));
        empty->setEnabled(false);
    }
}

bool MainWindow::ensureToolchain()
{
    if (m_toolchain.isValid())
        return true;

    if (!ToolchainDialog::configure(this, m_toolchain))
        return false;

    Settings::instance().setToolchainDir(m_toolchain.binDir);
    Settings::instance().setToolchainFamily(m_toolchain.family);
    Settings::instance().sync();
    reloadToolchain();
    return m_toolchain.isValid();
}

void MainWindow::reloadToolchain()
{
    Settings& s = Settings::instance();
    QString dir = s.toolchainDir();
    if (dir.isEmpty())
        dir = Toolchain::guessBinDir(s.toolchainFamily());

    m_toolchain = Toolchain::detect(dir, s.toolchainFamily());
    m_engine->setToolchain(m_toolchain);

    updateCompilerStatus();
}

void MainWindow::switchLanguage(int id)
{
    const Lang::Id target = static_cast<Lang::Id>(id);
    if (Lang::current() == target)
        return;

    Lang::set(target);
    Settings::instance().setLanguage(Lang::idToString(target));
    Settings::instance().sync();

    retranslateUi();
}

void MainWindow::retranslateUi()
{
    // 菜单和工具栏都是启动时一次性建的，切换语言就整体重建
    menuBar()->clear();
    createMenus();

    const QList<QToolBar*> bars = findChildren<QToolBar*>();
    for (QToolBar* bar : bars) {
        removeToolBarBreak(bar);
        removeToolBar(bar);
        bar->hide();          // deleteLater 之前先藏起来，避免视觉残留
        bar->deleteLater();
    }
    createToolBars();
    applyToolBarLayout();

    // 这些 QAction 是成员，语言切换时要手动刷新文本
    auto retitle = [](QAction* action, const QString& text, const QString& tip = QString()) {
        if (!action)
            return;
        action->setText(text);
        action->setToolTip(tip.isEmpty() ? text : tip);
    };
    retitle(m_actNewFile, Lang::t("New File"));
    retitle(m_actOpenFile, Lang::t("Open File"));
    retitle(m_actSave, Lang::t("Save"));
    retitle(m_actSaveAll, Lang::t("Save All"));
    retitle(m_actBuild, Lang::t("Build Target"), Lang::t("Build target files (F7)"));
    retitle(m_actRebuild, Lang::t("Rebuild all target files"));
    retitle(m_actTranslate, Lang::t("Translate"), Lang::t("Translate current file (Ctrl+F7)"));
    retitle(m_actStop, Lang::t("Stop build"));
    retitle(m_actClean, Lang::t("Clean targets"));
    retitle(m_actBatchBuild, Lang::t("Batch Build"));
    retitle(m_actOptionsTarget, Lang::t("Options for Target..."));
    retitle(m_actCloseProject, Lang::t("Close Project"));
    retitle(m_actDownload, Lang::t("Download to Flash"));

    m_projectDock->setWindowTitle(Lang::t("Project"));
    m_outputDock->setWindowTitle(Lang::t("Build Output"));

    m_statusMessage->setText(Lang::t("Ready"));
    m_statusPosition->setText(Lang::t("Ln 1, Col 1"));
    updateEncodingLabel();
    updateCompilerStatus();
    updateWindowTitle();
}

void MainWindow::updateCompilerStatus()
{
    const QString compiler = m_toolchain.isValid()
                                 ? Lang::t("Compiler: %1").arg(m_toolchain.family)
                                 : Lang::t("Compiler: not configured");
    m_statusToolchain->setText(Lang::t("%1  |  Jobs: %2")
                                   .arg(compiler)
                                   .arg(m_engine->jobs()));
}

void MainWindow::onConfigureToolchain()
{
    Toolchain tc = m_toolchain;
    if (!ToolchainDialog::configure(this, tc))
        return;

    m_toolchain = tc;
    Settings::instance().setToolchainDir(tc.binDir);
    Settings::instance().setToolchainFamily(tc.family);
    Settings::instance().sync();
    m_engine->setToolchain(m_toolchain);
    updateCompilerStatus();
}

void MainWindow::onOptionsForTarget()
{
    if (!m_project.isValid())
        return;

    Target* target = m_project.target(m_activeTarget);
    if (!target)
        return;

    OptionsDialog dlg(*target, m_project.directory(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const Target edited = dlg.result();

    QString error;
    if (!Uvproj::saveTarget(m_project.filePath, edited, &error)) {
        QMessageBox::warning(this, Lang::t("Options for Target"), error);
        return;
    }

    *target = edited;
    m_engine->setProject(m_project, m_activeTarget);
    m_projectTree->setProject(m_project, m_activeTarget);
    m_statusMessage->setText(Lang::t("Options saved."));
}

void MainWindow::onOptionsForFile(const QString& path)
{
    Q_UNUSED(path);
    QMessageBox::information(
        this, Lang::t("Options for File"),
        QStringLiteral("文件级选项的编辑界面将在下一步补上。\n"
                       "当前可以在 Options for Target 里设置公共选项。"));
}

void MainWindow::onBuild()
{
    if (!m_project.isValid()) {
        QMessageBox::information(this, QStringLiteral("Build"),
                                 QStringLiteral("请先打开一个 .uvprojx 工程。"));
        return;
    }
    if (!ensureToolchain())
        return;

    revealBuildOutput();
    m_buildOutput->clear();
    m_buildOutput->clearDiagnostics();
    setBusy(true);
    m_engine->build(false);
}

void MainWindow::onRebuild()
{
    if (!m_project.isValid()) {
        QMessageBox::information(this, Lang::t("Rebuild"),
                                 QStringLiteral("请先打开一个 .uvprojx 工程。"));
        return;
    }
    if (!ensureToolchain())
        return;

    revealBuildOutput();
    m_buildOutput->clear();
    m_buildOutput->clearDiagnostics();
    setBusy(true);
    m_engine->build(true);
}

void MainWindow::onTranslate()
{
    if (!m_project.isValid())
        return;
    if (!ensureToolchain())
        return;

    CodeEditor* editor = currentEditor();
    if (!editor) {
        QMessageBox::information(this, Lang::t("Translate"),
                                 QStringLiteral("请先打开一个源文件。"));
        return;
    }

    revealBuildOutput();
    m_buildOutput->clear();
    m_buildOutput->clearDiagnostics();
    setBusy(true);
    m_engine->translate(editor->filePath());
}

void MainWindow::onClean()
{
    if (!m_project.isValid())
        return;
    revealBuildOutput();
    m_buildOutput->clear();
    m_buildOutput->clearDiagnostics();
    m_engine->clean();
}

void MainWindow::onStopBuild()
{
    m_engine->cancel();
}

void MainWindow::onDownload()
{
    QMessageBox::information(
        this, Lang::t("Download"),
        QStringLiteral("烧写功能将在下一步接入（OpenOCD/pyOCD + CMSIS-DAP）。\n"
                       "当前可以先生成 HEX/BIN。"));
}

void MainWindow::onDebugPlaceholder()
{
    m_statusMessage->setText(QStringLiteral("调试会话尚未实现（下一步：GDB + OpenOCD 后端）"));
}

void MainWindow::onEngineOutput(const QString& line)
{
    m_buildOutput->appendLine(line);
}

void MainWindow::onEngineDiagnostic(const Diagnostic& d)
{
    m_buildOutput->addDiagnostic(d);
}

void MainWindow::onEngineFinished(bool success, int errors, int warnings)
{
    setBusy(false);
    m_statusMessage->setText(Lang::t("%1 - %2 Error(s), %3 Warning(s)")
                                 .arg(success ? Lang::t("Build succeeded")
                                              : Lang::t("Build failed"))
                                 .arg(errors)
                                 .arg(warnings));
    if (!success)
        m_buildOutput->showErrorList();
}

void MainWindow::setBusy(bool busy)
{
    m_busy = busy;
    m_actBuild->setEnabled(!busy);
    m_actRebuild->setEnabled(!busy);
    m_actTranslate->setEnabled(!busy);
    m_actClean->setEnabled(!busy);
    m_actStop->setEnabled(busy);
}

// ---------------------------------------------------------------- 收尾

void MainWindow::onAbout()
{
    QMessageBox::about(
        this, Lang::t("About uVision5-OSE"),
        QStringLiteral("<h3>uVision5-OSE</h3>"
                       "<p>一个开源的 uVision5 风格嵌入式 IDE 前端。</p>"
                       "<p>本程序不分发 ARM Compiler。请自行安装 Keil MDK 并在 "
                       "Tools &rarr; Configure Toolchain 中指定 armcc.exe 所在目录。</p>"));
}

void MainWindow::restoreWindowState()
{
    Settings& s = Settings::instance();
    const QByteArray geo = s.geometry();
    const QByteArray state = s.windowState();

    if (geo.isEmpty()) {
        resize(1180, 760);
    } else {
        restoreGeometry(geo);
        if (!state.isEmpty())
            restoreState(state);
    }
}

void MainWindow::saveWindowState()
{
    Settings& s = Settings::instance();
    s.setGeometry(saveGeometry());
    s.setWindowState(saveState());
    s.sync();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveWindowState();
    event->accept();
}
