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

    // 格式化后整体替换，保留撤销栈
    void applyFormattedText(const QString& text);

    // 字号缩放（高 DPI 屏上真正管用的那个"缩放"）
    void  setZoomPointSize(qreal pointSize);
    qreal zoomPointSize() const { return m_pointSize; }

    int  lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent* event);

signals:
    void cursorPositionChanged(int line, int column);
    void zoomRequested(int delta);
    // Ctrl+左键点击标识符
    void wordActivated(const QString& word, const QString& filePath);
    // Ctrl+左键点击 #include "x.h"
    void includeActivated(const QString& includeName, const QString& filePath);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea(const QRect& rect, int dy);
    void highlightCurrentLine();
    void onCursorMoved();

private:
    void setupContextMenu();
    void applyFont();

    LineNumberArea* m_lineNumberArea = nullptr;
    qreal           m_pointSize = 0.0;
    QString         m_filePath;
    bool            m_assembly = false;
};
