#include "ui/MainWindow.h"

#include "core/Settings.h"
#include "core/UvprojReader.h"
#include "ui/BuildOutput.h"
#include "ui/CodeEditor.h"
#include "ui/KeilStyle.h"
#include "ui/OptionsDialog.h"
#include "ui/ProjectTree.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
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

    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    setCentralWidget(m_tabs);

    m_engine = new BuildEngine(this);

    createActions();
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

    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::onTabCloseRequested);

    m_engine->setJobs(Settings::instance().buildJobs());

    reloadToolchain();
    restoreWindowState();
    updateWindowTitle();

    setBusy(false);
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------- 界面搭建

void MainWindow::createActions()
{
    m_actBuild = new QAction(KeilStyle::icon(QStringLiteral("build")),
                             QStringLiteral("Build Target"), this);
    m_actBuild->setShortcut(QKeySequence(Qt::Key_F7));
    m_actBuild->setToolTip(QStringLiteral("Build target files (F7)"));
    connect(m_actBuild, &QAction::triggered, this, &MainWindow::onBuild);

    m_actRebuild = new QAction(KeilStyle::icon(QStringLiteral("rebuild")),
                               QStringLiteral("Rebuild all target files"), this);
    m_actRebuild->setToolTip(QStringLiteral("Rebuild all target files"));
    connect(m_actRebuild, &QAction::triggered, this, &MainWindow::onRebuild);

    m_actTranslate = new QAction(KeilStyle::icon(QStringLiteral("translate")),
                                 QStringLiteral("Translate"), this);
    m_actTranslate->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_F7));
    m_actTranslate->setToolTip(QStringLiteral("Translate current file (Ctrl+F7)"));
    connect(m_actTranslate, &QAction::triggered, this, &MainWindow::onTranslate);

    m_actClean = new QAction(QStringLiteral("Clean targets"), this);
    connect(m_actClean, &QAction::triggered, this, &MainWindow::onClean);

    m_actStop = new QAction(KeilStyle::icon(QStringLiteral("stop")),
                            QStringLiteral("Stop build"), this);
    m_actStop->setToolTip(QStringLiteral("Stop build"));
    connect(m_actStop, &QAction::triggered, this, &MainWindow::onStopBuild);

    m_actSave = new QAction(KeilStyle::icon(QStringLiteral("save")),
                            QStringLiteral("Save"), this);
    m_actSave->setShortcut(QKeySequence::Save);
    connect(m_actSave, &QAction::triggered, this, &MainWindow::onSave);

    m_actSaveAll = new QAction(KeilStyle::icon(QStringLiteral("saveall")),
                               QStringLiteral("Save All"), this);
    connect(m_actSaveAll, &QAction::triggered, this, &MainWindow::onSaveAll);

    m_actOptionsTarget = new QAction(QStringLiteral("Options for Target..."), this);
    m_actOptionsTarget->setShortcut(QKeySequence(Qt::ALT + Qt::Key_F7));
    connect(m_actOptionsTarget, &QAction::triggered, this, &MainWindow::onOptionsForTarget);

    m_actCloseProject = new QAction(QStringLiteral("Close Project"), this);
    connect(m_actCloseProject, &QAction::triggered, this, &MainWindow::onCloseProject);

    m_actDownload = new QAction(QStringLiteral("Download to Flash"), this);
    m_actDownload->setShortcut(QKeySequence(Qt::Key_F8));
    connect(m_actDownload, &QAction::triggered, this, &MainWindow::onDownload);
}

