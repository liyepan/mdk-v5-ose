#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>

// C / C++ / 汇编 语法高亮，配色沿用 Keil 编辑器的经典方案。
class CSyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit CSyntaxHighlighter(QTextDocument* document, bool assembly = false);

protected:
    void highlightBlock(const QString& text) override;

private:
    struct Rule
    {
        QRegularExpression pattern;
        QTextCharFormat    format;
    };

    void buildRules(bool assembly);

    QVector<Rule> m_rules;
    QTextCharFormat m_commentFormat;
    QTextCharFormat m_multilineFormat;
    QTextCharFormat m_doxygenFormat;
};
