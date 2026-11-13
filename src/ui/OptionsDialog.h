#pragma once

#include <QDialog>

#include "core/Toolchain.h"
#include "core/UvprojModel.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;

// Project -> Options for Target
class OptionsDialog : public QDialog
{
    Q_OBJECT

public:
    OptionsDialog(const Target& target, const QString& projectDir, QWidget* parent = nullptr);

    Target result() const { return m_target; }

protected:
    void accept() override;

private:
    void buildUi(const QString& projectDir);
    void loadFromTarget();
    void saveToTarget();

    Target m_target;

    QLineEdit* m_outputName = nullptr;
    QLineEdit* m_outputDir = nullptr;
    QLineEdit* m_listingDir = nullptr;
    QCheckBox* m_createHex = nullptr;
    QCheckBox* m_debugInfo = nullptr;
    QCheckBox* m_browseInfo = nullptr;
    QCheckBox* m_createLib = nullptr;

    QComboBox* m_optimization = nullptr;
    QCheckBox* m_c99 = nullptr;
    QCheckBox* m_oneElf = nullptr;
    QCheckBox* m_strict = nullptr;
    QCheckBox* m_enumInt = nullptr;
    QCheckBox* m_plainChar = nullptr;
    QCheckBox* m_interwork = nullptr;
    QCheckBox* m_noWarnings = nullptr;
    QLineEdit* m_defines = nullptr;
    QLineEdit* m_includes = nullptr;
    QLineEdit* m_cMisc = nullptr;

    QCheckBox* m_useMemoryLayout = nullptr;
    QLineEdit* m_textAddr = nullptr;
    QLineEdit* m_dataAddr = nullptr;
    QLineEdit* m_scatter = nullptr;
    QCheckBox* m_microlib = nullptr;
    QCheckBox* m_noStdLib = nullptr;
    QLineEdit* m_linkerMisc = nullptr;
};

// Tools -> Configure Toolchain（手工指定 armcc.exe 所在目录）
class ToolchainDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ToolchainDialog(const Toolchain& current, QWidget* parent = nullptr);

    Toolchain result() const { return m_toolchain; }

    static bool configure(QWidget* parent, Toolchain& tc);

private:
    void browse();
    void refreshStatus();
    void accept() override;

    Toolchain  m_toolchain;
    QLineEdit* m_dir = nullptr;
    QComboBox* m_family = nullptr;
    QLabel*    m_status = nullptr;
};
