#include "ui/OptionsDialog.h"
#include "core/Flasher.h"
#include "core/Formatter.h"
#include "core/Lang.h"
#include "core/Settings.h"

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
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

// ---------------------------------------------------------------- Options

OptionsDialog::OptionsDialog(const Target& target, const QString& projectDir, QWidget* parent)
    : QDialog(parent)
    , m_target(target)
{
    setWindowTitle(Lang::t("Options for Target '%1'").arg(target.name));
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

        form->addRow(Lang::t("Device:"), device);
        form->addRow(Lang::t("Vendor:"), vendor);
        form->addRow(Lang::t("CPU:"), cpu);
        form->addRow(QStringLiteral("Pack (仅显示):"), pack);

        tabs->addTab(page, Lang::t("Target"));
    }

    // ---- Output ----
    {
        auto* page = new QWidget;
        auto* form = new QFormLayout(page);

        m_outputName = new QLineEdit;
        m_outputDir = new QLineEdit;
        m_listingDir = new QLineEdit;
        m_createHex = new QCheckBox(Lang::t("Create HEX File"));
        m_debugInfo = new QCheckBox(Lang::t("Debug Information"));
        m_browseInfo = new QCheckBox(Lang::t("Browse Information"));
        m_createLib = new QCheckBox(Lang::t("Create Library"));

        form->addRow(Lang::t("Name of Executable:"), m_outputName);
        form->addRow(Lang::t("Output Directory:"), m_outputDir);
        form->addRow(Lang::t("Listing Directory:"), m_listingDir);
        form->addRow(QString(), m_createHex);
        form->addRow(QString(), m_debugInfo);
        form->addRow(QString(), m_browseInfo);
        form->addRow(QString(), m_createLib);

        tabs->addTab(page, Lang::t("Output"));
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

        m_c99 = new QCheckBox(Lang::t("C99 Mode (--c99)"));
        m_oneElf = new QCheckBox(Lang::t("One ELF Section per Function"));
        m_strict = new QCheckBox(Lang::t("Strict ANSI"));
        m_enumInt = new QCheckBox(Lang::t("Enum as int"));
        m_plainChar = new QCheckBox(Lang::t("Plain char is signed"));
        m_interwork = new QCheckBox(Lang::t("Interworking (--apcs=interwork)"));
        m_noWarnings = new QCheckBox(Lang::t("No Warnings (-W)"));

        m_defines = new QLineEdit;
        m_includes = new QLineEdit;
        m_cMisc = new QLineEdit;
        m_defines->setPlaceholderText(QStringLiteral("USE_HAL_DRIVER,STM32F103xB"));
        m_includes->setPlaceholderText(QStringLiteral(".\\Inc;.\\Drivers\\CMSIS\\Include"));

        form->addRow(Lang::t("Optimization:"), m_optimization);
        form->addRow(QString(), m_c99);
        form->addRow(QString(), m_oneElf);
        form->addRow(QString(), m_strict);
        form->addRow(QString(), m_enumInt);
        form->addRow(QString(), m_plainChar);
        form->addRow(QString(), m_interwork);
        form->addRow(QString(), m_noWarnings);
        form->addRow(Lang::t("Define:"), m_defines);
        form->addRow(Lang::t("Include Paths:"), m_includes);
        form->addRow(Lang::t("Misc Controls:"), m_cMisc);

        tabs->addTab(page, Lang::t("C/C++"));
    }

    // ---- Linker ----
    {
        auto* page = new QWidget;
        auto* form = new QFormLayout(page);

        m_useMemoryLayout = new QCheckBox(Lang::t("Use Memory Layout from Target Dialog"));
        m_textAddr = new QLineEdit;
        m_dataAddr = new QLineEdit;
        m_scatter = new QLineEdit;
        m_microlib = new QCheckBox(Lang::t("Use MicroLIB"));
        m_noStdLib = new QCheckBox(Lang::t("Don't search Standard Libraries"));
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
                this, Lang::t("Select Scatter File"), QString(),
                Lang::t("Scatter Files (*.sct);;All Files (*)"));
            if (!f.isEmpty())
                m_scatter->setText(f);
        });

        form->addRow(QString(), m_useMemoryLayout);
        form->addRow(Lang::t("IROM1 Start:"), m_textAddr);
        form->addRow(Lang::t("IRAM1 Start:"), m_dataAddr);
        form->addRow(Lang::t("Scatter File:"), scatterRow);
        form->addRow(QString(), m_microlib);
        form->addRow(QString(), m_noStdLib);
        form->addRow(Lang::t("Misc Controls:"), m_linkerMisc);

        tabs->addTab(page, Lang::t("Linker"));
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

