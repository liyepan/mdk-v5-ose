#include "ui/BuildOutput.h"

#include "ui/KeilStyle.h"

#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QScrollBar>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextStream>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>

namespace {

// 一次构建输出里各种行的角色，决定颜色
enum class LogKind { Normal, Dim, Header, Error, Warning, Info, Command, Size, Success };

LogKind classifyLine(const QString& line)
{
    Diagnostic d;
    if (Diagnostics::parseLine(line, QString(), &d)) {
        switch (d.severity) {
        case Severity::Error:   return LogKind::Error;
        case Severity::Warning: return LogKind::Warning;
        default:                return LogKind::Info;
        }
    }

    if (line.startsWith(QLatin1String("*** Executing:")))
        return LogKind::Command;
    // 注意：中文不能用 QLatin1String（会按 Latin-1 解码，匹配不上）
    if (line.startsWith(QLatin1String("*** Error")) || line.contains(QStringLiteral("链接失败"))
        || line.contains(QStringLiteral("用户命令失败")))
        return LogKind::Error;
    if (line.startsWith(QLatin1String("*** Warning")) || line.contains(QStringLiteral("已忽略")))
        return LogKind::Warning;
    if (line.startsWith(QLatin1String("Build started"))
        || line.startsWith(QLatin1String("Clean started"))
        || line.startsWith(QLatin1String("Clean finished"))
        || line.startsWith(QLatin1String("*** Using Compiler"))
        || line.startsWith(QLatin1String("Build target")))
        return LogKind::Header;
    if (line.startsWith(QLatin1String("compiling "))
        || line.startsWith(QLatin1String("assembling ")))
        return LogKind::Dim;
    if (line.startsWith(QLatin1String("Program Size:")))
        return LogKind::Size;
    if (line.contains(QLatin1String("Error(s),")) && line.contains(QLatin1String("Warning(s)")))
        return line.contains(QLatin1String(" 0 Error(s)")) ? LogKind::Success : LogKind::Error;
    return LogKind::Normal;
}

QTextCharFormat formatForLine(const QString& line)
{
    QTextCharFormat fmt;

    switch (classifyLine(line)) {
    case LogKind::Dim:
        fmt.setForeground(QColor(0x6b, 0x72, 0x80));
        break;
    case LogKind::Header:
        fmt.setForeground(QColor(0x0b, 0x57, 0xd0));
        fmt.setFontWeight(QFont::DemiBold);
        break;
    case LogKind::Error:
        fmt.setForeground(QColor(0xb4, 0x23, 0x18));
        fmt.setFontWeight(QFont::DemiBold);
        break;
    case LogKind::Warning:
        fmt.setForeground(QColor(0xb2, 0x5e, 0x09));
        break;
    case LogKind::Info:
        fmt.setForeground(QColor(0x6b, 0x72, 0x80));
        break;
    case LogKind::Command:
        fmt.setForeground(QColor(0x82, 0x50, 0xdf));
        break;
    case LogKind::Size:
        fmt.setForeground(QColor(0x1a, 0x7f, 0x37));
        fmt.setFontWeight(QFont::DemiBold);
        break;
    case LogKind::Success:
        fmt.setForeground(QColor(0x1a, 0x7f, 0x37));
        fmt.setFontWeight(QFont::DemiBold);
        break;
    case LogKind::Normal:
    default:
        fmt.setForeground(QColor(0x1f, 0x23, 0x28));
        break;
    }
    return fmt;
}

const int kRoleFile = Qt::UserRole;
const int kRoleLine = Qt::UserRole + 1;
const int kRoleRaw  = Qt::UserRole + 2;

// 文本日志：回车双击跳转，Ctrl+C 复制。QPlainTextEdit 本身支持选中，
// 这里只补双击和"全选/复制全部"的快捷键。
class LogView : public QPlainTextEdit
{
public:
    explicit LogView(QWidget* parent = nullptr)
        : QPlainTextEdit(parent)
    {
    }

    std::function<void()> onDoubleClick;

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        QPlainTextEdit::mouseDoubleClickEvent(event);
        if (onDoubleClick && event->button() == Qt::LeftButton)
            onDoubleClick();
    }
};

// Error List 的行是列表项而不是文本，Ctrl+C 默认什么都不会发生。
// 这里把它接上，并且支持 Ctrl+Shift+C 复制全部。
class DiagnosticView : public QTreeWidget
{
public:
    explicit DiagnosticView(QWidget* parent = nullptr)
        : QTreeWidget(parent)
    {
    }

    std::function<void(bool onlySelected)> onCopy;
    std::function<void()> onCopyAll;

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        const bool shift = (event->modifiers() & Qt::ShiftModifier) != 0;
        const bool ctrlC = (event->modifiers() & Qt::ControlModifier)
                           && event->key() == Qt::Key_C;

