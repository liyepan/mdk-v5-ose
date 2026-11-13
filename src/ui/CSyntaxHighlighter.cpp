#include "ui/CSyntaxHighlighter.h"

namespace {

// 找从 from 开始的 "/*"，跳过字符串字面量和 // 行注释。
// 不跳的话 "// 见 /* 说明" 这种行会让后半个文件全变绿。
int commentStartOutside(const QString& text, int from)
{
    for (int i = from; i + 1 < text.size(); ++i) {
        const QChar c = text.at(i);

        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            const QChar quote = c;
            ++i;
            while (i < text.size()) {
                if (text.at(i) == QLatin1Char('\\')) {
                    i += 2;
                    continue;
                }
                if (text.at(i) == quote)
                    break;
                ++i;
            }
            continue;
        }

        if (c == QLatin1Char('/') && text.at(i + 1) == QLatin1Char('/'))
            return -1;                       // 行注释后面不可能再有代码

        if (c == QLatin1Char('/') && text.at(i + 1) == QLatin1Char('*'))
            return i;
    }
    return -1;
}

} // namespace

CSyntaxHighlighter::CSyntaxHighlighter(QTextDocument* document, bool assembly)
    : QSyntaxHighlighter(document)
{
    buildRules(assembly);
}

void CSyntaxHighlighter::buildRules(bool assembly)
{
    // ---- 调色板：VS Code Light+ ----
    const QColor cControl(0xaf, 0x00, 0xdb);    // if / for / return
    const QColor cKeyword(0x00, 0x00, 0xff);    // static / const / struct
    const QColor cType(0x26, 0x7f, 0x99);       // int / uint8_t
    const QColor cFunction(0x79, 0x5e, 0x26);   // 函数名
    const QColor cMacro(0xaf, 0x00, 0xdb);      // 全大写宏
    const QColor cNumber(0x09, 0x86, 0x58);
    const QColor cString(0xa3, 0x15, 0x15);
    const QColor cComment(0x00, 0x80, 0x00);
    const QColor cPreproc(0x80, 0x80, 0x80);

    QTextCharFormat controlFmt;
    controlFmt.setForeground(cControl);

    QTextCharFormat keywordFmt;
    keywordFmt.setForeground(cKeyword);

    QTextCharFormat typeFmt;
    typeFmt.setForeground(cType);

    QTextCharFormat functionFmt;
    functionFmt.setForeground(cFunction);

    QTextCharFormat macroFmt;
    macroFmt.setForeground(cMacro);

    QTextCharFormat numberFmt;
    numberFmt.setForeground(cNumber);

    QTextCharFormat stringFmt;
    stringFmt.setForeground(cString);

    QTextCharFormat preprocFmt;
    preprocFmt.setForeground(cPreproc);

    QTextCharFormat commentFmt;
    commentFmt.setForeground(cComment);
    commentFmt.setFontItalic(true);

    m_multilineFormat = commentFmt;

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
            QStringLiteral("SETA"), QStringLiteral("MACRO"), QStringLiteral("IF"),
            QStringLiteral("ELSE"), QStringLiteral("ENDIF")
        };
        for (const QString& op : ops) {
            m_rules.append(Rule{ QRegularExpression(QStringLiteral("\\b%1\\b").arg(op),
                                                    QRegularExpression::CaseInsensitiveOption),
                                 keywordFmt, 0, false });
        }
        m_rules.append(Rule{ QRegularExpression(QStringLiteral("^\\s*;.*")), commentFmt, 0, false });
        m_rules.append(Rule{ QRegularExpression(QStringLiteral("\\b0[xX][0-9a-fA-F]+\\b")),
                             numberFmt, 0, false });
        return;
    }

    const QStringList controlKeywords = {
        QStringLiteral("if"), QStringLiteral("else"), QStringLiteral("for"),
        QStringLiteral("while"), QStringLiteral("do"), QStringLiteral("switch"),
        QStringLiteral("case"), QStringLiteral("default"), QStringLiteral("break"),
        QStringLiteral("continue"), QStringLiteral("return"), QStringLiteral("goto")
    };
    for (const QString& k : controlKeywords) {
        m_rules.append(Rule{ QRegularExpression(QStringLiteral("\\b%1\\b").arg(k)),
                             controlFmt, 0, false });
        m_notFunction.insert(k);
    }

    const QStringList keywords = {
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
    for (const QString& k : keywords) {
        m_rules.append(Rule{ QRegularExpression(QStringLiteral("\\b%1\\b").arg(k)),
                             keywordFmt, 0, false });
        m_notFunction.insert(k);
    }

    const QStringList types = {
        QStringLiteral("void"), QStringLiteral("char"), QStringLiteral("short"),
        QStringLiteral("int"), QStringLiteral("long"), QStringLiteral("float"),
        QStringLiteral("double"), QStringLiteral("signed"), QStringLiteral("unsigned"),
        QStringLiteral("bool"), QStringLiteral("size_t"), QStringLiteral("uint8_t"),
        QStringLiteral("uint16_t"), QStringLiteral("uint32_t"), QStringLiteral("uint64_t"),
        QStringLiteral("int8_t"), QStringLiteral("int16_t"), QStringLiteral("int32_t"),
        QStringLiteral("int64_t"), QStringLiteral("u8"), QStringLiteral("u16"),
        QStringLiteral("u32"), QStringLiteral("u64"), QStringLiteral("vu16"),
        QStringLiteral("vu32"), QStringLiteral("__IO"), QStringLiteral("__I"),
        QStringLiteral("__O"), QStringLiteral("err_t"), QStringLiteral("bool")
    };
    for (const QString& t : types) {
        m_rules.append(Rule{ QRegularExpression(QStringLiteral("\\b%1\\b").arg(t)),
                             typeFmt, 0, false });
        m_notFunction.insert(t);
    }

    // 函数名：identifier 后面紧跟 (
    m_rules.append(Rule{ QRegularExpression(
                            QStringLiteral("\\b([A-Za-z_][A-Za-z0-9_]*)[ \\t]*(?=\\()")),
                        functionFmt, 1, true });

    // 宏：全大写标识符（NULL / GPIO_PIN_0 之类）
    m_rules.append(Rule{ QRegularExpression(QStringLiteral("\\b([A-Z_][A-Z0-9_]{2,})\\b")),
                        macroFmt, 1, false });

    // 预处理指令
    m_rules.append(Rule{ QRegularExpression(QStringLiteral("^[ \\t]*#[ \\t]*\\w+")),
                        preprocFmt, 0, false });

    // 数字
    m_rules.append(Rule{ QRegularExpression(
                            QStringLiteral("\\b(0[xX][0-9a-fA-F]+[uUlL]*|\\d+[uUlLfF]*|\\.\\d+)\\b")),
                        numberFmt, 0, false });

    // 字符串、字符（放最后，压过前面的关键字/函数着色）
    m_rules.append(Rule{ QRegularExpression(QStringLiteral("\"(\\\\.|[^\"\\\\])*\"")),
                        stringFmt, 0, false });
    m_rules.append(Rule{ QRegularExpression(QStringLiteral("'(\\\\.|[^'\\\\])*'")),
                        stringFmt, 0, false });

    // 单行注释
    m_rules.append(Rule{ QRegularExpression(QStringLiteral("//[^\n]*")), commentFmt, 0, false });
}

