#include "ui/CodeEditor.h"

#include "core/Encoding.h"
#include "core/UvprojModel.h"
#include "ui/CSyntaxHighlighter.h"
#include "ui/KeilStyle.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QFile>
#include <QKeyEvent>
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

    // 没有鼠标跟踪就不会收到"没按键的移动"，Ctrl 悬停效果也就出不来
    viewport()->setMouseTracking(true);
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

    // 按"第几行第几列"记光标，不能记字符偏移：
    // 格式化会改缩进，行首凭空多出几个空格，偏移量一平移光标就跑到词中间去了。
    const QTextCursor before = textCursor();
    const int line = before.blockNumber();
    const int column = before.positionInBlock();

    QTextCursor cursor(document());
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.insertText(text);
    cursor.endEditBlock();

    const int lastBlock = document()->blockCount() - 1;
    QTextBlock block = document()->findBlockByNumber(qBound(0, line, lastBlock));
    QTextCursor restored(block);
    // 列也不能越界：这行格式化后可能变短了
    const int maxColumn = qMax(0, block.length() - 1);
    restored.setPosition(block.position() + qMin(column, maxColumn));
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

bool CodeEditor::identifierAt(const QTextCursor& cursor, QString* word, int* start,
                              int* end) const
{
    static const QRegularExpression identifierRe(QStringLiteral("[A-Za-z_][A-Za-z0-9_]*"));

    const QString line = cursor.block().text();
    const int column = cursor.positionInBlock();

    QRegularExpressionMatchIterator it = identifierRe.globalMatch(line);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        if (column >= m.capturedStart() && column <= m.capturedEnd()) {
            if (word)
                *word = m.captured();
            if (start)
                *start = m.capturedStart();
            if (end)
                *end = m.capturedEnd();
            return true;
        }
    }
    return false;
}

void CodeEditor::setSymbolChecker(std::function<bool(const QString&)> checker)
{
    m_symbolChecker = std::move(checker);
}

void CodeEditor::setJumpResolver(std::function<QList<JumpTarget>(const QString&)> resolver)
{
    m_jumpResolver = std::move(resolver);
}

// 光标是否落在 #include "xxx.h" 的文件名上
bool CodeEditor::includeAt(const QTextCursor& cursor, int* start, int* end) const
{
    static const QRegularExpression includeRe(
        QStringLiteral("^[ \t]*#[ \t]*include[ \t]*[<\"]([^>\"]+)[>\"]"));

    const QRegularExpressionMatch m = includeRe.match(cursor.block().text());
    if (!m.hasMatch())
        return false;

    const int column = cursor.positionInBlock();
    if (column < m.capturedStart(1) || column > m.capturedEnd(1))
        return false;

    if (start)
        *start = m.capturedStart(1);
    if (end)
        *end = m.capturedEnd(1);
    return true;
}

// 右键时的位置 -> 标识符
QString CodeEditor::wordAtPosition(const QPoint& pos) const
{
    if (m_filePath.isEmpty())
        return QString();

    const QTextCursor cursor = cursorForPosition(pos);
    QString word;
    int start = 0;
    int end = 0;
    if (identifierAt(cursor, &word, &start, &end))
        return word;
    return QString();
}

// ---------------------------------------------------------------- 编辑操作

void CodeEditor::indentSelection()
{
    QTextCursor cursor = textCursor();
    const int first = document()->findBlock(cursor.selectionStart()).blockNumber();
    const int last = document()->findBlock(cursor.selectionEnd()).blockNumber();

    cursor.beginEditBlock();
    for (int i = first; i <= last; ++i) {
        QTextCursor line(document()->findBlockByNumber(i));
        line.movePosition(QTextCursor::StartOfBlock);
        line.insertText(QStringLiteral("    "));
    }
    cursor.endEditBlock();
}

void CodeEditor::unindentSelection()
{
    QTextCursor cursor = textCursor();
    const int first = document()->findBlock(cursor.selectionStart()).blockNumber();
    const int last = document()->findBlock(cursor.selectionEnd()).blockNumber();

    cursor.beginEditBlock();
    for (int i = first; i <= last; ++i) {
        const QTextBlock block = document()->findBlockByNumber(i);
        const QString text = block.text();

        int remove = 0;
        while (remove < 4 && remove < text.size() && text.at(remove) == QLatin1Char(' '))
            ++remove;
        if (remove == 0 && !text.isEmpty() && text.at(0) == QLatin1Char('	'))
            remove = 1;
        if (remove == 0)
            continue;

        QTextCursor line(block);
        line.movePosition(QTextCursor::StartOfBlock);
        line.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, remove);
        line.removeSelectedText();
    }
    cursor.endEditBlock();
}

