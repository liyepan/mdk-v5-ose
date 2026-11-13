#include "ui/FindReplaceDialog.h"

#include "core/Lang.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

FindReplaceDialog::FindReplaceDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(Lang::t("Find / Replace"));
    setModal(false);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 10);
    layout->setSpacing(8);

    auto* form = new QGridLayout;
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(6);

    m_find = new QLineEdit;
    m_replace = new QLineEdit;
    form->addWidget(new QLabel(Lang::t("Find:")), 0, 0);
    form->addWidget(m_find, 0, 1, 1, 3);
    form->addWidget(new QLabel(Lang::t("Replace:")), 1, 0);
    form->addWidget(m_replace, 1, 1, 1, 3);

    m_case = new QCheckBox(Lang::t("Match case"));
    m_whole = new QCheckBox(Lang::t("Whole word"));
    form->addWidget(m_case, 2, 1);
    form->addWidget(m_whole, 2, 2);
    layout->addLayout(form);

    auto* buttons = new QHBoxLayout;
    auto* findNext = new QPushButton(Lang::t("Find Next"));
    auto* findPrev = new QPushButton(Lang::t("Find Previous"));
    auto* replace = new QPushButton(Lang::t("Replace"));
    auto* replaceAll = new QPushButton(Lang::t("Replace All"));
    auto* close = new QPushButton(Lang::t("Close"));
    buttons->addWidget(findNext);
    buttons->addWidget(findPrev);
    buttons->addStretch(1);
    buttons->addWidget(replace);
    buttons->addWidget(replaceAll);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    m_status = new QLabel;
    m_status->setStyleSheet(QStringLiteral("color: #5b6270;"));
    layout->addWidget(m_status);

    connect(findNext, &QPushButton::clicked, this, [this]() {
        emit findNextRequested(true);
    });
    connect(findPrev, &QPushButton::clicked, this, [this]() {
        emit findNextRequested(false);
    });
    connect(replace, &QPushButton::clicked, this, [this]() {
        emit replaceRequested();
    });
    connect(replaceAll, &QPushButton::clicked, this, [this]() {
        emit replaceAllRequested();
    });
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(m_find, &QLineEdit::returnPressed, this, [this]() {
        emit findNextRequested(true);
    });

    resize(430, 190);
}

QString FindReplaceDialog::findText() const
{
    return m_find->text();
}

QString FindReplaceDialog::replaceText() const
{
    return m_replace->text();
}

bool FindReplaceDialog::caseSensitive() const
{
    return m_case->isChecked();
}

bool FindReplaceDialog::wholeWord() const
{
    return m_whole->isChecked();
}

void FindReplaceDialog::prepare(const QString& selection, bool replaceMode)
{
    if (!selection.isEmpty() && !selection.contains(QLatin1Char('\n')))
        m_find->setText(selection);
    m_find->selectAll();
    if (replaceMode)
        m_replace->setFocus();
    else
        m_find->setFocus();
}

void FindReplaceDialog::setStatus(const QString& text)
{
    m_status->setText(text);
}

void FindReplaceDialog::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    if (event->key() == Qt::Key_F3) {
        emit findNextRequested(!(event->modifiers() & Qt::ShiftModifier));
        return;
    }
    QDialog::keyPressEvent(event);
}
