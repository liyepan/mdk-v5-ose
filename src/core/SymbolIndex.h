#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include "core/UvprojModel.h"

struct SymbolLocation
{
    QString file;
    int     line = 0;
    bool    definition = false;
};

// 极简符号表：扫工程里的 .c/.h，记下函数定义/声明和宏名，
// 给 Ctrl+点击跳转和工具栏的符号下拉框用。不是 clangd，够用就行。
//
// 性能上做过两件事：
//   1. 手写扫描代替 QRegularExpression（打开工程慢主要就慢在这）；
//   2. 解析结果按 路径 + mtime + 大小 缓存在进程里，切 target 不用重扫。
class SymbolIndex
{
public:
    void clear();
    void build(const UvProject& project, int targetIndex);

    bool isBuilt() const { return m_built; }

    // 定义优先于声明
    QList<SymbolLocation> lookup(const QString& name) const;

    // 当前索引里的全部符号名（排序后）
    QStringList names() const;

    // 丢掉进程级的文件解析缓存（文件在外部被改过时用）
    static void clearCache();

private:
    QHash<QString, QList<SymbolLocation>> m_symbols;
    QSet<QString> m_indexed;              // 同一个文件可能被多个源文件 include
    mutable QStringList m_sortedNames;    // names() 的缓存
    bool m_built = false;
};
