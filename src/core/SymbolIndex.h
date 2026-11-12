#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include "core/UvprojModel.h"

struct SymbolLocation
{
    QString file;
    int     line = 0;
    bool    definition = false;
};

// 极简符号表：扫工程里的 .c/.h，记下函数定义/声明和宏名，
// 给 Ctrl+点击跳转用。不是 clangd，够用就行。
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

private:
    void indexFile(const QString& path);
    void indexIncludes(const QString& path, const QStringList& includeDirs);

    QHash<QString, QList<SymbolLocation>> m_symbols;
    QSet<QString> m_indexed;      // 同一个文件可能被多个源文件 include
    bool m_built = false;
};