void MainWindow::createMenus()
{
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(QStringLiteral("New Project..."), this, &MainWindow::onNewProject);
    fileMenu->addAction(QStringLiteral("Open Project..."), this, &MainWindow::onOpenProject);
    m_recentMenu = fileMenu->addMenu(QStringLiteral("Recent Projects"));
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::buildRecentMenu);
    fileMenu->addSeparator();
    fileMenu->addAction(m_actCloseProject);
    fileMenu->addSeparator();
    fileMenu->addAction(m_actSave);
    fileMenu->addAction(m_actSaveAll);
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("Exit"), this, &QWidget::close);

    QMenu* editMenu = menuBar()->addMenu(QStringLiteral("&Edit"));
    QAction* undo = editMenu->addAction(QStringLiteral("Undo"));
    undo->setShortcut(QKeySequence::Undo);
    connect(undo, &QAction::triggered, this, [this]() {
        if (currentEditor())
            currentEditor()->undo();
    });
    QAction* redo = editMenu->addAction(QStringLiteral("Redo"));
    redo->setShortcut(QKeySequence::Redo);
    connect(redo, &QAction::triggered, this, [this]() {
        if (currentEditor())
            currentEditor()->redo();
    });
    editMenu->addSeparator();
    QAction* cut = editMenu->addAction(QStringLiteral("Cut"));
    cut->setShortcut(QKeySequence::Cut);
    connect(cut, &QAction::triggered, this, [this]() {
        if (currentEditor())
            currentEditor()->cut();
    });
    QAction* copy = editMenu->addAction(QStringLiteral("Copy"));
    copy->setShortcut(QKeySequence::Copy);
    connect(copy, &QAction::triggered, this, [this]() {
        if (currentEditor())
            currentEditor()->copy();
    });
    QAction* paste = editMenu->addAction(QStringLiteral("Paste"));
    paste->setShortcut(QKeySequence::Paste);
    connect(paste, &QAction::triggered, this, [this]() {
        if (currentEditor())
            currentEditor()->paste();
    });
    editMenu->addSeparator();
    QAction* find = editMenu->addAction(QStringLiteral("Find..."));
    find->setShortcut(QKeySequence::Find);
    find->setEnabled(false);
    editMenu->addAction(QStringLiteral("Go to Line..."), this, [this]() {
        bool ok = false;
        const int line = QInputDialog::getInt(this, QStringLiteral("Go to Line"),
                                              QStringLiteral("Line:"), 1, 1, 1 << 20, 1, &ok);
        if (ok && currentEditor())
            currentEditor()->gotoLine(line);
    });

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    viewMenu->addAction(QStringLiteral("Project Window"), this, [this]() {
        m_projectDock->show();
        m_projectDock->raise();
    });
    viewMenu->addAction(QStringLiteral("Build Output Window"), this, [this]() {
        m_outputDock->show();
        m_outputDock->raise();
    });
    viewMenu->addAction(QStringLiteral("Error List Window"), this, [this]() {
        m_outputDock->show();
        m_buildOutput->showErrorList();
    });
    viewMenu->addSeparator();

    QAction* zoomIn = viewMenu->addAction(QStringLiteral("Zoom In"));
    zoomIn->setShortcuts({ QKeySequence::ZoomIn,
                           QKeySequence(Qt::CTRL + Qt::Key_Plus),
                           QKeySequence(Qt::CTRL + Qt::Key_Equal) });
    connect(zoomIn, &QAction::triggered, this, [this]() { changeEditorZoom(1); });

    QAction* zoomOut = viewMenu->addAction(QStringLiteral("Zoom Out"));
    zoomOut->setShortcuts({ QKeySequence::ZoomOut,
                            QKeySequence(Qt::CTRL + Qt::Key_Minus) });
    connect(zoomOut, &QAction::triggered, this, [this]() { changeEditorZoom(-1); });

    QAction* zoomReset = viewMenu->addAction(QStringLiteral("Reset Zoom"));
    zoomReset->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_0));
    connect(zoomReset, &QAction::triggered, this, &MainWindow::resetEditorZoom);

    viewMenu->addSeparator();
    for (const QString& name : { QStringLiteral("Functions Window"),
                                 QStringLiteral("Source Browser"),
                                 QStringLiteral("Books Window") }) {
        QAction* a = viewMenu->addAction(name);
        a->setEnabled(false);
        a->setToolTip(QStringLiteral("尚未实现"));
    }

    QMenu* projectMenu = menuBar()->addMenu(QStringLiteral("&Project"));
    projectMenu->addAction(m_actOptionsTarget);
    projectMenu->addSeparator();
    projectMenu->addAction(m_actBuild);
    projectMenu->addAction(m_actRebuild);
    projectMenu->addAction(m_actTranslate);
    projectMenu->addAction(m_actClean);
    projectMenu->addSeparator();
    projectMenu->addAction(m_actCloseProject);

    QMenu* flashMenu = menuBar()->addMenu(QStringLiteral("&Flash"));
    flashMenu->addAction(m_actDownload);
    QAction* erase = flashMenu->addAction(QStringLiteral("Erase"));
    erase->setEnabled(false);
    flashMenu->addSeparator();
    flashMenu->addAction(QStringLiteral("Configure Toolchain..."),
                         this, &MainWindow::onConfigureToolchain);

    QMenu* debugMenu = menuBar()->addMenu(QStringLiteral("&Debug"));
    QAction* startDebug = debugMenu->addAction(QStringLiteral("Start/Stop Debug Session"));
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
    addDebugMenuAction(QStringLiteral("Run"), QKeySequence(Qt::Key_F5));
    addDebugMenuAction(QStringLiteral("Stop"), QKeySequence());
    addDebugMenuAction(QStringLiteral("Step"), QKeySequence(Qt::Key_F11));
    addDebugMenuAction(QStringLiteral("Step Over"), QKeySequence(Qt::Key_F10));
    addDebugMenuAction(QStringLiteral("Step Out"), QKeySequence(Qt::SHIFT + Qt::Key_F11));
    addDebugMenuAction(QStringLiteral("Run to Cursor Line"), QKeySequence(Qt::CTRL + Qt::Key_F10));
    debugMenu->addSeparator();
    for (const QString& name : { QStringLiteral("Breakpoints..."),
                                 QStringLiteral("Watch Windows"),
                                 QStringLiteral("Call Stack Window"),
                                 QStringLiteral("Register Window"),
                                 QStringLiteral("Memory Window") }) {
        QAction* a = debugMenu->addAction(name);
        a->setEnabled(false);
        a->setToolTip(QStringLiteral("尚未实现"));
    }

    QMenu* peripheralsMenu = menuBar()->addMenu(QStringLiteral("&Peripherals"));
    QMenu* systemViewer = peripheralsMenu->addMenu(QStringLiteral("System Viewer"));
    QAction* svNote = systemViewer->addAction(QStringLiteral("(需要调试会话，尚未实现)"));
    svNote->setEnabled(false);

    QMenu* toolsMenu = menuBar()->addMenu(QStringLiteral("&Tools"));
    toolsMenu->addAction(QStringLiteral("Configure Toolchain..."),
                         this, &MainWindow::onConfigureToolchain);

    // 并行编译任务数：Auto = 本机逻辑处理器数
    QMenu* jobsMenu = toolsMenu->addMenu(QStringLiteral("Parallel Build"));
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

    addJobsAction(QStringLiteral("Auto  (%1 logical threads)")
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

    QAction* customTools = toolsMenu->addAction(QStringLiteral("Customize Tools Menu..."));
    customTools->setEnabled(false);

    QMenu* windowMenu = menuBar()->addMenu(QStringLiteral("&Window"));
    windowMenu->addAction(QStringLiteral("Close All"), this, [this]() {
        while (m_tabs->count() > 0)
            onTabCloseRequested(m_tabs->count() - 1);
    });
    windowMenu->addAction(QStringLiteral("Reset Window Layout"), this, [this]() {
        m_projectDock->show();
        m_outputDock->show();
        addDockWidget(Qt::LeftDockWidgetArea, m_projectDock);
        addDockWidget(Qt::BottomDockWidgetArea, m_outputDock);
        resizeDocks({ m_projectDock }, { 260 }, Qt::Horizontal);
        resizeDocks({ m_outputDock }, { 180 }, Qt::Vertical);
    });

    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    helpMenu->addAction(QStringLiteral("About uVision5-OSE"), this, &MainWindow::onAbout);
}

