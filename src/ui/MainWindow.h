#pragma once

#include <QMainWindow>

#include "core/BuildEngine.h"
#include "core/Lang.h"
#include "core/SymbolIndex.h"
#include "core/Toolchain.h"
#include "core/UvprojModel.h"

class QAction;
class QDockWidget;
class QMenu;
class QLabel;
class QTabWidget;

class BuildOutput;
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

private slots:
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
    void onFileActivated(const QString& path);
    void onTabCloseRequested(int index);

    void onEngineOutput(const QString& line);
    void onEngineDiagnostic(const Diagnostic& d);
    void onEngineFinished(bool success, int errors, int warnings);

    void onFormatDocument();
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
    void createMenus();
    void createToolBars();
    void createDocks();
    void createStatusBar();

    void applyProject();
    void updateWindowTitle();
    bool ensureToolchain();
    void reloadToolchain();
    void updateCompilerStatus();

    void changeEditorZoom(int delta);
    void resetEditorZoom();

    QColor nextTabColor();
    void revealBuildOutput();
    void formatEditor(CodeEditor* editor);
    bool saveEditor(CodeEditor* editor);
    void ensureSymbolIndex();
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

    QLabel* m_statusMessage = nullptr;
    QLabel* m_statusToolchain = nullptr;
    QLabel* m_statusEncoding = nullptr;
    QLabel* m_statusPosition = nullptr;

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
