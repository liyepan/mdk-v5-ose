#pragma once

#include <QDialog>

class QLineEdit;
class QCheckBox;
class QLabel;

// 查找 / 替换。非模态，停在主窗口上不挡编辑。
class FindReplaceDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FindReplaceDialog(QWidget* parent = nullptr);

    QString findText() const;
    QString replaceText() const;
    bool    caseSensitive() const;
    bool    wholeWord() const;

    // 打开时把选中的文字填进"查找"，并选中方便覆盖
    void prepare(const QString& selection, bool replaceMode);
    void setStatus(const QString& text);

signals:
    void findNextRequested(bool forward);
    void replaceRequested();
    void replaceAllRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    QLineEdit* m_find = nullptr;
    QLineEdit* m_replace = nullptr;
    QCheckBox* m_case = nullptr;
    QCheckBox* m_whole = nullptr;
    QLabel*    m_status = nullptr;
};