void CodeEditor::commentSelection()
{
    QTextCursor cursor = textCursor();
    const int first = document()->findBlock(cursor.selectionStart()).blockNumber();
    const int last = document()->findBlock(cursor.selectionEnd()).blockNumber();

    cursor.beginEditBlock();
    for (int i = first; i <= last; ++i) {
        const QTextBlock block = document()->findBlockByNumber(i);
        const QString text = block.text();
        if (text.trimmed().isEmpty())
            continue;

        int indent = 0;
        while (indent < text.size()
               && (text.at(indent) == QLatin1Char(' ') || text.at(indent) == QLatin1Char('	')))
            ++indent;
        if (text.mid(indent).startsWith(QLatin1String("//")))
            continue;

        QTextCursor line(block);
        line.movePosition(QTextCursor::StartOfBlock);
        line.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, indent);
        line.insertText(QStringLiteral("// "));
    }
    cursor.endEditBlock();
}

void CodeEditor::uncommentSelection()
{
    QTextCursor cursor = textCursor();
    const int first = document()->findBlock(cursor.selectionStart()).blockNumber();
    const int last = document()->findBlock(cursor.selectionEnd()).blockNumber();

    cursor.beginEditBlock();
    for (int i = first; i <= last; ++i) {
        const QTextBlock block = document()->findBlockByNumber(i);
        const QString text = block.text();

        int indent = 0;
        while (indent < text.size()
               && (text.at(indent) == QLatin1Char(' ') || text.at(indent) == QLatin1Char('	')))
            ++indent;
        if (!text.mid(indent).startsWith(QLatin1String("//")))
            continue;

        int remove = 2;
        if (text.mid(indent + 2, 1) == QLatin1String(" "))
            ++remove;

        QTextCursor line(block);
        line.movePosition(QTextCursor::StartOfBlock);
        line.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, indent);
        line.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, remove);
        line.removeSelectedText();
    }
    cursor.endEditBlock();
}

bool CodeEditor::findNextText(const QString& text, bool forward)
{
    if (text.isEmpty())
        return false;

    QTextDocument::FindFlags flags;
    if (!forward)
        flags |= QTextDocument::FindBackward;

    QTextCursor found = document()->find(text, textCursor(), flags);
    if (found.isNull()) {
        // 回绕到文件头/尾再找一次
        QTextCursor from(document());
        if (!forward)
            from.movePosition(QTextCursor::End);
        found = document()->find(text, from, flags);
    }

    if (found.isNull())
        return false;

    setTextCursor(found);
    centerCursor();
    return true;
}

// ---------------------------------------------------------------- 书签

void CodeEditor::toggleBookmarkAtCursor()
{
    const int number = textCursor().blockNumber();
    if (m_bookmarks.contains(number))
        m_bookmarks.remove(number);
    else
        m_bookmarks.insert(number);
    m_lineNumberArea->update();
}

void CodeEditor::removeBookmarkAtCursor()
{
    m_bookmarks.remove(textCursor().blockNumber());
    m_lineNumberArea->update();
}

void CodeEditor::gotoNextBookmark()
{
    if (m_bookmarks.isEmpty())
        return;

    const int current = textCursor().blockNumber();
    for (int n = current + 1; n < blockCount(); ++n) {
        if (m_bookmarks.contains(n)) {
            gotoLine(n + 1);
            return;
        }
    }
    gotoLine(*m_bookmarks.begin() + 1);     // 回绕到第一个
}

void CodeEditor::toggleBreakpointAtCursor()
{
    const int number = textCursor().blockNumber();
    if (m_breakpoints.contains(number))
        m_breakpoints.remove(number);
    else
        m_breakpoints.insert(number);
    m_lineNumberArea->update();
}

void CodeEditor::removeBreakpointAtCursor()
{
    m_breakpoints.remove(textCursor().blockNumber());
    m_lineNumberArea->update();
}

void CodeEditor::clearBreakpoints()
{
    m_breakpoints.clear();
    m_lineNumberArea->update();
}

void CodeEditor::gotoPrevBookmark()
{
    if (m_bookmarks.isEmpty())
        return;

    const int current = textCursor().blockNumber();
    for (int n = current - 1; n >= 0; --n) {
        if (m_bookmarks.contains(n)) {
            gotoLine(n + 1);
            return;
        }
    }
    gotoLine(*m_bookmarks.begin() + 1);
}

