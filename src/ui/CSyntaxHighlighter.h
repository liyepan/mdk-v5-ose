#pragma once

#include <QRegularExpression>
#include <QSet>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>

// C / C++ / 汇编 语法高亮，配色对齐 VS Code 的 Light+ 主题。
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
        int                group = 0;          // 0 = 整个匹配，1 = 捕获组 1
        bool               skipKeywords = false;
    };

    void buildRules(bool assembly);

    QVector<Rule>   m_rules;
    QSet<QString>   m_notFunction;   // 关键字/类型，不算函数名
    QTextCharFormat m_multilineFormat;
};
