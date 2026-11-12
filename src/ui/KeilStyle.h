#pragma once

#include <QFont>
#include <QIcon>
#include <QPalette>
#include <QString>

class QApplication;

// 还原 uVision5 的观感：经典窗口调色板、16x16 工具条图标、等宽编辑字体。
// 图标全部用 QPainter 现场绘制，仓库里因此没有任何二进制资源。
namespace KeilStyle {

void    apply(QApplication& app);

QFont   uiFont();
QFont   editorFont();

QIcon   icon(const QString& name, int size = 16);

QPalette palette();

} // namespace KeilStyle
