#pragma once

#include <QString>

enum class Severity {
    Error,
    Warning,
    Info
};

struct Diagnostic
{
    Severity severity = Severity::Error;
    QString  file;        // 绝对路径（解析成功后）
    int      line = 0;
    int      column = 0;
    QString  code;        // 例如 #20 / L6218E
    QString  message;
    QString  raw;

    bool hasLocation() const { return !file.isEmpty() && line > 0; }
    QString severityText() const;
};

namespace Diagnostics {

// 解析一行编译器 / 链接器输出。支持：
//   armcc : ".\main.c", line 45: Error:  #20: identifier "x" is undefined
//   gcc   : /path/main.c:45:12: error: 'x' undeclared
//   armlink: Error: L6218E: Undefined symbol foo (referred from main.o).
bool parseLine(const QString& line, const QString& projectDir, Diagnostic* out);

} // namespace Diagnostics