void MainWindow::createToolBars()
{
    auto* fileBar = addToolBar(QStringLiteral("File"));
    fileBar->setObjectName(QStringLiteral("fileBar"));
    fileBar->setIconSize(QSize(16, 16));
    fileBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    fileBar->setMovable(false);
    fileBar->addAction(m_actSave);
    fileBar->addAction(m_actSaveAll);

    auto* buildBar = addToolBar(QStringLiteral("Build"));
    buildBar->setObjectName(QStringLiteral("buildBar"));
    buildBar->setIconSize(QSize(16, 16));
    buildBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    buildBar->setMovable(false);
    buildBar->addAction(m_actBuild);
    buildBar->addAction(m_actRebuild);
    buildBar->addAction(m_actTranslate);
    buildBar->addSeparator();
    buildBar->addAction(m_actStop);

    auto* debugBar = addToolBar(QStringLiteral("Debug"));
    debugBar->setObjectName(QStringLiteral("debugBar"));
    debugBar->setIconSize(QSize(16, 16));
    debugBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    debugBar->setMovable(false);

    auto addDebugAction = [this, debugBar](const QString& icon, const QString& text,
                                           const QKeySequence& shortcut) {
        QAction* a = new QAction(KeilStyle::icon(icon), text, this);
        a->setToolTip(text);
        if (!shortcut.isEmpty())
            a->setShortcut(shortcut);
        connect(a, &QAction::triggered, this, &MainWindow::onDebugPlaceholder);
        debugBar->addAction(a);
    };

    addDebugAction(QStringLiteral("debug_start"), QStringLiteral("Start/Stop Debug Session"),
                   QKeySequence(Qt::CTRL + Qt::Key_F5));
    addDebugAction(QStringLiteral("run"), QStringLiteral("Run"), QKeySequence(Qt::Key_F5));
    addDebugAction(QStringLiteral("stop"), QStringLiteral("Stop"), QKeySequence());
    debugBar->addSeparator();
    addDebugAction(QStringLiteral("step"), QStringLiteral("Step"), QKeySequence(Qt::Key_F11));
    addDebugAction(QStringLiteral("stepover"), QStringLiteral("Step Over"),
                   QKeySequence(Qt::Key_F10));
    addDebugAction(QStringLiteral("stepout"), QStringLiteral("Step Out"),
                   QKeySequence(Qt::SHIFT + Qt::Key_F11));
    addDebugAction(QStringLiteral("runtocursor"), QStringLiteral("Run to Cursor Line"),
                   QKeySequence(Qt::CTRL + Qt::Key_F10));
}