// ---------------------------------------------------------------- Flash
FlashDialog::FlashDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(Lang::t("Flash Download Settings"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 12);
    layout->setSpacing(8);

    auto* form = new QFormLayout;
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);

    Settings& settings = Settings::instance();

    m_backend = new QComboBox;
    m_backend->addItem(QStringLiteral("J-Link (JLink.exe)"), QStringLiteral("jlink"));
    m_backend->addItem(QStringLiteral("OpenOCD"), QStringLiteral("openocd"));
    m_backend->addItem(QStringLiteral("pyOCD"), QStringLiteral("pyocd"));
    m_backend->addItem(Lang::t("Custom command"), QStringLiteral("custom"));
    for (int i = 0; i < m_backend->count(); ++i) {
        if (m_backend->itemData(i).toString() == settings.flashBackend()) {
            m_backend->setCurrentIndex(i);
            break;
        }
    }
    form->addRow(Lang::t("Backend:"), m_backend);

    auto* toolRow = new QWidget;
    auto* toolLayout = new QHBoxLayout(toolRow);
    toolLayout->setContentsMargins(0, 0, 0, 0);
    m_toolPath = new QLineEdit(settings.flashToolPath());
    m_toolPath->setPlaceholderText(QStringLiteral("留空 = 自动探测"));
    auto* browse = new QPushButton(Lang::t("Browse..."));
    toolLayout->addWidget(m_toolPath, 1);
    toolLayout->addWidget(browse);
    form->addRow(Lang::t("Tool path:"), toolRow);

    m_interface = new QComboBox;
    m_interface->addItem(QStringLiteral("SWD"));
    m_interface->addItem(QStringLiteral("JTAG"));
    m_interface->setCurrentText(settings.flashInterface());
    form->addRow(Lang::t("Interface:"), m_interface);

    m_speed = new QSpinBox;
    m_speed->setRange(100, 50000);
    m_speed->setSingleStep(1000);
    m_speed->setSuffix(QStringLiteral(" kHz"));
    m_speed->setValue(settings.flashSpeed());
    form->addRow(Lang::t("Speed:"), m_speed);

    m_verify = new QCheckBox(Lang::t("Verify after download"));
    m_verify->setChecked(settings.flashVerify());
    form->addRow(QString(), m_verify);

    m_resetAfter = new QCheckBox(Lang::t("Reset and run after download"));
    m_resetAfter->setChecked(settings.flashResetAfter());
    form->addRow(QString(), m_resetAfter);

    m_ocdInterface = new QLineEdit(settings.flashOpenOcdInterface());
    m_ocdTarget = new QLineEdit(settings.flashOpenOcdTarget());
    m_ocdTarget->setPlaceholderText(QStringLiteral("target/gd32f10x.cfg"));
    form->addRow(QStringLiteral("OpenOCD -f:"), m_ocdInterface);
    form->addRow(QStringLiteral("OpenOCD target:"), m_ocdTarget);
    m_ocdIfaceLabel = form->labelForField(m_ocdInterface);
    m_ocdTargetLabel = form->labelForField(m_ocdTarget);

    m_custom = new QLineEdit(settings.flashCustomCommand());
    m_custom->setPlaceholderText(
        QStringLiteral("openocd -f ... -c \"program {file} verify reset exit\""));
    form->addRow(Lang::t("Custom command:"), m_custom);
    m_customLabel = form->labelForField(m_custom);

    layout->addLayout(form);

    m_status = new QLabel;
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(box);

    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browse, &QPushButton::clicked, this, &FlashDialog::browseTool);
    connect(m_backend, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { refreshStatus(); });
    connect(m_toolPath, &QLineEdit::textChanged, this, [this](const QString&) { refreshStatus(); });

    refreshStatus();
    resize(560, 380);
}

void FlashDialog::browseTool()
{
    const QString backend = m_backend->currentData().toString();
    QString filter = QStringLiteral("All Files (*)");
    if (backend == QLatin1String("jlink"))
        filter = QStringLiteral("J-Link (*.exe JLink JLinkExe);;All Files (*)");
    const QString file = QFileDialog::getOpenFileName(
        this, Lang::t("Select flash tool"), m_toolPath->text(), filter);
    if (!file.isEmpty())
        m_toolPath->setText(QDir::toNativeSeparators(file));
}

