#pragma once

#include <QString>

// 代码格式化。优先用 clang-format（和 VS Code C/C++ 插件同一个引擎），
// 找不到就退回只调整行首空白的保守缩进器——那种绝不会改坏语义。
class Formatter
{
public:
    struct Result
    {
        bool    ok = false;
        QString text;
        QString engine;     // "clang-format" / "builtin"
        QString error;
    };

    static Result format(const QString& input, const QString& filePath);

    // 自动探测：配置 -> PATH -> LLVM / VS Code cpptools 常见位置
    static QString detectClangFormat();
    static bool    hasClangFormat();

private:
    static Result  runClangFormat(const QString& clang, const QString& input,
                                  const QString& filePath);
    static QString builtinIndent(const QString& input);
};
