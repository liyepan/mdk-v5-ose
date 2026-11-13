#pragma once

#include <QFont>
#include <QList>
#include <QPlainTextEdit>
#include <QPoint>
#include <QSet>
#include <QTextCursor>

#include <functional>

class LineNumberArea;

// 右键菜单里的一条跳转（"跳转定义" / "跳转实现"）
struct JumpTarget
{
    QString label;
    QString file;
    int     line = 0;
};

// 带行号栏、当前行高亮、右键菜单的源码编辑器。
class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit CodeEditor(QWidget* parent = nullptr);

    bool loadFile(const QString& path, QString* error = nullptr);
    bool saveFile(QString* error = nullptr);

    QString filePath() const { return m_filePath; }
    void    setFilePath(const QString& path);

    void gotoLine(int line, int column = 1);
    void setHighlightAssembly(bool assembly);

    // 格式化后整体替换，保留撤销栈
    void applyFormattedText(const QString& text);

    // 文件编码：默认 UTF-8，可切 GBK / GB18030 / Big5 / 系统默认
    QString encoding() const { return m_encoding; }
    void    setEncoding(const QString& name, bool reloadFromDisk = false);
    static QStringList encodingChoices();

    // 判断某个标识符能不能跳转（由主窗口接上符号表）
    void setSymbolChecker(std::function<bool(const QString&)> checker);

    // 某个标识符的跳转目标（定义 / 实现），右键菜单用
    void setJumpResolver(std::function<QList<JumpTarget>(const QString&)> resolver);

    // 编辑操作
    void indentSelection();
    void unindentSelection();
    void commentSelection();
    void uncommentSelection();
    bool findNextText(const QString& text, bool forward = true);

    // 书签
    void toggleBookmarkAtCursor();
    void removeBookmarkAtCursor();
    void gotoNextBookmark();
    void gotoPrevBookmark();

    // 断点（只做标记，调试器还没接）
    void toggleBreakpointAtCursor();
    void removeBreakpointAtCursor();
    void clearBreakpoints();

    // 字号缩放（高 DPI 屏上真正管用的那个"缩放"）
    void  setZoomPointSize(qreal pointSize);
    qreal zoomPointSize() const { return m_pointSize; }

    int  lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent* event);

signals:
    void cursorPositionChanged(int line, int column);
    void zoomRequested(int delta);
    void encodingChanged(const QString& name);
    // Ctrl+左键点击标识符
    void wordActivated(const QString& word, const QString& filePath);
    // Ctrl+左键点击 #include "x.h"
    void includeActivated(const QString& includeName, const QString& filePath);
    // 右键菜单里点了"跳转定义/跳转实现"
    void jumpRequested(const QString& file, int line);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void leaveEvent(QEvent* event) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea(const QRect& rect, int dy);
    void highlightCurrentLine();
    void onCursorMoved();

private:
    void setupContextMenu();
    void applyFont();
    bool identifierAt(const QTextCursor& cursor, QString* word, int* start, int* end) const;
    bool includeAt(const QTextCursor& cursor, int* start, int* end) const;
    void updateHoverSelection(const QTextCursor& cursor, bool ctrlHeld);
    void refreshHover();
    QString wordAtPosition(const QPoint& pos) const;

    LineNumberArea* m_lineNumberArea = nullptr;
    qreal           m_pointSize = 0.0;
    QString         m_encoding = QStringLiteral("UTF-8");
    QTextCursor     m_hoverCursor;
    QSet<int>       m_bookmarks;      // 块号
    QSet<int>       m_breakpoints;    // 块号
    std::function<bool(const QString&)> m_symbolChecker;
    std::function<QList<JumpTarget>(const QString&)> m_jumpResolver;
    QPoint          m_lastMousePos;
    QString         m_filePath;
    bool            m_assembly = false;
};
