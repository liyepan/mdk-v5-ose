#pragma once

#include <QString>

// 极简多语言：源码里的界面文字保持英文，切到中文时走一张 EN->ZH 词典。
// 没收录的字符串原样返回，所以漏翻不会变成空白。
namespace Lang {

enum class Id {
    English,
    Chinese
};

void    set(Id id);
Id      current();

// en 为源码里的英文原文
QString t(const char* en);

QString idToString(Id id);
Id      idFromString(const QString& s);

} // namespace Lang