void FlashDialog::refreshStatus()
{
    const QString backend = m_backend->currentData().toString();
    const bool custom = (backend == QLatin1String("custom"));
    const bool openocd = (backend == QLatin1String("openocd"));

    // 和后端无关的行直接藏起来，别堆在那儿碍事
    m_ocdInterface->setVisible(openocd);
    m_ocdTarget->setVisible(openocd);
    m_custom->setVisible(custom);
    if (m_ocdIfaceLabel)
        m_ocdIfaceLabel->setVisible(openocd);
    if (m_ocdTargetLabel)
        m_ocdTargetLabel->setVisible(openocd);
    if (m_customLabel)
        m_customLabel->setVisible(custom);
    m_interface->setEnabled(!custom && !openocd && backend != QLatin1String("pyocd"));
    m_speed->setEnabled(!custom && !openocd);

    if (custom) {
        m_status->setText(QStringLiteral("占位符：{file} {device} {interface} {speed} {addr}"));
        m_status->setStyleSheet(QStringLiteral("color: #5b6270;"));
        return;
    }

    const QString typed = m_toolPath->text().trimmed();
    const QString found = typed.isEmpty() ? Flasher::detectTool(backend) : typed;

    if (found.isEmpty()) {
        m_status->setText(QStringLiteral("没找到烧写工具，请指定路径"));
        m_status->setStyleSheet(QStringLiteral("color: #b01a1a;"));
    } else {
        m_status->setText(QStringLiteral("将使用: %1").arg(QDir::toNativeSeparators(found)));
        m_status->setStyleSheet(QStringLiteral("color: #1a7f37;"));
    }
}

bool FlashDialog::configure(QWidget* parent)
{
    FlashDialog dialog(parent);
    if (dialog.exec() != QDialog::Accepted)
        return false;

    Settings& settings = Settings::instance();
    settings.setFlashBackend(dialog.m_backend->currentData().toString());
    settings.setFlashToolPath(dialog.m_toolPath->text().trimmed());
    settings.setFlashInterface(dialog.m_interface->currentText());
    settings.setFlashSpeed(dialog.m_speed->value());
    settings.setFlashVerify(dialog.m_verify->isChecked());
    settings.setFlashResetAfter(dialog.m_resetAfter->isChecked());
    settings.setFlashOpenOcdInterface(dialog.m_ocdInterface->text().trimmed());
    settings.setFlashOpenOcdTarget(dialog.m_ocdTarget->text().trimmed());
    settings.setFlashCustomCommand(dialog.m_custom->text().trimmed());
    settings.sync();
    return true;
}

// ---------------------------------------------------------------- Toolchain

ToolchainDialog::ToolchainDialog(const Toolchain& current, QWidget* parent)
    : QDialog(parent)
    , m_toolchain(current)
{
    setWindowTitle(Lang::t("Configure Toolchain"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 12);
    layout->setSpacing(8);

    auto* form = new QFormLayout;
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);

    auto* dirRow = new QWidget;
    auto* dirLayout = new QHBoxLayout(dirRow);
    dirLayout->setContentsMargins(0, 0, 0, 0);

    m_dir = new QLineEdit(current.binDir);
    m_family = new QComboBox;
    m_family->addItem(QStringLiteral("AC5 (armcc)"), QStringLiteral("AC5"));
    m_family->addItem(QStringLiteral("AC6 (armclang)"), QStringLiteral("AC6"));
    m_family->setCurrentIndex(current.family == QLatin1String("AC6") ? 1 : 0);

    auto* browseButton = new QPushButton(Lang::t("Browse..."));
    dirLayout->addWidget(m_dir, 1);
    dirLayout->addWidget(browseButton);

    form->addRow(Lang::t("Compiler Folder:"), dirRow);
    form->addRow(Lang::t("Family:"), m_family);

    // ---- clang-format（格式化引擎）----
    auto* clangRow = new QWidget;
    auto* clangLayout = new QHBoxLayout(clangRow);
    clangLayout->setContentsMargins(0, 0, 0, 0);

    m_clangFormat = new QLineEdit(Settings::instance().clangFormatPath());
    m_clangFormat->setPlaceholderText(QStringLiteral("留空 = 自动探测"));

    auto* clangBrowse = new QPushButton(Lang::t("Browse..."));
    auto* clangAuto = new QPushButton(QStringLiteral("自动探测"));
    clangLayout->addWidget(m_clangFormat, 1);
    clangLayout->addWidget(clangBrowse);
    clangLayout->addWidget(clangAuto);

    form->addRow(QStringLiteral("clang-format:"), clangRow);

    // 风格：空 = 自动
    m_formatStyle = new QComboBox;
    m_formatStyle->addItem(QStringLiteral("自动（工程里有 .clang-format 就用它）"), QString());
    m_formatStyle->addItem(QStringLiteral("内置（4 空格缩进 + Allman，贴近 Keil 代码习惯）"),
                           QStringLiteral("__builtin"));
    for (const char* name : {"LLVM", "Google", "Chromium", "Mozilla", "WebKit", "Microsoft", "GNU"})
        m_formatStyle->addItem(QString::fromLatin1(name), QString::fromLatin1(name));

    const QString savedStyle = Settings::instance().formatStyle();
    int styleIndex = 0;
    for (int i = 0; i < m_formatStyle->count(); ++i) {
        if (m_formatStyle->itemData(i).toString() == savedStyle) {
            styleIndex = i;
            break;
        }
    }
    m_formatStyle->setCurrentIndex(styleIndex);
    form->addRow(QStringLiteral("格式化风格:"), m_formatStyle);

    layout->addLayout(form);

    layout->addSpacing(2);

    auto* hint = new QLabel(
        QStringLiteral("编译器目录填 armcc.exe / armclang.exe 所在目录，例如\n"
                       "    C:/Keil_v5/ARM/ARMCC/bin        (AC5)\n"
                       "    C:/Keil_v5/ARM/ARMCLANG/bin     (AC6)\n"
                       "本程序不分发任何 ARM 编译器，授权由你自己的 Keil MDK 提供。"));
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #5b6270;"));
    layout->addWidget(hint);

    layout->addSpacing(2);

    m_status = new QLabel;
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    m_clangStatus = new QLabel;
    m_clangStatus->setWordWrap(true);
    layout->addWidget(m_clangStatus);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(box);

    connect(browseButton, &QPushButton::clicked, this, &ToolchainDialog::browse);
    connect(clangBrowse, &QPushButton::clicked, this, &ToolchainDialog::browseClangFormat);
    connect(clangAuto, &QPushButton::clicked, this, [this]() {
        // 把自动探测到的路径直接填进去，方便用户确认到底用了哪一份
        const QString found = Formatter::detectClangFormat();
        m_clangFormat->setText(QDir::toNativeSeparators(found));
    });
    connect(m_clangFormat, &QLineEdit::textChanged, this,
            [this](const QString&) { refreshClangStatus(); });
    connect(m_formatStyle, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { refreshClangStatus(); });
    connect(m_dir, &QLineEdit::textChanged, this, &ToolchainDialog::refreshStatus);
    connect(m_family, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { refreshStatus(); });
    connect(box, &QDialogButtonBox::accepted, this, &ToolchainDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    refreshStatus();
    refreshClangStatus();
    resize(560, 360);
}

void ToolchainDialog::browseClangFormat()
{
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择 clang-format"),
        QFileInfo(m_clangFormat->text()).absolutePath(),
        QStringLiteral("clang-format (clang-format*);;All Files (*)"));
    if (!file.isEmpty())
        m_clangFormat->setText(QDir::toNativeSeparators(file));
}