void MainWindow::createDocks()
{
    m_projectDock = new QDockWidget(QStringLiteral("Project"), this);
    m_projectDock->setObjectName(QStringLiteral("projectDock"));
    m_projectDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    m_projectTree = new ProjectTree(m_projectDock);
    m_projectDock->setWidget(m_projectTree);
    addDockWidget(Qt::LeftDockWidgetArea, m_projectDock);

    connect(m_projectTree, &ProjectTree::targetChanged, this, &MainWindow::onTargetChanged);
    connect(m_projectTree, &ProjectTree::fileActivated, this, &MainWindow::onFileActivated);
    connect(m_projectTree, &ProjectTree::optionsForTargetRequested,
            this, &MainWindow::onOptionsForTarget);
    connect(m_projectTree, &ProjectTree::optionsForFileRequested,
            this, &MainWindow::onOptionsForFile);

    m_outputDock = new QDockWidget(QStringLiteral("Build Output"), this);
    m_outputDock->setObjectName(QStringLiteral("outputDock"));
    m_outputDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    m_buildOutput = new BuildOutput(m_outputDock);
    m_outputDock->setWidget(m_buildOutput);
    addDockWidget(Qt::BottomDockWidgetArea, m_outputDock);

    connect(m_buildOutput, &BuildOutput::jumpRequested, this, &MainWindow::onJumpTo);

    resizeDocks({ m_projectDock }, { 260 }, Qt::Horizontal);
    resizeDocks({ m_outputDock }, { 180 }, Qt::Vertical);
}

void MainWindow::createStatusBar()
{
    m_statusMessage = new QLabel(QStringLiteral("Ready"), this);
    statusBar()->addWidget(m_statusMessage, 1);

    m_statusToolchain = new QLabel(this);
    statusBar()->addPermanentWidget(m_statusToolchain);

    m_statusPosition = new QLabel(QStringLiteral("Ln 1, Col 1"), this);
    statusBar()->addPermanentWidget(m_statusPosition);
}

// ---------------------------------------------------------------- 工程

void MainWindow::onNewProject()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("New uVision Project"), QString(),
        QStringLiteral("uVision Project (*.uvprojx)"));
    if (path.isEmpty())
        return;

    if (!createProjectTemplate(path)) {
        QMessageBox::warning(this, QStringLiteral("New Project"),
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
        this, QStringLiteral("Open uVision Project"),
        m_project.isValid() ? m_project.directory() : QString(),
        QStringLiteral("uVision Project (*.uvprojx *.uvproj);;All Files (*)"));
    if (!path.isEmpty())
        openProject(path);
}