        // Ctrl+Shift+C 在有些平台上不会被 matches(Copy) 认出来，所以自己判一次
        if (ctrlC || event->matches(QKeySequence::Copy)) {
            if (shift) {
                if (onCopyAll)
                    onCopyAll();
            } else {
                if (onCopy)
                    onCopy(true);
            }
            event->accept();
            return;
        }
        QTreeWidget::keyPressEvent(event);
    }
};

} // namespace

BuildOutput::BuildOutput(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    layout->addWidget(m_tabs);

    auto* log = new LogView(this);
    m_log = log;
    m_log->setReadOnly(true);
    m_log->setFont(KeilStyle::editorFont());
    m_log->setMaximumBlockCount(50000);
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_tabs->addTab(m_log, QStringLiteral("Build Output"));

    log->onDoubleClick = [this]() {
        if (!m_log)
            return;
        const QString text = m_log->textCursor().block().text();
        Diagnostic d;
        if (Diagnostics::parseLine(text, m_projectDir, &d) && d.hasLocation())
            emit jumpRequested(d.file, d.line);
    };

    m_log->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_log, &QWidget::customContextMenuRequested, this, &BuildOutput::buildLogMenu);

    auto* errors = new DiagnosticView(this);
    m_errors = errors;
    m_errors->setRootIsDecorated(false);
    m_errors->setUniformRowHeights(true);
    m_errors->setAlternatingRowColors(true);
    m_errors->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_errors->setColumnCount(5);
    m_errors->setHeaderLabels({ QStringLiteral("Severity"), QStringLiteral("Code"),
                                QStringLiteral("Description"), QStringLiteral("File"),
                                QStringLiteral("Line") });
    m_errors->header()->setStretchLastSection(false);
    m_errors->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_tabs->addTab(m_errors, QStringLiteral("Error List"));

    m_errors->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_errors, &QWidget::customContextMenuRequested, this, &BuildOutput::buildErrorMenu);

    errors->onCopy = [this](bool onlySelected) { copyDiagnostics(onlySelected); };
    errors->onCopyAll = [this]() { copyDiagnostics(false); };

    connect(m_errors, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
                if (!item)
                    return;
                const QString file = item->data(0, kRoleFile).toString();
                const int line = item->data(0, kRoleLine).toInt();
                if (!file.isEmpty())
                    emit jumpRequested(file, line);
            });
}

void BuildOutput::setProjectDir(const QString& dir)
{
    m_projectDir = dir;
}

void BuildOutput::clear()
{
    m_log->clear();
}

