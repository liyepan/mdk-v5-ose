#pragma once

#include <QHash>

#include "core/UvprojModel.h"

// .uvprojx 读写。读用 QDomDocument 建立完整模型；
// 写采用"原地改 DOM 再落盘"的方式，保证 Keil 自己定义的其余节点不丢。
namespace Uvproj {

bool load(const QString& path, UvProject& out, QString* error = nullptr);

bool saveTarget(const QString& path, const Target& target, QString* error = nullptr);

// 只重写 <Groups>（分组 + 文件列表），其它内容原样保留。
// 已有的分组/文件的组级、文件级选项会尽量带过去。
bool saveGroups(const QString& path, const Target& target, QString* error = nullptr);

// 让文件里的 <Targets> 和给定的一致：按 names 排序、删掉多余的、
// 新增的（cloneFrom 里有映射的）从源 Target 整段复制过来再改名。
// 已有 Target 的内部内容一个字节都不动。
bool syncTargets(const QString& path, const QStringList& names,
                 const QHash<QString, QString>& cloneFrom, QString* error = nullptr);

// 把 uvprojx 里的 .\Foo\bar.c 还原成真实文件系统路径
QString resolvePath(const QString& projectDir, const QString& relative);

} // namespace Uvproj
