#pragma once

#include <QMainWindow>

#include "core/BuildEngine.h"
#include "core/Lang.h"
#include "core/SymbolIndex.h"
#include "core/Toolchain.h"
#include "core/UvprojModel.h"
#include "ui/CodeEditor.h"   // JumpTarget

class QAction;
class QComboBox;
class QDockWidget;
class QMenu;
class QStringListModel;
class QThread;
class QToolBar;
class QLabel;
class QTabWidget;

class BuildOutput;
class LoadingOverlay;
class ProjectLoader;
class CodeEditor;
class ProjectTree;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void openProject(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void onNewFile();
    void onOpenFile();
    void onSaveAs();
    bool saveAsFor(CodeEditor* editor);
    bool saveEditorInteractive(CodeEditor* editor);
    void refreshTabTitle(CodeEditor* editor);
    void onNewProject();
    void onOpenProject();
    void onCloseProject();
    void onSave();
    void onSaveAll();
    void onOptionsForTarget();
    void onOptionsForFile(const QString& path);
    void onConfigureToolchain();
    void onBuild();
    void onRebuild();
    void onTranslate();
    void onClean();
    void onStopBuild();
    void onDownload();
    void onDebugPlaceholder();

    void onTargetChanged(int index);
    void onProjectLoaded();
    void onFileActivated(const QString& path);
    void onTabCloseRequested(int index);

    void onEngineOutput(const QString& line);
    void onEngineDiagnostic(const Diagnostic& d);
    void onEngineFinished(bool success, int errors, int warnings);

    void onFormatDocument();

    // 第一排工具栏对应的操作
    void onCut();
    void onCopy();
    void onPaste();
    void onUndo();
    void onRedo();
    void onNavigateBack();
    void onNavigateForward();
    void onBookmarkPrev();
    void onBookmarkNext();
    void onBookmarkDelete();
    void onIndent();
    void onUnindent();
    void onComment();
    void onUncomment();
    void onFindInFiles();
    void onFindNext();
    void onFind();
    void onSymbolChosen(int index);
    void onBreakpointToggle();
    void onBreakpointDelete();
    void onBreakpointClearTarget();
    void onBreakpointClearAll();
    void switchLanguage(int id);
    void retranslateUi();
    void onWordActivated(const QString& word, const QString& filePath);
    void onIncludeActivated(const QString& includeName, const QString& filePath);
    void onOpenWithEncoding(const QString& path, const QString& encoding);
    void updateEncodingLabel();

    void onJumpTo(const QString& file, int line);
    void onCursorMoved(int line, int column);

    void onAbout();

private:
    void createActions();
    void createMainToolbarActions();
    void applyToolBarLayout();
    void dumpToolBarLayout();
    void populateSymbolCombo();
    void populateTargetCombo();
    void ensureOverlay();
    void finishOverlay();
    void recordNavigation(const QString& file, int line);
    void createMenus();
    void createToolBars();
    void createDocks();
    void createStatusBar();

    void applyProject();
    void startLoad(const QString& path, const UvProject* project, int targetIndex);
    void updateWindowTitle();
    bool ensureToolchain();
    void reloadToolchain();
    void updateCompilerStatus();

    void changeEditorZoom(int delta);
    void resetEditorZoom();

    QColor nextTabColor();

    void revealBuildOutput();
    void formatEditor(CodeEditor* editor, bool interactive);
    bool saveEditor(CodeEditor* editor);
    void ensureSymbolIndex();
    QList<JumpTarget> jumpTargetsFor(const QString& word);
    QStringList currentIncludeDirs() const;

    CodeEditor* openFile(const QString& absolutePath);
    CodeEditor* currentEditor() const;
    void        setBusy(bool busy);
    void        saveWindowState();
    void        restoreWindowState();
    void        buildRecentMenu();

    bool createProjectTemplate(const QString& path);

    UvProject m_project;
    int       m_activeTarget = 0;
    bool      m_busy = false;
    qreal     m_editorZoom = 0.0;   // 0 表示还没调过，用默认字号

    Toolchain   m_toolchain;
    BuildEngine* m_engine = nullptr;
    SymbolIndex m_symbols;
    QHash<QString, QColor> m_fileTabColors;
    int         m_tabColorCursor = 0;

    ProjectTree* m_projectTree = nullptr;
    BuildOutput* m_buildOutput = nullptr;
    QTabWidget*  m_tabs = nullptr;
    QDockWidget* m_projectDock = nullptr;
    QDockWidget* m_outputDock = nullptr;

    LoadingOverlay* m_overlay = nullptr;
    ProjectLoader*  m_loader = nullptr;
    QThread*        m_loadThread = nullptr;
    bool            m_loading = false;
    bool            m_layoutDumped = false;
    QString         m_loadStage;
    QString         m_loadDetail;

    QLabel* m_statusMessage = nullptr;
    QLabel* m_statusToolchain = nullptr;
    QLabel* m_statusEncoding = nullptr;
    QLabel* m_statusPosition = nullptr;

    struct NavPoint
    {
        QString file;
        int     line = 0;
    };

    QList<QAction*> m_mainBarActions;   // nullptr = 分隔线
    QList<QToolBar*> m_toolBars;
    QComboBox*       m_symbolCombo = nullptr;
    QStringListModel* m_symbolModel = nullptr;
    QComboBox*       m_targetCombo = nullptr;
    bool            m_updatingTargets = false;
    QAction*        m_symbolComboAnchor = nullptr;
    QList<NavPoint> m_navHistory;
    int             m_navIndex = -1;

    QAction* m_actNewFile = nullptr;
    QAction* m_actOpenFile = nullptr;
    QAction* m_actBatchBuild = nullptr;
    QAction* m_actBuild = nullptr;
    QAction* m_actRebuild = nullptr;
    QAction* m_actTranslate = nullptr;
    QAction* m_actClean = nullptr;
    QAction* m_actStop = nullptr;
    QAction* m_actSave = nullptr;
    QAction* m_actSaveAll = nullptr;
    QAction* m_actOptionsTarget = nullptr;
    QAction* m_actCloseProject = nullptr;
    QAction* m_actDownload = nullptr;
    QMenu*   m_recentMenu = nullptr;
};
