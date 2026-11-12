#include "ui/KeilStyle.h"

#include <QApplication>
#include <QFontDatabase>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QProxyStyle>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOption>
#include <QtMath>

namespace {

// 接管页签 / 停靠窗口的关闭按钮图标（QSS 改不了它们，只能走 style）
class AppStyle : public QProxyStyle
{
public:
    AppStyle()
        : QProxyStyle(QStringLiteral("Fusion"))
    {
    }

    QIcon standardIcon(StandardPixmap which, const QStyleOption* option = nullptr,
                       const QWidget* widget = nullptr) const override
    {
        if (which == SP_TitleBarCloseButton || which == SP_DockWidgetCloseButton) {
            return KeilStyle::icon(QStringLiteral("close"), 16);
        }
        return QProxyStyle::standardIcon(which, option, widget);
    }
};

const QColor kInk(0x3c, 0x3c, 0x3c);
const QColor kPage(0xff, 0xff, 0xff);
const QColor kFolder(0xf2, 0xc4, 0x4c);
const QColor kFolderEdge(0xb0, 0x8c, 0x1c);
const QColor kGreen(0x2e, 0x8b, 0x34);
const QColor kRed(0xd0, 0x30, 0x20);
const QColor kBlue(0x2b, 0x5d, 0x9e);

void drawPage(QPainter& p, const QRectF& r, const QColor& pen = kInk)
{
    const qreal fold = 3.5;
    QPolygonF poly;
    poly << QPointF(r.left(), r.top())
         << QPointF(r.right() - fold, r.top())
         << QPointF(r.right(), r.top() + fold)
         << QPointF(r.right(), r.bottom())
         << QPointF(r.left(), r.bottom());
    p.setPen(QPen(pen, 1));
    p.setBrush(kPage);
    p.drawPolygon(poly);
    p.setPen(QPen(pen.lighter(150), 1));
    p.drawLine(QPointF(r.right() - fold, r.top()), QPointF(r.right() - fold, r.top() + fold));
    p.drawLine(QPointF(r.right() - fold, r.top() + fold), QPointF(r.right(), r.top() + fold));
}

void drawLetter(QPainter& p, const QRectF& r, const QString& ch, const QColor& color)
{
    QFont f = p.font();
    f.setPixelSize(8);
    f.setBold(true);
    p.setFont(f);
    p.setPen(color);
    p.drawText(QRectF(r.left(), r.top() + 4.0, r.width(), r.height() - 4.0),
               Qt::AlignHCenter | Qt::AlignTop, ch);
}

void drawFolderShape(QPainter& p, const QRectF& r)
{
    const qreal tabH = r.height() * 0.28;
    QPolygonF poly;
    poly << QPointF(r.left(), r.top() + tabH)
         << QPointF(r.left() + r.width() * 0.35, r.top() + tabH)
         << QPointF(r.left() + r.width() * 0.45, r.top())
         << QPointF(r.right(), r.top())
         << QPointF(r.right(), r.bottom())
         << QPointF(r.left(), r.bottom());
    p.setPen(QPen(kFolderEdge, 1));
    p.setBrush(kFolder);
    p.drawPolygon(poly);
}

void drawHammer(QPainter& p, const QRectF& r)
{
    p.save();
    p.translate(r.center());
    p.rotate(-40.0);
    p.setPen(QPen(QColor(0x8a, 0x5a, 0x24), 1));
    p.setBrush(QColor(0xb8, 0x84, 0x4a));
    p.drawRect(QRectF(-1.2, -0.5, 2.4, 7.0));
    p.setPen(QPen(QColor(0x50, 0x50, 0x50), 1));
    p.setBrush(QColor(0x9e, 0x9e, 0x9e));
    p.drawRect(QRectF(-3.4, -5.2, 6.8, 3.4));
    p.restore();
}

void drawTriangle(QPainter& p, const QRectF& r, const QColor& color)
{
    QPolygonF poly;
    poly << QPointF(r.left() + 2, r.top() + 1)
         << QPointF(r.right() - 1, r.center().y())
         << QPointF(r.left() + 2, r.bottom() - 1);
    p.setPen(QPen(color.darker(150), 1));
    p.setBrush(color);
    p.drawPolygon(poly);
}

void drawMagnifier(QPainter& p, const QRectF& r, const QColor& color)
{
    p.setPen(QPen(color, 1.6));
    p.setBrush(QColor(220, 234, 250));
    p.drawEllipse(QPointF(r.left() + 5.5, r.top() + 5.5), 4.2, 4.2);
    p.setBrush(Qt::NoBrush);
    p.drawLine(QPointF(r.left() + 8.8, r.top() + 8.8), QPointF(r.right() - 1.5, r.bottom() - 1.5));
}

void drawBug(QPainter& p, const QRectF& r)
{
    p.setPen(QPen(QColor(0x1f, 0x4d, 0x1f), 1));
    p.setBrush(QColor(0x6d, 0xb3, 0x4d));
    p.drawEllipse(QRectF(r.left() + 4.5, r.top() + 4.0, 7.0, 8.0));
    p.drawLine(QPointF(r.left() + 4.5, r.top() + 7.0), QPointF(r.left() + 1.5, r.top() + 5.5));
    p.drawLine(QPointF(r.left() + 4.5, r.top() + 9.5), QPointF(r.left() + 1.5, r.top() + 9.5));
    p.drawLine(QPointF(r.left() + 4.5, r.top() + 12.0), QPointF(r.left() + 1.5, r.top() + 13.5));
    p.drawLine(QPointF(r.right() - 4.5, r.top() + 7.0), QPointF(r.right() - 1.5, r.top() + 5.5));
    p.drawLine(QPointF(r.right() - 4.5, r.top() + 9.5), QPointF(r.right() - 1.5, r.top() + 9.5));
    p.drawLine(QPointF(r.right() - 4.5, r.top() + 12.0), QPointF(r.right() - 1.5, r.top() + 13.5));
}

// 所有图形都按 16x16 的逻辑坐标画，再整体缩放到目标尺寸，
// 这样同一份代码既能画 16px 工具条，也能画 32px 窗口图标、96px 的 6x 屏。
void paintIcon(QPainter& p, const QString& name, int size)
{
    p.save();
    p.scale(size / 16.0, size / 16.0);
    p.setRenderHint(QPainter::Antialiasing, false);

    const QRectF r(1.5, 1.5, 13.0, 13.0);

    if (name == QLatin1String("new")) {
        drawPage(p, r);
    } else if (name == QLatin1String("open")) {
        drawFolderShape(p, r);
    } else if (name == QLatin1String("save")) {
        p.setPen(QPen(QColor(0x33, 0x33, 0x33), 1));
        p.setBrush(kBlue);
        p.drawRect(r);
        p.setBrush(kPage);
        p.drawRect(QRectF(r.left() + 2, r.top() + 1, r.width() - 4, 4));
        p.drawRect(QRectF(r.left() + 2, r.center().y() + 1, r.width() - 4, r.height() * 0.4));
    } else if (name == QLatin1String("saveall")) {
        p.setPen(QPen(QColor(0x33, 0x33, 0x33), 1));
        p.setBrush(QColor(0xd8, 0xd8, 0xd8));
        p.drawRect(r.adjusted(2, 2, 0, 0));
        p.setBrush(kBlue);
        p.drawRect(r.adjusted(0, 0, -2, -2));
        p.setBrush(kPage);
        p.drawRect(QRectF(r.left() + 1, r.top() + 1, r.width() - 5, 3));
    } else if (name == QLatin1String("build") || name == QLatin1String("translate")) {
        drawHammer(p, r);
    } else if (name == QLatin1String("rebuild")) {
        drawHammer(p, r);
        p.setPen(QPen(kRed, 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(r.right() - 7, r.top(), 6.5, 6.5), 40 * 16, 260 * 16);
        QPolygonF tip;
        tip << QPointF(r.right() - 1.5, r.top() + 1.0)
            << QPointF(r.right() - 4.0, r.top() + 3.0)
            << QPointF(r.right() - 0.5, r.top() + 4.5);
        p.setBrush(kRed);
        p.setPen(QPen(kRed, 0));
        p.drawPolygon(tip);
    } else if (name == QLatin1String("stop")) {
        p.setPen(QPen(kRed.darker(160), 1));
        p.setBrush(kRed);
        p.drawRect(r.adjusted(1, 1, -1, -1));
    } else if (name == QLatin1String("debug_start")) {
        drawBug(p, r);
        drawMagnifier(p, r, QColor(0x20, 0x50, 0x90));
    } else if (name == QLatin1String("run")) {
        drawTriangle(p, r, kGreen);
    } else if (name == QLatin1String("debug_stop")) {
        p.setPen(QPen(QColor(0x33, 0x33, 0x33), 1));
        p.setBrush(QColor(0x9e, 0x9e, 0x9e));
        p.drawRect(r.adjusted(1, 1, -1, -1));
        p.setPen(QPen(kRed, 1.4));
        p.drawLine(r.topLeft() + QPointF(2, 2), r.bottomRight() - QPointF(2, 2));
        p.drawLine(r.topRight() + QPointF(-2, 2), r.bottomLeft() + QPointF(2, -2));
    } else if (name == QLatin1String("step")) {
        drawTriangle(p, r.adjusted(0, 0, -4, 0), kGreen);
        p.setPen(QPen(kInk, 2));
        p.drawLine(QPointF(r.right() - 2, r.top() + 1), QPointF(r.right() - 2, r.bottom() - 1));
    } else if (name == QLatin1String("stepover")) {
        drawTriangle(p, r.adjusted(0, 0, -3, 3), kGreen);
        p.setPen(QPen(kBlue, 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(r.left() + 5, r.bottom() - 6, 8, 6), 0, 180 * 16);
        p.drawLine(QPointF(r.right() - 1, r.bottom() - 3), QPointF(r.right() - 1, r.bottom() + 0.5));
    } else if (name == QLatin1String("stepout")) {
        drawTriangle(p, r.adjusted(0, 2, -3, -1), kGreen);
        p.setPen(QPen(kBlue, 1.4));
        p.drawLine(QPointF(r.right() - 3, r.bottom() - 1), QPointF(r.right() - 3, r.top() + 1));
        QPolygonF tip;
        tip << QPointF(r.right() - 6, r.top() + 3) << QPointF(r.right() - 3, r.top() + 0.5)
            << QPointF(r.right(), r.top() + 3);
        p.setBrush(kBlue);
        p.drawPolygon(tip);
    } else if (name == QLatin1String("runtocursor")) {
        drawTriangle(p, r.adjusted(0, 0, -2, 0), kGreen);
        p.setPen(QPen(kRed, 1.4, Qt::DashLine));
        p.drawLine(QPointF(r.left() + 1, r.bottom() - 1), QPointF(r.right(), r.bottom() - 1));
    } else if (name == QLatin1String("breakpoint")) {
        p.setPen(QPen(kRed.darker(170), 1));
        p.setBrush(kRed);
        p.drawEllipse(r.adjusted(1.5, 1.5, -1.5, -1.5));
    } else if (name == QLatin1String("folder")) {
        drawFolderShape(p, r);
    } else if (name == QLatin1String("target")) {
        p.setPen(QPen(QColor(0x55, 0x55, 0x55), 1));
        p.setBrush(QColor(0xc8, 0xa2, 0x50));
        p.drawRoundedRect(r.adjusted(0.5, 2.5, -0.5, -1.0), 1.5, 1.5);
        p.setBrush(QColor(0x3a, 0x3a, 0x3a));
        for (int i = 0; i < 3; ++i)
            p.drawRect(QRectF(r.left() + 2.0 + i * 4.0, r.top() + 4.0, 2.0, 2.0));
        p.drawRect(QRectF(r.left() + 2.0, r.bottom() - 5.5, r.width() - 4.0, 1.6));
    } else if (name == QLatin1String("file_c")) {
        drawPage(p, r);
        drawLetter(p, r, QStringLiteral("C"), QColor(0x1b, 0x4f, 0x9a));
    } else if (name == QLatin1String("file_cpp")) {
        drawPage(p, r);
        drawLetter(p, r, QStringLiteral("C+"), QColor(0x1b, 0x4f, 0x9a));
    } else if (name == QLatin1String("file_h")) {
        drawPage(p, r);
        drawLetter(p, r, QStringLiteral("H"), QColor(0x6a, 0x2d, 0x8a));
    } else if (name == QLatin1String("file_asm")) {
        drawPage(p, r);
        drawLetter(p, r, QStringLiteral("A"), QColor(0x8a, 0x4a, 0x1a));
    } else if (name == QLatin1String("batchbuild")) {
        // 多张纸叠着 + 一个小锤子，对应 uVision 的 Batch Build
        p.setPen(QPen(QColor(0x55, 0x55, 0x55), 1));
        p.setBrush(QColor(0xdd, 0xdd, 0xdd));
        p.drawRect(QRectF(r.left() + 2.5, r.top() + 1.0, 7.5, 9.0));
        p.setBrush(kPage);
        p.drawRect(QRectF(r.left() + 1.0, r.top() + 3.0, 7.5, 9.5));
        p.setPen(QPen(QColor(0x99, 0x99, 0x99), 1));
        for (int i = 0; i < 3; ++i)
            p.drawLine(QPointF(r.left() + 2.5, r.top() + 5.5 + i * 2.2),
                       QPointF(r.left() + 7.0, r.top() + 5.5 + i * 2.2));
        drawHammer(p, QRectF(r.left() + 7.5, r.top() + 6.5, 7.0, 7.0));
    } else if (name == QLatin1String("reset")) {
        // 复位：环形箭头
        p.setPen(QPen(kBlue, 1.6));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(r.left() + 2.0, r.top() + 2.0, 10.0, 10.0), 40 * 16, 280 * 16);
        QPolygonF tip;
        tip << QPointF(r.right() - 1.0, r.top() + 3.0)
            << QPointF(r.right() - 4.5, r.top() + 4.5)
            << QPointF(r.right() - 1.5, r.top() + 6.5);
        p.setBrush(kBlue);
        p.setPen(QPen(kBlue, 0));
        p.drawPolygon(tip);
    } else if (name == QLatin1String("breakpoint_off")) {
        p.setPen(QPen(kRed, 1.3));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(r.adjusted(1.8, 1.8, -1.8, -1.8));
    } else if (name == QLatin1String("cut")) {
        p.setPen(QPen(kInk, 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(r.left() + 3.2, r.bottom() - 2.6), 1.8, 1.8);
        p.drawEllipse(QPointF(r.left() + 3.2, r.top() + 2.6), 1.8, 1.8);
        p.drawLine(QPointF(r.left() + 4.6, r.bottom() - 3.4), QPointF(r.right() - 1.5, r.top() + 1.6));
        p.drawLine(QPointF(r.left() + 4.6, r.top() + 3.4), QPointF(r.right() - 1.5, r.bottom() - 1.6));
    } else if (name == QLatin1String("copy")) {
        p.setPen(QPen(QColor(0x55, 0x55, 0x55), 1));
        p.setBrush(QColor(0xe8, 0xe8, 0xe8));
        p.drawRect(QRectF(r.left() + 4.0, r.top() + 1.0, 7.5, 9.0));
        p.setBrush(kPage);
        p.drawRect(QRectF(r.left() + 1.5, r.top() + 4.0, 7.5, 9.0));
    } else if (name == QLatin1String("paste")) {
        p.setPen(QPen(QColor(0x8a, 0x6a, 0x3a), 1));
        p.setBrush(QColor(0xd8, 0xbc, 0x8a));
        p.drawRect(QRectF(r.left() + 2.0, r.top() + 3.0, 9.5, 10.0));
        p.setBrush(kPage);
        p.drawRect(QRectF(r.left() + 3.5, r.top() + 1.0, 6.5, 3.0));
    } else if (name == QLatin1String("undo") || name == QLatin1String("redo")) {
        const bool redo = (name == QLatin1String("redo"));
        p.setPen(QPen(kBlue, 1.6));
        p.setBrush(Qt::NoBrush);
        const QRectF arc = redo ? QRectF(r.left() + 1.5, r.top() + 3.5, 10.0, 8.0)
                                : QRectF(r.right() - 11.5, r.top() + 3.5, 10.0, 8.0);
        p.drawArc(arc, redo ? 160 * 16 : 20 * 16, 160 * 16);
        QPolygonF tip;
        if (redo) {
            tip << QPointF(r.left() + 2.0, r.top() + 5.0)
                << QPointF(r.left() + 6.0, r.top() + 6.0)
                << QPointF(r.left() + 2.5, r.top() + 8.5);
        } else {
            tip << QPointF(r.right() - 2.0, r.top() + 5.0)
                << QPointF(r.right() - 6.0, r.top() + 6.0)
                << QPointF(r.right() - 2.5, r.top() + 8.5);
        }
        p.setBrush(kBlue);
        p.setPen(QPen(kBlue, 0));
        p.drawPolygon(tip);
    } else if (name == QLatin1String("back") || name == QLatin1String("forward")) {
        const bool back = (name == QLatin1String("back"));
        p.setPen(QPen(QColor(0x4a, 0x6a, 0x8f), 1.6, Qt::SolidLine, Qt::RoundCap));
        if (back) {
            p.drawLine(QPointF(r.right() - 2.0, r.center().y()), QPointF(r.left() + 2.0, r.center().y()));
            p.drawLine(QPointF(r.left() + 2.0, r.center().y()), QPointF(r.left() + 5.5, r.center().y() - 3.2));
            p.drawLine(QPointF(r.left() + 2.0, r.center().y()), QPointF(r.left() + 5.5, r.center().y() + 3.2));
        } else {
            p.drawLine(QPointF(r.left() + 2.0, r.center().y()), QPointF(r.right() - 2.0, r.center().y()));
            p.drawLine(QPointF(r.right() - 2.0, r.center().y()), QPointF(r.right() - 5.5, r.center().y() - 3.2));
            p.drawLine(QPointF(r.right() - 2.0, r.center().y()), QPointF(r.right() - 5.5, r.center().y() + 3.2));
        }
    } else if (name == QLatin1String("mouse")) {
        p.setPen(QPen(QColor(0x60, 0x68, 0x74), 1.2));
        p.setBrush(kPage);
        p.drawRoundedRect(QRectF(r.left() + 4.5, r.top() + 1.5, 7.0, 11.0), 3.5, 3.5);
        p.drawLine(QPointF(r.center().x(), r.top() + 1.5), QPointF(r.center().x(), r.top() + 5.0));
    } else if (name == QLatin1String("bookmark_prev") || name == QLatin1String("bookmark_next")) {
        const bool next = (name == QLatin1String("bookmark_next"));
        p.setPen(QPen(QColor(0x1f, 0x5f, 0xbf), 1));
        p.setBrush(QColor(0x6f, 0x9e, 0xe8));
        QPolygonF ribbon;
        ribbon << QPointF(r.left() + 2.0, r.top() + 1.5) << QPointF(r.left() + 8.0, r.top() + 1.5)
               << QPointF(r.left() + 8.0, r.bottom() - 1.5) << QPointF(r.left() + 5.0, r.bottom() - 4.5)
               << QPointF(r.left() + 2.0, r.bottom() - 1.5);
        p.drawPolygon(ribbon);
        p.setPen(QPen(kInk, 1.4));
        if (next) {
            p.drawLine(QPointF(r.left() + 9.5, r.center().y()), QPointF(r.right() - 1.0, r.center().y()));
            p.drawLine(QPointF(r.right() - 1.0, r.center().y()), QPointF(r.right() - 4.0, r.center().y() - 2.6));
            p.drawLine(QPointF(r.right() - 1.0, r.center().y()), QPointF(r.right() - 4.0, r.center().y() + 2.6));
        } else {
            p.drawLine(QPointF(r.left() + 1.0, r.center().y()), QPointF(r.left() + 1.5, r.center().y()));
        }
    } else if (name == QLatin1String("bookmark_delete")) {
        p.setPen(QPen(QColor(0x1f, 0x5f, 0xbf), 1));
        p.setBrush(QColor(0x6f, 0x9e, 0xe8));
        QPolygonF ribbon;
        ribbon << QPointF(r.left() + 1.5, r.top() + 1.5) << QPointF(r.left() + 7.0, r.top() + 1.5)
               << QPointF(r.left() + 7.0, r.bottom() - 2.5) << QPointF(r.left() + 4.2, r.bottom() - 5.0)
               << QPointF(r.left() + 1.5, r.bottom() - 2.5);
        p.drawPolygon(ribbon);
        p.setPen(QPen(kRed, 1.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(r.left() + 8.5, r.top() + 6.0), QPointF(r.right() - 1.5, r.bottom() - 1.5));
        p.drawLine(QPointF(r.right() - 1.5, r.top() + 6.0), QPointF(r.left() + 8.5, r.bottom() - 1.5));
    } else if (name == QLatin1String("indent") || name == QLatin1String("unindent")) {
        const bool in = (name == QLatin1String("indent"));
        p.setPen(QPen(QColor(0x8a, 0x93, 0x9e), 1));
        for (int i = 0; i < 3; ++i)
            p.drawLine(QPointF(r.left() + 5.0, r.top() + 3.0 + i * 4.0), QPointF(r.right() - 1.0, r.top() + 3.0 + i * 4.0));
        p.setPen(QPen(kGreen.darker(120), 1.5, Qt::SolidLine, Qt::RoundCap));
        if (in)
            p.drawLine(QPointF(r.left() + 1.0, r.center().y()), QPointF(r.left() + 3.5, r.center().y()));
        else
            p.drawLine(QPointF(r.left() + 1.0, r.center().y()), QPointF(r.left() + 3.5, r.center().y()));
    } else if (name == QLatin1String("comment") || name == QLatin1String("uncomment")) {
        p.setPen(QPen(QColor(0x8a, 0x93, 0x9e), 1));
        for (int i = 0; i < 3; ++i)
            p.drawLine(QPointF(r.left() + 5.0, r.top() + 3.5 + i * 4.0), QPointF(r.right() - 1.0, r.top() + 3.5 + i * 4.0));
        QFont f = p.font();
        f.setPixelSize(8);
        f.setBold(true);
        p.setFont(f);
        p.setPen(name == QLatin1String("comment") ? kGreen.darker(130) : kRed);
        p.drawText(QRectF(r.left() - 1.0, r.top() + 1.0, 6.0, r.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("//"));
    } else if (name == QLatin1String("find")) {
        drawMagnifier(p, r, QColor(0x1f, 0x5f, 0xbf));
    } else if (name == QLatin1String("find_all")) {
        drawFolderShape(p, QRectF(r.left(), r.top() + 3.0, r.width(), r.height() - 3.0));
        drawMagnifier(p, QRectF(r.left() + 4.0, r.top(), 10.0, 10.0), QColor(0x1f, 0x5f, 0xbf));
    } else if (name == QLatin1String("breakpoint_insert")) {
        p.setPen(QPen(kRed.darker(170), 1));
        p.setBrush(kRed);
        p.drawEllipse(QRectF(r.left() + 2.0, r.top() + 2.0, 8.0, 8.0));
        p.setPen(QPen(QColor(0x2e, 0x8b, 0x34), 1.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(r.right() - 3.0, r.center().y()), QPointF(r.right() - 0.5, r.center().y()));
        p.drawLine(QPointF(r.right() - 1.75, r.center().y() - 2.0), QPointF(r.right() - 1.75, r.center().y() + 2.0));
    } else if (name == QLatin1String("breakpoint_delete")) {
        p.setPen(QPen(kRed.darker(170), 1));
        p.setBrush(kRed);
        p.drawEllipse(QRectF(r.left() + 2.0, r.top() + 2.0, 8.0, 8.0));
        p.setPen(QPen(QColor(0x2e, 0x8b, 0x34), 1.8, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(r.right() - 3.0, r.center().y()), QPointF(r.right() - 0.5, r.center().y()));
    } else if (name == QLatin1String("breakpoint_clear")) {
        p.setPen(QPen(kRed.darker(170), 1));
        p.setBrush(QColor(0xee, 0xc8, 0xc4));
        p.drawEllipse(QRectF(r.left() + 1.0, r.top() + 2.0, 7.0, 7.0));
        p.drawEllipse(QRectF(r.left() + 5.0, r.top() + 7.0, 7.0, 7.0));
    } else if (name == QLatin1String("breakpoint_clear_all")) {
        p.setPen(QPen(kRed.darker(170), 1));
        p.setBrush(QColor(0xee, 0xc8, 0xc4));
        p.drawEllipse(QRectF(r.left() + 1.0, r.top() + 1.5, 6.0, 6.0));
        p.drawEllipse(QRectF(r.left() + 5.5, r.top() + 5.5, 6.0, 6.0));
        p.setPen(QPen(kRed.darker(150), 1.4));
        p.drawLine(QPointF(r.left() + 6.0, r.top() + 1.5), QPointF(r.right() - 3.0, r.bottom() - 2.0));
    } else if (name == QLatin1String("close")) {
        // 细一点的圆头叉号，比 Fusion 自带的方块 X 好看
        QPen pen(QColor(0x7b, 0x84, 0x91), 1.5, Qt::SolidLine, Qt::RoundCap);
        p.setPen(pen);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.drawLine(QPointF(5.0, 5.0), QPointF(11.0, 11.0));
        p.drawLine(QPointF(11.0, 5.0), QPointF(5.0, 11.0));
    } else if (name == QLatin1String("book")) {
        p.setPen(QPen(QColor(0x55, 0x3a, 0x1a), 1));
        p.setBrush(QColor(0xc9, 0x7a, 0x3a));
        p.drawRect(r.adjusted(1, 0, -1, 0));
        p.setBrush(Qt::white);
        p.drawRect(r.adjusted(3, 2, -3, -2));
    } else {
        drawPage(p, r);
    }

    p.restore();
}

// 为 1x / 2x / 3x 各生成一张位图塞进同一个 QIcon：
// Qt 会按当前屏幕的 devicePixelRatio 自动挑最合适的那张，
// 所以 150% / 200% 缩放下图标依然是清晰的，而不是被拉伸糊掉。
QIcon makeIcon(const QString& name, int size)
{
    QIcon icon;
    for (int factor = 1; factor <= 3; ++factor) {
        QPixmap pm(size * factor, size * factor);
        pm.setDevicePixelRatio(factor);
        pm.fill(Qt::transparent);

        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, factor > 1);
        paintIcon(p, name, size);
        p.end();

        icon.addPixmap(pm);
    }
    return icon;
}

} // namespace

namespace KeilStyle {

QString styleSheet()
{
    return QString::fromUtf8(R"QSS(
/* ---------- 基础：灰底衬白面板，面板之间才有层次 ---------- */
QWidget { color: #101418; }
QMainWindow, QDialog { background: #e9edf2; }

/* 停靠窗口之间的分隔条：这就是"边缘" */
QMainWindow::separator { background: #c9d2dc; width: 5px; height: 5px; }
QMainWindow::separator:hover { background: #a9b6c4; }

/* ---------- 菜单栏 ---------- */
QMenuBar { background: #ffffff; border-bottom: 1px solid #d8dee6; padding: 2px 4px; }
QMenuBar::item { background: transparent; padding: 5px 10px; border-radius: 5px; }
QMenuBar::item:selected { background: #eef2f7; }
QMenuBar::item:pressed { background: #e2e8f0; }

QMenu { background: #ffffff; border: 1px solid #cfd6df; padding: 5px; }
QMenu::item { padding: 6px 28px 6px 22px; border-radius: 5px; }
QMenu::item:selected { background: #eaf1fd; color: #0b57d0; }
QMenu::item:disabled { color: #98a0ab; }
QMenu::separator { height: 1px; background: #e8ecf1; margin: 5px 10px; }

/* ---------- 工具栏 ---------- */
QToolBar { background: #ffffff; border: none; border-bottom: 1px solid #d8dee6;
           spacing: 2px; padding: 1px 4px; }
QToolBar::separator { width: 1px; background: #dde3ea; margin: 4px 6px; }
QToolButton { background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 2px; }
QToolButton:hover { background: #eef2f7; border-color: #dbe2ea; }
QToolButton:pressed { background: #dfe6ef; }
QToolButton:checked { background: #e4edfb; border-color: #c9dcf7; }

/* ---------- 停靠窗口：白底 + 标题条 + 外框 ---------- */
QDockWidget { background: #ffffff; border: 1px solid #c9d2dc; }
QDockWidget::title { background: #eef2f7; color: #333b45; padding: 6px 10px;
                     border-bottom: 1px solid #c9d2dc; text-align: left; }

/* ---------- 页签 ---------- */
QTabWidget { background: #e9edf2; }
QTabWidget::pane { background: #ffffff; border: 1px solid #c9d2dc; }
QTabBar { background: transparent; }
QTabBar::tab { background: #eef1f5; color: #4b5563; padding: 2px 12px 2px 10px; margin-right: 2px;
               border: 1px solid #d8dee6; border-bottom: none;
               border-top-left-radius: 6px; border-top-right-radius: 6px; min-height: 18px; }
QTabBar::tab:selected { background: #ffffff; color: #1f2328;
                        border-color: #c9d2dc; border-bottom: none; }
QTabBar::tab:hover:!selected { background: #e4e9f0; }
QTabBar QToolButton { padding: 0px; border: none; border-radius: 4px; margin-left: 6px; }
QTabBar QToolButton:hover { background: #fbdad8; }
QTabBar::close-button { border-radius: 4px; }

/* ---------- 树 / 表格：独立白块，带边框 ---------- */
QTreeWidget, QTreeView, QListView, QTableView { background: #ffffff;
    border: 1px solid #c9d2dc; outline: 0; }
QTreeView::item { padding: 3px 2px; }
QTreeView::item:hover { background: #f2f6fb; }
QTreeView::item:selected { background: #e2ecfb; color: #0b2545; }
QHeaderView::section { background: #f2f5f9; color: #4b5563; padding: 5px 6px;
                       border: none; border-bottom: 1px solid #d8dee6;
                       border-right: 1px solid #e8ecf1; }

/* ---------- 输入控件 ---------- */
QLineEdit, QComboBox, QSpinBox { background: #ffffff; border: 1px solid #cfd6df;
                                 border-radius: 6px; padding: 4px 8px; }
QLineEdit:focus, QComboBox:focus { border-color: #0b57d0; }
QComboBox::drop-down { border: none; width: 18px; }
QPushButton { background: #ffffff; border: 1px solid #cfd6df; border-radius: 6px; padding: 5px 14px; }
QPushButton:hover { background: #f2f5f9; }
QPushButton:pressed { background: #e4e9f0; }
QPushButton:default { background: #0b57d0; border-color: #0b57d0; color: #ffffff; }
QPushButton:default:hover { background: #0a4cba; }

/* ---------- 文本区 ---------- */
QPlainTextEdit, QTextEdit { background: #ffffff; border: none;
                            selection-background-color: #cfe3ff; selection-color: #10233f; }

/* ---------- 滚动条 ---------- */
QScrollBar:vertical { background: transparent; width: 11px; margin: 0; }
QScrollBar::handle:vertical { background: #c2cad4; border-radius: 5px; min-height: 32px; }
QScrollBar::handle:vertical:hover { background: #a6b0bc; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QScrollBar:horizontal { background: transparent; height: 11px; margin: 0; }
QScrollBar::handle:horizontal { background: #c2cad4; border-radius: 5px; min-width: 32px; }
QScrollBar::handle:horizontal:hover { background: #a6b0bc; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }

/* ---------- 状态栏 ---------- */
QStatusBar { background: #ffffff; border-top: 1px solid #d8dee6; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: #4b5563; padding: 0 10px; }

/* ---------- 其它 ---------- */
QCheckBox::indicator { width: 15px; height: 15px; }
QGroupBox { border: 1px solid #d8dee6; border-radius: 8px; margin-top: 10px; padding-top: 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: #4b5563; }
QToolTip { background: #1f2937; color: #f9fafb; border: none; padding: 4px 6px; }
)QSS");
}

void apply(QApplication& app)
{
    // 统一用 Fusion 打底：QSS 在 Windows 的 vista 样式上会花得厉害。
    // 外面再包一层 AppStyle，用来换掉标准图标（关闭按钮等）。
    app.setStyle(new AppStyle);

#if defined(Q_OS_WIN)
    QFont f(QStringLiteral("Segoe UI"));
    f.setPointSizeF(9.5);
    if (!QFontDatabase().families().contains(QStringLiteral("Segoe UI")))
        f = app.font();
#else
    QFont f = app.font();
    if (f.pointSizeF() < 9.0)
        f.setPointSizeF(9.5);
#endif
    app.setFont(f);

    app.setStyleSheet(styleSheet());
}

QFont uiFont()
{
    QFont f = QApplication::font();
    return f;
}

QFont editorFont()
{
#if defined(Q_OS_WIN)
    // Consolas 比 Courier New 清晰得多，是 Win7+ 自带的
    QFont f(QStringLiteral("Consolas"));
    f.setPointSizeF(10.5);
    if (!QFontDatabase().families().contains(QStringLiteral("Consolas"))) {
        f = QFont(QStringLiteral("Courier New"));
        f.setPointSizeF(10.0);
    }
#else
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setPointSizeF(10.0);
#endif
    f.setStyleHint(QFont::Monospace);
    f.setFixedPitch(true);
    return f;
}

QIcon icon(const QString& name, int size)
{
    static QHash<QString, QIcon> cache;
    const QString key = name + QLatin1Char('@') + QString::number(size);
    auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return *it;
    const QIcon ic = makeIcon(name, size);
    cache.insert(key, ic);
    return ic;
}

QPalette palette()
{
    return QApplication::palette();
}

} // namespace KeilStyle
