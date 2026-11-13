#pragma once

#include <QString>

// 代码格式化，只用 clang-format（和 VS Code C/C++ 插件同一个引擎）。
// 没配 / 找不到就明确报错，不再退回内置缩进器——两套引擎出来的风格不一致，
// 反而毁工程。路径和风格在 Tools -> Configure Toolchain 里指定。
class Formatter
{
public:
    struct Result
    {
        bool    ok = false;
        QString text;
        QString engine;     // 固定 "clang-format"
        QString error;
    };

    static Result format(const QString& input, const QString& filePath);

    // 自动探测：配置 -> PATH -> LLVM / VS Code cpptools 常见位置
    static QString detectClangFormat();
    static bool    hasClangFormat();

    // 当前会用的风格，给人看的（状态栏 / 对话框里显示）
    static QString activeStyleName(const QString& filePath);

private:
    static Result runClangFormat(const QString& clang, const QString& input,
                                 const QString& filePath);
};
