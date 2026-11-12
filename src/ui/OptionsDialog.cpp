#include "ui/OptionsDialog.h"

#include "ui/KeilStyle.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

// ---------------------------------------------------------------- Options

OptionsDialog::OptionsDialog(const Target& target, const QString& projectDir, QWidget* parent)
    : QDialog(parent)
    , m_target(target)
{
    setWindowTitle(QStringLiteral("Options for Target '%1'").arg(target.name));
    setModal(true);
    buildUi(projectDir);
    loadFromTarget();
    resize(560, 460);
}

void OptionsDialog::buildUi(const QString& projectDir)
{
    Q_UNUSED(projectDir);

    auto* layout = new QVBoxLayout(this);

    auto* tabs = new QTabWidget(this);
    layout->addWidget(tabs, 1);

    // ---- Target ----
    {
        auto* page = new QWidget;
        auto* form = new QFormLayout(page);

        auto* device = new QLineEdit(m_target.device);
        device->setReadOnly(true);
        auto* vendor = new QLineEdit(m_target.vendor);
        vendor->setReadOnly(true);
        auto* cpu = new QLineEdit(m_target.displayCpu());
        cpu->setReadOnly(true);
        auto* pack = new QLineEdit(m_target.packId);
        pack->setReadOnly(true);

        form->addRow(QStringLiteral("Device:"), device);
        form->addRow(QStringLiteral("Vendor:"), vendor);
        form->addRow(QStringLiteral("CPU:"), cpu);
        form->addRow(QStringLiteral("Pack (仅显示):"), pack);

        tabs->addTab(page, QStringLiteral("Target"));
    }

    // ---- Output ----
    {
        auto* page = new QWidget;
        auto* form = new QFormLayout(page);

        m_outputName = new QLineEdit;
        m_outputDir = new QLineEdit;
        m_listingDir = new QLineEdit;
        m_createHex = new QCheckBox(QStringLiteral("Create HEX File"));
        m_debugInfo = new QCheckBox(QStringLiteral("Debug Information"));
        m_browseInfo = new QCheckBox(QStringLiteral("Browse Information"));
        m_createLib = new QCheckBox(QStringLiteral("Create Library"));

        form->addRow(QStringLiteral("Name of Executable:"), m_outputName);
        form->addRow(QStringLiteral("Output Directory:"), m_outputDir);
        form->addRow(QStringLiteral("Listing Directory:"), m_listingDir);
        form->addRow(QString(), m_createHex);
        form->addRow(QString(), m_debugInfo);
        form->addRow(QString(), m_browseInfo);
        form->addRow(QString(), m_createLib);

        tabs->addTab(page, QStringLiteral("Output"));
    }

    // ---- C/C++ ----
    {
        auto* page = new QWidget;
        auto* form = new QFormLayout(page);

        m_optimization = new QComboBox;
        m_optimization->addItem(QStringLiteral("Level 0 (-O0)"), 0);
        m_optimization->addItem(QStringLiteral("Level 1 (-O1)"), 1);
        m_optimization->addItem(QStringLiteral("Level 2 (-O2)"), 2);
        m_optimization->addItem(QStringLiteral("Level 3 (-O3)"), 3);

        m_c99 = new QCheckBox(QStringLiteral("C99 Mode (--c99)"));
        m_oneElf = new QCheckBox(QStringLiteral("One ELF Section per Function"));
        m_strict = new QCheckBox(QStringLiteral("Strict ANSI"));
        m_enumInt = new QCheckBox(QStringLiteral("Enum as int"));
        m_plainChar = new QCheckBox(QStringLiteral("Plain char is signed"));
        m_interwork = new QCheckBox(QStringLiteral("Interworking (--apcs=interwork)"));
        m_noWarnings = new QCheckBox(QStringLiteral("No Warnings (-W)"));

        m_defines = new QLineEdit;
        m_includes = new QLineEdit;
        m_cMisc = new QLineEdit;
        m_defines->setPlaceholderText(QStringLiteral("USE_HAL_DRIVER,STM32F103xB"));
        m_includes->setPlaceholderText(QStringLiteral(".\\Inc;.\\Drivers\\CMSIS\\Include"));

        form->addRow(QStringLiteral("Optimization:"), m_optimization);
        form->addRow(QString(), m_c99);
        form->addRow(QString(), m_oneElf);
        form->addRow(QString(), m_strict);
        form->addRow(QString(), m_enumInt);
        form->addRow(QString(), m_plainChar);
        form->addRow(QString(), m_interwork);
        form->addRow(QString(), m_noWarnings);
        form->addRow(QStringLiteral("Define:"), m_defines);
        form->addRow(QStringLiteral("Include Paths:"), m_includes);
        form->addRow(QStringLiteral("Misc Controls:"), m_cMisc);

        tabs->addTab(page, QStringLiteral("C/C++"));
    }

    // ---- Linker ----
    {
        auto* page = new QWidget;
        auto* form = new QFormLayout(page);

        m_useMemoryLayout = new QCheckBox(QStringLiteral("Use Memory Layout from Target Dialog"));
        m_textAddr = new QLineEdit;
        m_dataAddr = new QLineEdit;
        m_scatter = new QLineEdit;
        m_microlib = new QCheckBox(QStringLiteral("Use MicroLIB"));
        m_noStdLib = new QCheckBox(QStringLiteral("Don't search Standard Libraries"));
        m_linkerMisc = new QLineEdit;

        auto* scatterRow = new QWidget;
        auto* scatterLayout = new QHBoxLayout(scatterRow);
        scatterLayout->setContentsMargins(0, 0, 0, 0);
        scatterLayout->addWidget(m_scatter, 1);
        auto* browse = new QPushButton(QStringLiteral("..."));
        browse->setFixedWidth(28);
        scatterLayout->addWidget(browse);

        connect(browse, &QPushButton::clicked, this, [this]() {
            const QString f = QFileDialog::getOpenFileName(
                this, QStringLiteral("Select Scatter File"), QString(),
                QStringLiteral("Scatter Files (*.sct);;All Files (*)"));
            if (!f.isEmpty())
                m_scatter->setText(f);
        });

        form->addRow(QString(), m_useMemoryLayout);
        form->addRow(QStringLiteral("IROM1 Start:"), m_textAddr);
        form->addRow(QStringLiteral("IRAM1 Start:"), m_dataAddr);
        form->addRow(QStringLiteral("Scatter File:"), scatterRow);
        form->addRow(QString(), m_microlib);
        form->addRow(QString(), m_noStdLib);
        form->addRow(QStringLiteral("Misc Controls:"), m_linkerMisc);

        tabs->addTab(page, QStringLiteral("Linker"));
    }

    // ---- Debug ----
    {
        auto* page = new QWidget;
        auto* v = new QVBoxLayout(page);
        auto* label = new QLabel(
            QStringLiteral("调试会话（GDB / OpenOCD 后端）将在后续步骤接入。\n\n"
                           "当前版本已实现：工程解析、引用配置、编译、链接、HEX 生成。"));
        label->setWordWrap(true);
        v->addWidget(label);
        v->addStretch(1);
        tabs->addTab(page, QStringLiteral("Debug"));
    }

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(box);

    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void OptionsDialog::accept()
{
    saveToTarget();
    QDialog::accept();
}

void OptionsDialog::loadFromTarget()
{
    m_outputName->setText(m_target.outputName);
    m_outputDir->setText(m_target.outputDirectory);
    m_listingDir->setText(m_target.listingPath);
    m_createHex->setChecked(m_target.createHexFile);
    m_debugInfo->setChecked(m_target.debugInformation);
    m_browseInfo->setChecked(m_target.browseInformation);
    m_createLib->setChecked(m_target.createLib);

    m_optimization->setCurrentIndex(qBound(0, m_target.optimization, 3));
    m_c99->setChecked(m_target.c99);
    m_oneElf->setChecked(m_target.oneElfSectionPerFunction);
    m_strict->setChecked(m_target.strict);
    m_enumInt->setChecked(m_target.enumIsInt);
    m_plainChar->setChecked(m_target.plainChar);
    m_interwork->setChecked(m_target.interwork);
    m_noWarnings->setChecked(m_target.noWarnings);
    m_defines->setText(m_target.defines);
    m_includes->setText(m_target.includePaths);
    m_cMisc->setText(m_target.miscControls);

    m_useMemoryLayout->setChecked(m_target.useMemoryLayout);
    m_textAddr->setText(m_target.textAddressRange);
    m_dataAddr->setText(m_target.dataAddressRange);
    m_scatter->setText(m_target.scatterFile);
    m_microlib->setChecked(m_target.useMicrolib);
    m_noStdLib->setChecked(m_target.noStartLib);
    m_linkerMisc->setText(m_target.linkerMisc);
}

void OptionsDialog::saveToTarget()
{
    m_target.outputName = m_outputName->text().trimmed();
    m_target.outputDirectory = m_outputDir->text().trimmed();
    m_target.listingPath = m_listingDir->text().trimmed();
    m_target.createHexFile = m_createHex->isChecked();
    m_target.debugInformation = m_debugInfo->isChecked();
    m_target.browseInformation = m_browseInfo->isChecked();
    m_target.createLib = m_createLib->isChecked();

    m_target.optimization = m_optimization->currentData().toInt();
    m_target.c99 = m_c99->isChecked();
    m_target.oneElfSectionPerFunction = m_oneElf->isChecked();
    m_target.strict = m_strict->isChecked();
    m_target.enumIsInt = m_enumInt->isChecked();
    m_target.plainChar = m_plainChar->isChecked();
    m_target.interwork = m_interwork->isChecked();
    m_target.noWarnings = m_noWarnings->isChecked();
    m_target.warningLevel = m_noWarnings->isChecked() ? 0 : 2;
    m_target.defines = m_defines->text();
    m_target.includePaths = m_includes->text();
    m_target.miscControls = m_cMisc->text();

    m_target.useMemoryLayout = m_useMemoryLayout->isChecked();
    m_target.textAddressRange = m_textAddr->text().trimmed();
    m_target.dataAddressRange = m_dataAddr->text().trimmed();
    m_target.scatterFile = m_scatter->text().trimmed();
    m_target.useMicrolib = m_microlib->isChecked();
    m_target.noStartLib = m_noStdLib->isChecked();
    m_target.linkerMisc = m_linkerMisc->text();
}

// ---------------------------------------------------------------- Toolchain

ToolchainDialog::ToolchainDialog(const Toolchain& current, QWidget* parent)
    : QDialog(parent)
    , m_toolchain(current)
{
    setWindowTitle(QStringLiteral("Configure Toolchain"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;

    auto* dirRow = new QWidget;
    auto* dirLayout = new QHBoxLayout(dirRow);
    dirLayout->setContentsMargins(0, 0, 0, 0);

    m_dir = new QLineEdit(current.binDir);
    m_family = new QComboBox;
    m_family->addItem(QStringLiteral("AC5 (armcc)"), QStringLiteral("AC5"));
    m_family->addItem(QStringLiteral("AC6 (armclang)"), QStringLiteral("AC6"));
    m_family->setCurrentIndex(current.family == QLatin1String("AC6") ? 1 : 0);

    auto* browseButton = new QPushButton(QStringLiteral("Browse..."));
    dirLayout->addWidget(m_dir, 1);
    dirLayout->addWidget(browseButton);

    form->addRow(QStringLiteral("Compiler Folder:"), dirRow);
    form->addRow(QStringLiteral("Family:"), m_family);
    layout->addLayout(form);

    auto* hint = new QLabel(
        QStringLiteral("填 armcc.exe / armclang.exe 所在目录，例如：\n"
                       "    C:/Keil_v5/ARM/ARMCC/bin        (AC5)\n"
                       "    C:/Keil_v5/ARM/ARMCLANG/bin     (AC6)\n"
                       "本程序不分发任何 ARM 编译器，授权由你自己的 Keil MDK 提供。"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    m_status = new QLabel;
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(box);

    connect(browseButton, &QPushButton::clicked, this, &ToolchainDialog::browse);
    connect(m_dir, &QLineEdit::textChanged, this, &ToolchainDialog::refreshStatus);
    connect(m_family, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { refreshStatus(); });
    connect(box, &QDialogButtonBox::accepted, this, &ToolchainDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    refreshStatus();
    resize(520, 300);
}

void ToolchainDialog::browse()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select ARM Compiler bin folder"), m_dir->text());
    if (!dir.isEmpty())
        m_dir->setText(QDir::toNativeSeparators(dir));
}

void ToolchainDialog::refreshStatus()
{
    const QString family = m_family->currentData().toString();
    m_toolchain = Toolchain::detect(m_dir->text().trimmed(), family);

    if (m_toolchain.isValid()) {
        m_status->setText(QStringLiteral("Detected: %1    compiler = %2")
                              .arg(m_toolchain.family, m_toolchain.cc));
        m_status->setStyleSheet(QStringLiteral("color: #1a7f37;"));
    } else {
        m_status->setText(QStringLiteral("未在该目录找到 armcc.exe / armclang.exe"));
        m_status->setStyleSheet(QStringLiteral("color: #b01a1a;"));
    }
}

void ToolchainDialog::accept()
{
    refreshStatus();
    if (!m_toolchain.isValid()) {
        m_status->setText(QStringLiteral("请选择一个包含 armcc.exe / armclang.exe 的目录"));
        return;
    }
    QDialog::accept();
}

bool ToolchainDialog::configure(QWidget* parent, Toolchain& tc)
{
    ToolchainDialog dlg(tc, parent);
    if (dlg.exec() != QDialog::Accepted)
        return false;
    tc = dlg.result();
    return true;
}
