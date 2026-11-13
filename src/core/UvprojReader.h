#pragma once

#include "core/UvprojModel.h"

// .uvprojx 读写。读用 QDomDocument 建立完整模型；
// 写采用"原地改 DOM 再落盘"的方式，保证 Keil 自己定义的其余节点不丢。
namespace Uvproj {

bool load(const QString& path, UvProject& out, QString* error = nullptr);

bool saveTarget(const QString& path, const Target& target, QString* error = nullptr);

// 把 uvprojx 里的 .\Foo\bar.c 还原成真实文件系统路径
QString resolvePath(const QString& projectDir, const QString& relative);

} // namespace Uvproj
