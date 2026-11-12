#include "ui/CodeEditor.h"

#include "core/UvprojModel.h"
#include "ui/CSyntaxHighlighter.h"
#include "ui/KeilStyle.h"

#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QPainter>
#include <QTextBlock>
#include <QTextCodec>
#include <QTextStream>

// ---------------------------------------------------------------- 行号栏

class LineNumberArea : public QWidget
{
public:
    explicit LineNumberArea(CodeEditor* editor)
        : QWidget(editor)
        , m_editor(editor)
    {
    }

    QSize sizeHint() const override
    {
        return QSize(m_editor->lineNumberAreaWidth(), 0);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        m_editor->lineNumberAreaPaintEvent(event);
    }

private:
    CodeEditor* m_editor;
};

// ---------------------------------------------------------------- CodeEditor

CodeEditor::CodeEditor(QWidget* parent)
    : QPlainTextEdit(parent)
{
    m_lineNumberArea = new LineNumberArea(this);

    m_pointSize = KeilStyle::editorFont().pointSizeF();
    applyFont();
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setWordWrapMode(QTextOption::NoWrap);

    connect(this, &CodeEditor::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &CodeEditor::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &CodeEditor::cursorPositionChanged, this, &CodeEditor::onCursorMoved);

    updateLineNumberAreaWidth(0);
    highlightCurrentLine();
    setupContextMenu();
}

void CodeEditor::setupContextMenu()
{
    setContextMenuPolicy(Qt::DefaultContextMenu);
}

void CodeEditor::applyFont()
{
    QFont f = KeilStyle::editorFont();
    f.setPointSizeF(m_pointSize);
    setFont(f);
    setTabStopDistance(QFontMetricsF(f).horizontalAdvance(QLatin1Char(' ')) * 4);
    updateLineNumberAreaWidth(0);
}

void CodeEditor::setZoomPointSize(qreal pointSize)
{
    const qreal clamped = qBound(6.0, pointSize, 48.0);
    if (qFuzzyCompare(clamped, m_pointSize))
        return;

    m_pointSize = clamped;
    applyFont();
    highlightCurrentLine();
}

void CodeEditor::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const int steps = event->angleDelta().y() / 120;
        if (steps != 0)
            emit zoomRequested(steps);
        event->accept();
        return;
    }
    QPlainTextEdit::wheelEvent(event);
}

void CodeEditor::setHighlightAssembly(bool assembly)
{
    m_assembly = assembly;
    if (document())
        new CSyntaxHighlighter(document(), assembly);
}

bool CodeEditor::loadFile(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    // 自动识别 UTF-8 / 本地编码，避免中文注释在 GBK 工程里乱码
    const QByteArray raw = file.readAll();
    file.close();

    QString text;
    if (raw.startsWith("\xEF\xBB\xBF")) {
        text = QString::fromUtf8(raw.mid(3));
    } else {
        QTextCodec* utf8 = QTextCodec::codecForName("UTF-8");
        QTextCodec::ConverterState state;
        text = utf8->toUnicode(raw.constData(), raw.size(), &state);
        if (state.invalidChars > 0)
            text = QString::fromLocal8Bit(raw);
    }

    setPlainText(text);
    m_filePath = QFileInfo(path).absoluteFilePath();
    document()->setModified(false);

    setHighlightAssembly(fileKindFromPath(path) == FileKind::Assembly);
    return true;
}

bool CodeEditor::saveFile(QString* error)
{
    if (m_filePath.isEmpty())
        return false;

    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    QTextStream ts(&file);
    ts.setCodec("UTF-8");
    ts << toPlainText();
    ts.flush();
    file.close();

    document()->setModified(false);
    return true;
}

void CodeEditor::setFilePath(const QString& path)
{
    m_filePath = path;
}

void CodeEditor::gotoLine(int line, int column)
{
    const QTextBlock block = document()->findBlockByLineNumber(qMax(0, line - 1));
    if (!block.isValid())
        return;

    QTextCursor cursor(block);
    if (column > 1)
        cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, column - 1);
    setTextCursor(cursor);
    centerCursor();
    setFocus();
}

int CodeEditor::lineNumberAreaWidth() const
{
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) {
        max /= 10;
        ++digits;
    }
    return 10 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * (digits + 1);
}

void CodeEditor::updateLineNumberAreaWidth(int)
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect& rect, int dy)
{
    if (dy)
        m_lineNumberArea->scroll(0, dy);
    else
        m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth(0);
}

void CodeEditor::resizeEvent(QResizeEvent* event)
{
    QPlainTextEdit::resizeEvent(event);

    const QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeEditor::highlightCurrentLine()
{
    QList<QTextEdit::ExtraSelection> selections;
    if (!isReadOnly()) {
        QTextEdit::ExtraSelection selection;
        selection.format.setBackground(QColor(0xea, 0xf2, 0xfb));
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        selections.append(selection);
    }
    setExtraSelections(selections);
}

void CodeEditor::onCursorMoved()
{
    const QTextCursor c = textCursor();
    emit cursorPositionChanged(c.blockNumber() + 1, c.positionInBlock() + 1);
    highlightCurrentLine();
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent* event)
{
    QPainter painter(m_lineNumberArea);
    painter.fillRect(event->rect(), QColor(0xf2, 0xf2, 0xf2));
    painter.setPen(QColor(0xc0, 0xc0, 0xc0));
    painter.drawLine(event->rect().topRight(), event->rect().bottomRight());

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    painter.setPen(QColor(0x80, 0x80, 0x80));

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            painter.drawText(0, top, m_lineNumberArea->width() - 6,
                             fontMetrics().height(), Qt::AlignRight, QString::number(blockNumber + 1));
        }

        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}