void MainWindow::openProject(const QString& path)
{
    UvProject project;
    QString error;
    if (!Uvproj::load(path, project, &error)) {
        QMessageBox::critical(this, QStringLiteral("Open Project"), error);
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

    updateWindowTitle();
    setBusy(false);

    m_statusMessage->setText(QStringLiteral("Project '%1' loaded.")
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
    m_activeTarget = index;
    m_engine->setProject(m_project, m_activeTarget);
    updateWindowTitle();
}

void MainWindow::updateWindowTitle()
{
    QString title;
    if (m_project.isValid()) {
        const Target* t = m_project.target(m_activeTarget);
        title = QStringLiteral("%1 - %2").arg(m_project.baseName(), t ? t->name : QString());
    } else {
        title = QStringLiteral("No Project");
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
        QMessageBox::warning(this, QStringLiteral("Open File"),
                             QStringLiteral("无法打开 %1\n%2").arg(abs, error));
        delete editor;
        return nullptr;
    }

    connect(editor, &CodeEditor::cursorPositionChanged, this, &MainWindow::onCursorMoved);
    connect(editor, &CodeEditor::zoomRequested, this, &MainWindow::changeEditorZoom);

    if (m_editorZoom > 0.0)
        editor->setZoomPointSize(m_editorZoom);

    const int index = m_tabs->addTab(editor, QFileInfo(abs).fileName());
    m_tabs->setTabToolTip(index, abs);
    m_tabs->setCurrentIndex(index);

    return editor;
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
            this, QStringLiteral("Save Changes"),
            QStringLiteral("%1 has been modified. Save changes?").arg(m_tabs->tabText(index)),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

        if (answer == QMessageBox::Cancel)
            return;
        if (answer == QMessageBox::Save) {
            QString error;
            if (!editor->saveFile(&error)) {
                QMessageBox::warning(this, QStringLiteral("Save"), error);
                return;
            }
        }
    }

    m_tabs->removeTab(index);
    if (editor)
        editor->deleteLater();
}

void MainWindow::onSave()
{
    if (CodeEditor* editor = currentEditor()) {
        QString error;
        if (!editor->saveFile(&error))
            QMessageBox::warning(this, QStringLiteral("Save"), error);
    }
}

void MainWindow::onSaveAll()
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto* editor = qobject_cast<CodeEditor*>(m_tabs->widget(i));
        if (editor && editor->document()->isModified()) {
            QString error;
            if (!editor->saveFile(&error))
                QMessageBox::warning(this, QStringLiteral("Save"), error);
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
    m_statusPosition->setText(QStringLiteral("Ln %1, Col %2").arg(line).arg(column));
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
        QAction* empty = m_recentMenu->addAction(QStringLiteral("(empty)"));
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

void MainWindow::updateCompilerStatus()
{
    const QString compiler = m_toolchain.isValid()
                                 ? QStringLiteral("Compiler: %1").arg(m_toolchain.family)
                                 : QStringLiteral("Compiler: not configured");
    m_statusToolchain->setText(QStringLiteral("%1  |  Jobs: %2")
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
        QMessageBox::warning(this, QStringLiteral("Options for Target"), error);
        return;
    }

    *target = edited;
    m_engine->setProject(m_project, m_activeTarget);
    m_projectTree->setProject(m_project, m_activeTarget);
    m_statusMessage->setText(QStringLiteral("Options saved."));
}

void MainWindow::onOptionsForFile(const QString& path)
{
    Q_UNUSED(path);
    QMessageBox::information(
        this, QStringLiteral("Options for File"),
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

    m_buildOutput->clear();
    m_buildOutput->clearDiagnostics();
    m_outputDock->show();
    m_outputDock->raise();

    setBusy(true);
    m_engine->build(false);
}

void MainWindow::onRebuild()
{
    if (!m_project.isValid()) {
        QMessageBox::information(this, QStringLiteral("Rebuild"),
                                 QStringLiteral("请先打开一个 .uvprojx 工程。"));
        return;
    }
    if (!ensureToolchain())
        return;

    m_buildOutput->clear();
    m_buildOutput->clearDiagnostics();
    m_outputDock->show();
    m_outputDock->raise();

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
        QMessageBox::information(this, QStringLiteral("Translate"),
                                 QStringLiteral("请先打开一个源文件。"));
        return;
    }

    m_buildOutput->clear();
    m_buildOutput->clearDiagnostics();
    m_outputDock->show();

    setBusy(true);
    m_engine->translate(editor->filePath());
}

void MainWindow::onClean()
{
    if (!m_project.isValid())
        return;
    m_buildOutput->clear();
    m_buildOutput->clearDiagnostics();
    m_outputDock->show();
    m_engine->clean();
}

void MainWindow::onStopBuild()
{
    m_engine->cancel();
}

void MainWindow::onDownload()
{
    QMessageBox::information(
        this, QStringLiteral("Download"),
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
    m_statusMessage->setText(QStringLiteral("%1 - %2 Error(s), %3 Warning(s)")
                                 .arg(success ? QStringLiteral("Build succeeded")
                                              : QStringLiteral("Build failed"))
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
        this, QStringLiteral("About uVision5-OSE"),
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
