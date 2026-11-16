#include "ui/FileOptionsDialog.h"

#include "core/Lang.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

FileOptionsDialog::FileOptionsDialog(Kind kind, const QString& title, QWidget* parent)
    : QDialog(parent)
    , m_kind(kind)
{
    setWindowTitle(title);
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 12);
    layout->setSpacing(10);

    auto* form = new QFormLayout;
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);

    m_include = new QCheckBox(Lang::t("Include in Build"));
    m_include->setChecked(true);
    form->addRow(QString(), m_include);

    m_defines = new QLineEdit;
    m_defines->setPlaceholderText(QStringLiteral("USE_STDPERIPH_DRIVER,GD32F10X"));
    m_includePaths = new QLineEdit;
    m_includePaths->setPlaceholderText(QStringLiteral(".\\Inc;.\\Firmware"));
    m_misc = new QLineEdit;
    m_misc->setPlaceholderText(QStringLiteral("-O2 --split_sections"));

    form->addRow(Lang::t("Define:"), m_defines);
    form->addRow(Lang::t("Include Paths:"), m_includePaths);
    form->addRow(Lang::t("Misc Controls:"), m_misc);

    layout->addLayout(form);

    auto* hint = new QLabel(
        (kind == Kind::File)
            ? QStringLiteral("这三个留空就继承分组和 Target 的设置；填了只对这个文件生效。")
            : QStringLiteral("这三个留空就继承 Target 的设置；填了对这个分组里的所有文件生效。"));
    hint->setStyleSheet(QStringLiteral("color: #5b6270;"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    resize(540, 250);
}

void FileOptionsDialog::setIncludeInBuild(bool on)
{
    m_include->setChecked(on);
}

void FileOptionsDialog::setOptions(const QString& defines, const QString& includePaths,
                                   const QString& misc)
{
    m_defines->setText(defines);
    m_includePaths->setText(includePaths);
    m_misc->setText(misc);
}

bool FileOptionsDialog::includeInBuild() const
{
    return m_include->isChecked();
}

QString FileOptionsDialog::defines() const
{
    return m_defines->text().trimmed();
}

QString FileOptionsDialog::includePaths() const
{
    return m_includePaths->text().trimmed();
}

QString FileOptionsDialog::misc() const
{
    return m_misc->text().trimmed();
}
