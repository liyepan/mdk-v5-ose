#include "ui/CSyntaxHighlighter.h"

CSyntaxHighlighter::CSyntaxHighlighter(QTextDocument* document, bool assembly)
    : QSyntaxHighlighter(document)
{
    buildRules(assembly);
}

void CSyntaxHighlighter::buildRules(bool assembly)
{
    QTextCharFormat keyword;
    keyword.setForeground(QColor(0x00, 0x00, 0xc0));
    keyword.setFontWeight(QFont::Bold);

    QTextCharFormat type;
    type.setForeground(QColor(0x00, 0x70, 0x90));

    QTextCharFormat preproc;
    preproc.setForeground(QColor(0x80, 0x00, 0x80));

    QTextCharFormat string;
    string.setForeground(QColor(0xa3, 0x15, 0x15));

    QTextCharFormat number;
    number.setForeground(QColor(0x09, 0x86, 0x58));

    m_commentFormat = QTextCharFormat();
    m_commentFormat.setForeground(QColor(0x00, 0x80, 0x00));
    m_commentFormat.setFontItalic(true);

    m_multilineFormat = QTextCharFormat();
    m_multilineFormat.setForeground(QColor(0x00, 0x80, 0x00));
    m_multilineFormat.setFontItalic(true);

    m_doxygenFormat = QTextCharFormat();
    m_doxygenFormat.setForeground(QColor(0x55, 0x80, 0x55));
    m_doxygenFormat.setFontItalic(true);

    if (assembly) {
        const QStringList ops = {
            QStringLiteral("MOV"), QStringLiteral("MOVS"), QStringLiteral("LDR"),
            QStringLiteral("STR"), QStringLiteral("LDRB"), QStringLiteral("STRB"),
            QStringLiteral("LDRH"), QStringLiteral("STRH"), QStringLiteral("ADD"),
            QStringLiteral("ADDS"), QStringLiteral("SUB"), QStringLiteral("SUBS"),
            QStringLiteral("MUL"), QStringLiteral("AND"), QStringLiteral("ORR"),
            QStringLiteral("EOR"), QStringLiteral("BIC"), QStringLiteral("CMP"),
            QStringLiteral("TST"), QStringLiteral("B"), QStringLiteral("BL"),
            QStringLiteral("BX"), QStringLiteral("BLX"), QStringLiteral("BEQ"),
            QStringLiteral("BNE"), QStringLiteral("PUSH"), QStringLiteral("POP"),
            QStringLiteral("STM"), QStringLiteral("LDM"), QStringLiteral("MRS"),
            QStringLiteral("MSR"), QStringLiteral("DCD"), QStringLiteral("DCB"),
            QStringLiteral("SPACE"), QStringLiteral("AREA"), QStringLiteral("EXPORT"),
            QStringLiteral("IMPORT"), QStringLiteral("THUMB"), QStringLiteral("PRESERVE8"),
            QStringLiteral("ALIGN"), QStringLiteral("END"), QStringLiteral("EQU"),
            QStringLiteral("RN"), QStringLiteral("SETA"), QStringLiteral("MACRO")
        };
        for (const QString& op : ops)
            m_rules.append(Rule{ QRegularExpression(
                                    QStringLiteral("\\b%1\\b").arg(op),
                                    QRegularExpression::CaseInsensitiveOption),
                                keyword });
        return;
    }

    const QStringList keywords = {
        QStringLiteral("if"), QStringLiteral("else"), QStringLiteral("for"),
        QStringLiteral("while"), QStringLiteral("do"), QStringLiteral("switch"),
        QStringLiteral("case"), QStringLiteral("default"), QStringLiteral("break"),
        QStringLiteral("continue"), QStringLiteral("return"), QStringLiteral("goto"),
        QStringLiteral("sizeof"), QStringLiteral("typedef"), QStringLiteral("struct"),
        QStringLiteral("union"), QStringLiteral("enum"), QStringLiteral("static"),
        QStringLiteral("extern"), QStringLiteral("const"), QStringLiteral("volatile"),
        QStringLiteral("register"), QStringLiteral("inline"), QStringLiteral("restrict"),
        QStringLiteral("class"), QStringLiteral("public"), QStringLiteral("private"),
        QStringLiteral("protected"), QStringLiteral("virtual"), QStringLiteral("namespace"),
        QStringLiteral("using"), QStringLiteral("template"), QStringLiteral("typename"),
        QStringLiteral("new"), QStringLiteral("delete"), QStringLiteral("this"),
        QStringLiteral("nullptr"), QStringLiteral("true"), QStringLiteral("false"),
        QStringLiteral("explicit"), QStringLiteral("friend"), QStringLiteral("operator"),
        QStringLiteral("throw"), QStringLiteral("try"), QStringLiteral("catch"),
        QStringLiteral("constexpr"), QStringLiteral("noexcept"), QStringLiteral("override")
    };
    for (const QString& k : keywords)
        m_rules.append(Rule{ QRegularExpression(QStringLiteral("\\b%1\\b").arg(k)), keyword });

    const QStringList types = {
        QStringLiteral("void"), QStringLiteral("char"), QStringLiteral("short"),
        QStringLiteral("int"), QStringLiteral("long"), QStringLiteral("float"),
        QStringLiteral("double"), QStringLiteral("signed"), QStringLiteral("unsigned"),
        QStringLiteral("bool"), QStringLiteral("size_t"), QStringLiteral("uint8_t"),
        QStringLiteral("uint16_t"), QStringLiteral("uint32_t"), QStringLiteral("uint64_t"),
        QStringLiteral("int8_t"), QStringLiteral("int16_t"), QStringLiteral("int32_t"),
        QStringLiteral("int64_t"), QStringLiteral("u8"), QStringLiteral("u16"),
        QStringLiteral("u32"), QStringLiteral("vu16"), QStringLiteral("vu32"),
        QStringLiteral("__IO"), QStringLiteral("__I"), QStringLiteral("__O")
    };
    for (const QString& t : types)
        m_rules.append(Rule{ QRegularExpression(QStringLiteral("\\b%1\\b").arg(t)), type });

    // 预处理指令
    m_rules.append(Rule{ QRegularExpression(QStringLiteral("^\\s*#\\s*\\w+")), preproc });

    // 数字
    m_rules.append(Rule{ QRegularExpression(
                            QStringLiteral("\\b(0[xX][0-9a-fA-F]+[uUlL]*|\\d+[uUlLfF]*|\\.\\d+)\\b")),
                        number });

    // 字符串 / 字符
    m_rules.append(Rule{ QRegularExpression(QStringLiteral("\"(\\\\.|[^\"\\\\])*\"")), string });
    m_rules.append(Rule{ QRegularExpression(QStringLiteral("'(\\\\.|[^'\\\\])*'")), string });

    // 单行注释
    m_rules.append(Rule{ QRegularExpression(QStringLiteral("//[^\n]*")), m_commentFormat });
}

void CSyntaxHighlighter::highlightBlock(const QString& text)
{
    for (const Rule& rule : m_rules) {
        QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            setFormat(m.capturedStart(), m.capturedLength(), rule.format);
        }
    }

    // 跨行注释 /* ... */
    setCurrentBlockState(0);

    int startIndex = 0;
    if (previousBlockState() != 1)
        startIndex = text.indexOf(QLatin1String("/*"));

    while (startIndex >= 0) {
        const int endIndex = text.indexOf(QLatin1String("*/"), startIndex + 2);
        int commentLength = 0;
        if (endIndex == -1) {
            setCurrentBlockState(1);
            commentLength = text.length() - startIndex;
        } else {
            commentLength = endIndex - startIndex + 2;
        }
        setFormat(startIndex, commentLength, m_multilineFormat);
        startIndex = text.indexOf(QLatin1String("/*"), startIndex + commentLength);
    }
}