void CSyntaxHighlighter::highlightBlock(const QString& text)
{
    for (const Rule& rule : m_rules) {
        QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();

            int start = m.capturedStart(rule.group);
            int length = m.capturedLength(rule.group);
            if (start < 0 || length <= 0)
                continue;

            if (rule.skipKeywords && m_notFunction.contains(m.captured(1)))
                continue;

            setFormat(start, length, rule.format);
        }
    }

    // 跨行注释 /* ... */
    setCurrentBlockState(0);

    int startIndex = 0;
    if (previousBlockState() == 1) {
        // 本行开头就在注释里：先找收尾，找不到说明整行都是注释
        const int endIndex = text.indexOf(QLatin1String("*/"));
        if (endIndex < 0) {
            setCurrentBlockState(1);
            setFormat(0, text.length(), m_multilineFormat);
            return;
        }
        setFormat(0, endIndex + 2, m_multilineFormat);
        startIndex = commentStartOutside(text, endIndex + 2);
    } else {
        startIndex = commentStartOutside(text, 0);
    }

    while (startIndex >= 0) {
        const int endIndex = text.indexOf(QLatin1String("*/"), startIndex + 2);
        if (endIndex < 0) {
            setCurrentBlockState(1);
            setFormat(startIndex, text.length() - startIndex, m_multilineFormat);
            break;
        }
        setFormat(startIndex, endIndex - startIndex + 2, m_multilineFormat);
        startIndex = commentStartOutside(text, endIndex + 2);
    }
}
