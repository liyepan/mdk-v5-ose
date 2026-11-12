#pragma once

#include <QFont>
#include <QPlainTextEdit>

class LineNumberArea;

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

    int  lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent* event);

signals:
    void cursorPositionChanged(int line, int column);

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea(const QRect& rect, int dy);
    void highlightCurrentLine();
    void onCursorMoved();

private:
    void setupContextMenu();

    LineNumberArea* m_lineNumberArea = nullptr;
    QString         m_filePath;
    bool            m_assembly = false;
};
