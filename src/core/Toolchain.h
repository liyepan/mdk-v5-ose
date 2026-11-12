#pragma once

#include <QString>
#include <QStringList>

// 手工配置的 ARM 编译器。目录形如：
//   C:/Keil_v5/ARM/ARMCC/bin      (AC5)
//   C:/Keil_v5/ARM/ARMCLANG/bin   (AC6)
struct Toolchain
{
    QString binDir;
    QString family;     // "AC5" / "AC6"
    QString cc;         // armcc.exe    / armclang.exe
    QString as;         // armasm.exe   / armasm.exe
    QString ld;         // armlink.exe
    QString from;       // fromelf.exe

    bool    isValid() const { return !cc.isEmpty() && !ld.isEmpty(); }
    QString displayName() const;
    QStringList missingTools() const;

    // 探测目录下可用的编译器；preferFamily 为 AC5/AC6 时优先匹配
    static Toolchain detect(const QString& binDir, const QString& preferFamily = QString());

    // 猜一个 Keil 安装目录（只做提示，不保证存在）
    static QString guessKeilRoot();
    static QString guessBinDir(const QString& family);

    // 运行 cc --vsn / --version，取版本号文本
    QString queryVersion() const;

    static QString exeSuffix();
};
