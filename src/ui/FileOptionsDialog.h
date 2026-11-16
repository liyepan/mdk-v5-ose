#pragma once

#include <QDialog>

#include "core/UvprojModel.h"

class QCheckBox;
class QLineEdit;
class QLabel;

// Options for File... / Options for Group...
// 文件和分组共用一套：显示/修改 参与编译 + 自己的宏、include、杂项。
// 留空就继承上一级（文件 < 分组 < Target）。
class FileOptionsDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Kind { File, Group };

    FileOptionsDialog(Kind kind, const QString& title, QWidget* parent = nullptr);

    void setIncludeInBuild(bool on);
    void setOptions(const QString& defines, const QString& includePaths, const QString& misc);

    bool    includeInBuild() const;
    QString defines() const;
    QString includePaths() const;
    QString misc() const;

private:
    Kind       m_kind;
    QCheckBox* m_include = nullptr;
    QLineEdit* m_defines = nullptr;
    QLineEdit* m_includePaths = nullptr;
    QLineEdit* m_misc = nullptr;
};