void ToolchainDialog::refreshClangStatus()
{
    const QString typed = m_clangFormat->text().trimmed();

    QString path;
    if (!typed.isEmpty()) {
        if (QFileInfo(typed).isFile())
            path = typed;
    } else {
        path = Formatter::detectClangFormat();
    }

    const QString style = m_formatStyle ? m_formatStyle->currentData().toString() : QString();
    QString styleText;
    if (style.isEmpty())
        styleText = QStringLiteral("，风格：自动（有 .clang-format 就按它）");
    else if (style == QLatin1String("__builtin"))
        styleText = QStringLiteral("，风格：内置（4 空格 + Allman）");
    else
        styleText = QStringLiteral("，风格：%1").arg(style);

    if (!path.isEmpty()) {
        m_clangStatus->setText(QStringLiteral("格式化将使用: %1%2")
                                   .arg(QDir::toNativeSeparators(path), styleText));
        m_clangStatus->setStyleSheet(QStringLiteral("color: #1a7f37;"));
    } else if (!typed.isEmpty()) {
        m_clangStatus->setText(QStringLiteral("这个文件不存在: %1").arg(typed));
        m_clangStatus->setStyleSheet(QStringLiteral("color: #b01a1a;"));
    } else {
        m_clangStatus->setText(
            QStringLiteral("没找到 clang-format：格式化只能用它，"
                           "装一个 LLVM 或在这里指定路径。"));
        m_clangStatus->setStyleSheet(QStringLiteral("color: #b01a1a;"));
    }
}

void ToolchainDialog::browse()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, Lang::t("Select ARM Compiler bin folder"), m_dir->text());
    if (!dir.isEmpty())
        m_dir->setText(QDir::toNativeSeparators(dir));
}

void ToolchainDialog::refreshStatus()
{
    const QString family = m_family->currentData().toString();
    m_toolchain = Toolchain::detect(m_dir->text().trimmed(), family);

    if (m_toolchain.isValid()) {
        m_status->setText(Lang::t("Detected: %1    compiler = %2")
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

    // clang-format 路径和风格是应用级设置，直接存掉（这个对话框是唯一能改它的地方）
    Settings::instance().setClangFormatPath(m_clangFormat->text().trimmed());
    Settings::instance().setFormatStyle(m_formatStyle->currentData().toString());
    Settings::instance().sync();

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
