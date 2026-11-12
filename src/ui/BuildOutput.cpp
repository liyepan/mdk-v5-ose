#include "ui/BuildOutput.h"

#include "ui/KeilStyle.h"

#include <QHeaderView>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>

namespace {

// 只需要"双击"这一个信号，用回调代替 Q_OBJECT，省掉一个 moc 文件
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

    m_errors = new QTreeWidget(this);
    m_errors->setRootIsDecorated(false);
    m_errors->setUniformRowHeights(true);
    m_errors->setAlternatingRowColors(true);
    m_errors->setColumnCount(5);
    m_errors->setHeaderLabels({ QStringLiteral("Severity"), QStringLiteral("Code"),
                                QStringLiteral("Description"), QStringLiteral("File"),
                                QStringLiteral("Line") });
    m_errors->header()->setStretchLastSection(false);
    m_errors->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_tabs->addTab(m_errors, QStringLiteral("Error List"));

    connect(m_errors, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
                if (!item)
                    return;
                const QString file = item->data(0, Qt::UserRole).toString();
                const int line = item->data(0, Qt::UserRole + 1).toInt();
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
    m_log->appendPlainText(line);
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
    item->setData(0, Qt::UserRole, d.file);
    item->setData(0, Qt::UserRole + 1, d.line);

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