void BuildOutput::appendLine(const QString& line)
{
    // 用光标插入而不是 appendPlainText，才能按行类型上色
    QTextCursor cursor(m_log->document());
    cursor.movePosition(QTextCursor::End);
    if (!m_log->document()->isEmpty())
        cursor.insertBlock();
    cursor.insertText(line, formatForLine(line));

    QScrollBar* sb = m_log->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void BuildOutput::addDiagnostic(const Diagnostic& d)
{
    auto* item = new QTreeWidgetItem(m_errors);
    item->setText(0, d.severityText());
    item->setText(1, d.code);
    item->setText(2, d.message);
    item->setText(3, d.file.isEmpty() ? QStringLiteral("<project>") : d.file);
    item->setText(4, d.line > 0 ? QString::number(d.line) : QString());
    item->setData(0, kRoleFile, d.file);
    item->setData(0, kRoleLine, d.line);
    item->setData(0, kRoleRaw, d.raw);

    if (d.severity == Severity::Error)
        item->setForeground(0, QColor(0xb0, 0x1a, 0x1a));
    else if (d.severity == Severity::Warning)
        item->setForeground(0, QColor(0x9a, 0x62, 0x00));
}

void BuildOutput::clearDiagnostics()
{
    m_errors->clear();
}

void BuildOutput::showErrorList()
{
    m_tabs->setCurrentWidget(m_errors);
}

void BuildOutput::showLog()
{
    m_tabs->setCurrentWidget(m_log);
}

// ---------------------------------------------------------------- 复制

void BuildOutput::putOnClipboard(const QString& text)
{
    if (text.isEmpty())
        return;
    QApplication::clipboard()->setText(text);
}

QString BuildOutput::diagnosticsAsText(bool onlySelected) const
{
    // 优先给出编译器原始行（".\main.c", line 45: Error: #20: ...），
    // 这样贴回 Keil / 邮件里还是原样；没有原始行时再按列拼一条。
    QList<QTreeWidgetItem*> items;
    if (onlySelected) {
        items = m_errors->selectedItems();
    } else {
        items.reserve(m_errors->topLevelItemCount());
        for (int i = 0; i < m_errors->topLevelItemCount(); ++i)
            items.append(m_errors->topLevelItem(i));
    }

    QStringList out;
    out.reserve(items.size());

    for (QTreeWidgetItem* item : items) {
        if (!item)
            continue;
        const QString raw = item->data(0, kRoleRaw).toString();
        if (!raw.isEmpty()) {
            out << raw;
            continue;
        }
        const QString file = item->text(3);
        const QString line = item->text(4);
        out << QStringLiteral("%1(%2): %3 %4: %5")
                   .arg(file, line, item->text(0), item->text(1), item->text(2));
    }
    return out.join(QLatin1Char('\n'));
}

void BuildOutput::copyDiagnostics(bool onlySelected)
{
    if (onlySelected && m_errors->selectedItems().isEmpty()) {
        // 没有选中就当成"复制全部"，避免 Ctrl+C 按下去毫无反应
        putOnClipboard(diagnosticsAsText(false));
        return;
    }
    putOnClipboard(diagnosticsAsText(onlySelected));
}

void BuildOutput::copyLogSelection()
{
    const QString text = m_log->textCursor().selectedText();
    if (!text.isEmpty()) {
        // selectedText() 用 U+2029 当换行
        QString normalized = text;
        normalized.replace(QChar(0x2029), QLatin1Char('\n'));
        putOnClipboard(normalized);
        return;
    }
    copyLogAll();
}

void BuildOutput::copyLogAll()
{
    putOnClipboard(m_log->toPlainText());
}

void BuildOutput::saveLogAs()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Save Build Output"), QStringLiteral("build_output.txt"),
        QStringLiteral("Text Files (*.txt);;All Files (*)"));
    if (path.isEmpty())
        return;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    QTextStream ts(&file);
    ts.setCodec("UTF-8");
    ts << m_log->toPlainText();
    ts.flush();
    file.commit();
}

// ---------------------------------------------------------------- 右键菜单

void BuildOutput::buildLogMenu(const QPoint& pos)
{
    const QPoint globalPos = m_log->viewport()->mapToGlobal(pos);

    QMenu menu(this);
    const bool hasSelection = m_log->textCursor().hasSelection();

    QAction* copy = menu.addAction(QStringLiteral("Copy"));
    copy->setShortcut(QKeySequence::Copy);
    copy->setEnabled(hasSelection);

    QAction* copyAll = menu.addAction(QStringLiteral("Copy All"));
    copyAll->setShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_C));

    QAction* selectAll = menu.addAction(QStringLiteral("Select All"));
    selectAll->setShortcut(QKeySequence::SelectAll);

    menu.addSeparator();

    QAction* clearAction = menu.addAction(QStringLiteral("Clear"));
    QAction* saveAction = menu.addAction(QStringLiteral("Save Output As..."));

    QAction* chosen = menu.exec(globalPos);
    if (!chosen)
        return;

    if (chosen == copy)
        copyLogSelection();
    else if (chosen == copyAll)
        copyLogAll();
    else if (chosen == selectAll)
        m_log->selectAll();
    else if (chosen == clearAction)
        m_log->clear();
    else if (chosen == saveAction)
        saveLogAs();
}

void BuildOutput::buildErrorMenu(const QPoint& pos)
{
    const QPoint globalPos = m_errors->viewport()->mapToGlobal(pos);
    const bool hasSelection = !m_errors->selectedItems().isEmpty();

    QMenu menu(this);

    QAction* copy = menu.addAction(QStringLiteral("Copy"));
    copy->setShortcut(QKeySequence::Copy);
    copy->setEnabled(hasSelection);

    QAction* copyAll = menu.addAction(QStringLiteral("Copy All"));
    copyAll->setShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_C));

    QAction* selectAll = menu.addAction(QStringLiteral("Select All"));
    selectAll->setShortcut(QKeySequence::SelectAll);

    menu.addSeparator();

    QAction* goTo = menu.addAction(QStringLiteral("Go to Source"));
    QTreeWidgetItem* item = m_errors->itemAt(pos);
    goTo->setEnabled(item != nullptr);

    QAction* chosen = menu.exec(globalPos);
    if (!chosen)
        return;

    if (chosen == copy)
        copyDiagnostics(true);
    else if (chosen == copyAll)
        copyDiagnostics(false);
    else if (chosen == selectAll)
        m_errors->selectAll();
    else if (chosen == goTo && item)
        emit jumpRequested(item->data(0, kRoleFile).toString(),
                           item->data(0, kRoleLine).toInt());
}
