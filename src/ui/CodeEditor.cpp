#include "ui/CodeEditor.h"

#include "core/Encoding.h"
#include "core/UvprojModel.h"
#include "ui/CSyntaxHighlighter.h"
#include "ui/KeilStyle.h"

#include <QContextMenuEvent>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QPainter>
#include <QTextBlock>
#include <QTextCodec>
#include <QTextStream>

namespace {

QTextCodec* codecForEncoding(const QString& name)
{
    if (name == QLatin1String("System"))
        return QTextCodec::codecForLocale();
    if (QTextCodec* c = QTextCodec::codecForName(name.toLatin1()))
        return c;
    return QTextCodec::codecForName("UTF-8");
}

QString decodeBytes(const QByteArray& raw, const QString& encoding, bool* hasInvalid)
{
    QTextCodec* codec = codecForEncoding(encoding);
    QTextCodec::ConverterState state;
    const QString text = codec->toUnicode(raw.constData(), raw.size(), &state);
    if (hasInvalid)
        *hasInvalid = state.invalidChars > 0;
    return text;
}

} // namespace

QStringList CodeEditor::encodingChoices()
{
    return Encoding::choices();
}

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

void CodeEditor::applyFormattedText(const QString& text)
{
    if (text == toPlainText())
        return;

    const int position = textCursor().position();

    QTextCursor cursor(document());
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.insertText(text);
    cursor.endEditBlock();

    // 尽量把光标放回原处（行变了就夹到文档末尾）
    QTextCursor restored(document());
    restored.setPosition(qBound(0, position, qMax(0, document()->characterCount() - 1)));
    setTextCursor(restored);

    document()->setModified(true);
}

void CodeEditor::mousePressEvent(QMouseEvent* event)
{
    if ((event->modifiers() & Qt::ControlModifier) && event->button() == Qt::LeftButton
        && !m_filePath.isEmpty()) {

        const QTextCursor cursor = cursorForPosition(event->pos());
        const QString lineText = cursor.block().text();

        // #include "xxx.h" / <xxx.h>
        static const QRegularExpression includeRe(
            QStringLiteral("^[ \t]*#[ \t]*include[ \t]*[<\"]([^>\"]+)[>\"]"));
        const QRegularExpressionMatch includeMatch = includeRe.match(lineText);
        if (includeMatch.hasMatch()) {
            emit includeActivated(includeMatch.captured(1), m_filePath);
            event->accept();
            return;
        }

        // 自己按正则取标识符，比 WordUnderCursor 可靠：
        // 后者会把 "();" 这种标点也当成一个词
        static const QRegularExpression identifierRe(
            QStringLiteral("[A-Za-z_][A-Za-z0-9_]*"));

        const int column = cursor.positionInBlock();
        QString word;
        QRegularExpressionMatchIterator it = identifierRe.globalMatch(lineText);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            if (column >= m.capturedStart() && column <= m.capturedEnd()) {
                word = m.captured();
                break;
            }
        }

        if (!word.isEmpty()) {
            emit wordActivated(word, m_filePath);
            event->accept();
            return;
        }
    }

    QPlainTextEdit::mousePressEvent(event);
}

void CodeEditor::mouseMoveEvent(QMouseEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier)
        viewport()->setCursor(Qt::PointingHandCursor);
    else
        viewport()->unsetCursor();

    QPlainTextEdit::mouseMoveEvent(event);
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

    const QByteArray raw = file.readAll();
    file.close();

    // 默认按 UTF-8 解；解不开再退回系统编码，保证老 GBK 工程也能读
    QString text;
    QString used = m_encoding;

    if (raw.startsWith("\xEF\xBB\xBF")) {
        text = QString::fromUtf8(raw.mid(3));
        used = QStringLiteral("UTF-8");
    } else {
        bool invalid = false;
        text = decodeBytes(raw, m_encoding, &invalid);
        if (invalid && m_encoding == QLatin1String("UTF-8")) {
            bool altInvalid = false;
            const QString alt = decodeBytes(raw, QStringLiteral("System"), &altInvalid);
            if (!altInvalid) {
                text = alt;
                used = QStringLiteral("System");
            }
        }
    }

    setPlainText(text);
    m_filePath = QFileInfo(path).absoluteFilePath();
    document()->setModified(false);

    setHighlightAssembly(fileKindFromPath(path) == FileKind::Assembly);

    if (m_encoding != used) {
        m_encoding = used;
        emit encodingChanged(m_encoding);
    }
    return true;
}

void CodeEditor::setEncoding(const QString& name, bool reloadFromDisk)
{
    const QString target = name.isEmpty() ? QStringLiteral("UTF-8") : name;

    if (reloadFromDisk && !m_filePath.isEmpty()) {
        if (document()->isModified()) {
            const auto answer = QMessageBox::question(
                this, QStringLiteral("Encoding"),
                QStringLiteral("File has unsaved changes. Reload with %1 and lose them?")
                    .arg(target),
                QMessageBox::Yes | QMessageBox::No);
            if (answer != QMessageBox::Yes)
                return;
        }
        m_encoding = target;
        QString error;
        loadFile(m_filePath, &error);
        return;
    }

    m_encoding = target;
    emit encodingChanged(m_encoding);
}

void CodeEditor::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);

    QAction* actUndo = menu.addAction(QStringLiteral("Undo"));
    actUndo->setEnabled(document()->isUndoAvailable());
    connect(actUndo, &QAction::triggered, this, [this]() { undo(); });

    QAction* actRedo = menu.addAction(QStringLiteral("Redo"));
    actRedo->setEnabled(document()->isRedoAvailable());
    connect(actRedo, &QAction::triggered, this, [this]() { redo(); });

    menu.addSeparator();

    QAction* actCopy = menu.addAction(QStringLiteral("Copy"));
    actCopy->setEnabled(textCursor().hasSelection());
    connect(actCopy, &QAction::triggered, this, [this]() { copy(); });

    QAction* actSelectAll = menu.addAction(QStringLiteral("Select All"));
    connect(actSelectAll, &QAction::triggered, this, [this]() { selectAll(); });

    menu.addSeparator();

    QMenu* encodingMenu = menu.addMenu(QStringLiteral("Encoding"));
    for (const QString& name : encodingChoices()) {
        QAction* action = encodingMenu->addAction(name);
        action->setCheckable(true);
        action->setChecked(name == m_encoding);
        connect(action, &QAction::triggered, this, [this, name]() {
            setEncoding(name, true);
        });
    }

    menu.exec(event->globalPos());
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
    ts.setCodec(codecForEncoding(m_encoding));
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
        selection.format.setBackground(QColor(0xf2, 0xf7, 0xff));
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
    painter.fillRect(event->rect(), QColor(0xfa, 0xfb, 0xfc));
    painter.setPen(QColor(0xe8, 0xea, 0xed));
    painter.drawLine(event->rect().topRight(), event->rect().bottomRight());

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    painter.setPen(QColor(0xa0, 0xa7, 0xb2));

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
