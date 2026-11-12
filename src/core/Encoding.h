#pragma once

#include <QStringList>

// 支持的文件编码。System = 系统本地代码页（中文 Windows 上是 GBK）。
namespace Encoding {

inline QStringList choices()
{
    return { QStringLiteral("UTF-8"), QStringLiteral("GBK"), QStringLiteral("GB18030"),
             QStringLiteral("Big5"), QStringLiteral("System") };
}

} // namespace Encoding