void CodeEditor::updateHoverSelection(const QTextCursor& cursor, bool ctrlHeld)
{
    QTextCursor hover;

    // 只要能 Ctrl+左键跳过去的东西，悬停时都画下划线：
    //   #include "x.h" 里的头文件名、符号表里有的标识符
    if (ctrlHeld && !m_filePath.isEmpty() && !cursor.isNull()) {
        int start = 0;
        int end = 0;
        bool underline = false;

        if (includeAt(cursor, &start, &end)) {
            underline = true;               // 头文件能不能打开在点击时才判断
        } else {
            QString word;
            if (identifierAt(cursor, &word, &start, &end))
                underline = !m_symbolChecker || m_symbolChecker(word);
        }

        if (underline) {
            hover = cursor;
            hover.setPosition(cursor.block().position() + start);
            hover.setPosition(cursor.block().position() + end, QTextCursor::KeepAnchor);
        }
    }

    const bool changed = hover.hasSelection() != m_hoverCursor.hasSelection()
                         || hover.anchor() != m_hoverCursor.anchor()
                         || hover.position() != m_hoverCursor.position();
    if (!changed)
        return;

    m_hoverCursor = hover;

    // 注意：收的时候必须显式设回 I 型光标。
    // unsetCursor() 只是"不再用自定义光标"，viewport 会退回默认箭头，
    // 于是 Ctrl 悬停过一次之后，编辑器里就再也看不到文本光标了。
    if (m_hoverCursor.hasSelection())
        viewport()->setCursor(Qt::PointingHandCursor);
    else
        viewport()->setCursor(Qt::IBeamCursor);

    highlightCurrentLine();      // 里面会把 hover 下划线一起重建
}

void CodeEditor::mouseMoveEvent(QMouseEvent* event)
{
    m_lastMousePos = event->pos();
    if (event->modifiers() & Qt::ControlModifier)
        updateHoverSelection(cursorForPosition(event->pos()), true);
    else
        updateHoverSelection(QTextCursor(), false);

    QPlainTextEdit::mouseMoveEvent(event);
}

// 鼠标不动、只按/松 Ctrl 也要立刻出（收）下划线
void CodeEditor::refreshHover()
{
    const bool ctrl = (QApplication::keyboardModifiers() & Qt::ControlModifier) != 0;
    if (ctrl && !m_filePath.isEmpty())
        updateHoverSelection(cursorForPosition(m_lastMousePos), true);
    else
        updateHoverSelection(QTextCursor(), false);
}

void CodeEditor::keyPressEvent(QKeyEvent* event)
{
    QPlainTextEdit::keyPressEvent(event);
    if (event->key() == Qt::Key_Control)
        refreshHover();
}

void CodeEditor::keyReleaseEvent(QKeyEvent* event)
{
    QPlainTextEdit::keyReleaseEvent(event);
    if (event->key() == Qt::Key_Control)
        refreshHover();
}

void CodeEditor::leaveEvent(QEvent* event)
{
    updateHoverSelection(QTextCursor(), false);
    QPlainTextEdit::leaveEvent(event);
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

    // ---- 跳转（只对光标下能识别的标识符出现）----
    const QString word = wordAtPosition(event->pos());
    if (!word.isEmpty() && m_jumpResolver) {
        const QList<JumpTarget> targets = m_jumpResolver(word);
        for (const JumpTarget& target : targets) {
            QAction* act = menu.addAction(target.label);
            act->setToolTip(target.file);
            connect(act, &QAction::triggered, this, [this, target]() {
                emit jumpRequested(target.file, target.line);
            });
        }
        if (!targets.isEmpty())
            menu.addSeparator();
    }

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

    // Ctrl 悬停在可跳转的符号上时加一条下划线
    if (m_hoverCursor.hasSelection()) {
        QTextEdit::ExtraSelection hover;
        hover.format.setUnderlineStyle(QTextCharFormat::SingleUnderline);
        hover.format.setUnderlineColor(QColor(0x0b, 0x57, 0xd0));
        hover.cursor = m_hoverCursor;
        selections.append(hover);
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

            if (m_breakpoints.contains(blockNumber)) {
                painter.setPen(QPen(QColor(0xa0, 0x10, 0x10), 1));
                painter.setBrush(QColor(0xd8, 0x2b, 0x2b));
                painter.drawEllipse(QPointF(5, top + fontMetrics().height() / 2.0), 3.2, 3.2);
                painter.setBrush(Qt::NoBrush);
            }

            if (m_bookmarks.contains(blockNumber)) {
                painter.fillRect(QRect(3, top + 2, 4, fontMetrics().height() - 4),
                                 QColor(0x2f, 0x7c, 0xd8));
            }
        }

        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}
