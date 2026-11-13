#include "ui/LoadingOverlay.h"

#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QTimer>
#include <QtMath>

namespace {

const QColor kVeil(246, 247, 249, 178);
const QColor kCardFill(255, 255, 255, 252);
const QColor kCardBorder(0xc9, 0xce, 0xd6);
const QColor kTitle(0x1f, 0x23, 0x28);
const QColor kDetail(0x6b, 0x72, 0x80);
const QColor kTrack(0xe3, 0xe7, 0xec);
const QColor kBlueFrom(0x2f, 0x7f, 0xe8);
const QColor kBlueTo(0x63, 0xa6, 0xf7);

const qreal kCardRadius = 12.0;

} // namespace

LoadingOverlay::LoadingOverlay(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("loadingOverlay"));
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::BusyCursor);

    m_timer = new QTimer(this);
    m_timer->setInterval(16);
    connect(m_timer, &QTimer::timeout, this, &LoadingOverlay::onTick);

    hide();
}

void LoadingOverlay::start(QWidget* owner, const QString& title)
{
    m_title = title;
    m_detail.clear();
    m_fraction = 0.0;
    m_loop = true;
    m_phase = 0.28;   // 别从“进度条完全空着”开始
    m_running = true;

    if (owner) {
        setGeometry(owner->rect());
        captureBackdrop(owner);
    }

    raise();
    show();
    m_timer->start();
    update();
}

void LoadingOverlay::setStatus(const QString& title, const QString& detail)
{
    if (m_title == title && m_detail == detail)
        return;

    m_title = title;
    m_detail = detail;
    if (m_running)
        update();
}

void LoadingOverlay::setProgress(int done, int total)
{
    if (total <= 0) {
        m_loop = true;
    } else {
        // 队列随着 #include 扩散会变长，比例只许涨不许掉，看着才不别扭
        const qreal f = qBound(0.0, qreal(done) / qreal(total), 1.0);
        if (f > m_fraction)
            m_fraction = f;
        m_loop = false;
    }

    if (m_running)
        update();
}

void LoadingOverlay::finish()
{
    m_running = false;
    m_timer->stop();
    hide();
    m_small = QPixmap();
    m_backdrop = QPixmap();
}

void LoadingOverlay::onTick()
{
    m_phase += 0.008;
    if (m_phase >= 1.0)
        m_phase -= 1.0;
    update();
}

void LoadingOverlay::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (!m_small.isNull())
        m_backdrop = m_small.scaled(size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

// 背景模糊：先整窗截图，缩到 1/10，再放大回来。
// 比 QGraphicsBlurEffect 便宜得多，效果对这种大块虚化够用了。
void LoadingOverlay::captureBackdrop(QWidget* owner)
{
    const QPixmap snap = owner->grab();
    if (snap.isNull() || snap.width() < 8 || snap.height() < 8) {
        m_small = QPixmap();
        m_backdrop = QPixmap();
        return;
    }

    const int w = qMax(1, snap.width() / 10);
    const int h = qMax(1, snap.height() / 10);
    m_small = snap.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    m_backdrop = m_small.scaled(size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

void LoadingOverlay::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    // ---- 背景：模糊快照 + 薄纱 ----
    if (!m_backdrop.isNull())
        p.drawPixmap(rect(), m_backdrop);
    else
        p.fillRect(rect(), QColor(0xf2, 0xf3, 0xf5));
    p.fillRect(rect(), kVeil);

    // ---- 圆角卡片 ----
    const int cardW = qBound(320, qRound(width() * 0.42), 520);
    const int cardH = 150;
    const QRect card((width() - cardW) / 2, (height() - cardH) / 2, cardW, cardH);

    p.setPen(Qt::NoPen);
    for (int i = 10; i >= 1; --i) {
        p.setBrush(QColor(15, 23, 42, 4));
        p.drawRoundedRect(card.adjusted(-i, -i + 3, i, i + 3), kCardRadius + i, kCardRadius + i);
    }

    p.setBrush(kCardFill);
    p.setPen(QPen(kCardBorder, 1));
    p.drawRoundedRect(QRectF(card).adjusted(0.5, 0.5, -0.5, -0.5), kCardRadius, kCardRadius);

    // ---- 文字 ----
    const int pad = 26;

    QFont titleFont = font();
    titleFont.setPointSizeF(font().pointSizeF() + 1.5);
    titleFont.setBold(true);
    p.setFont(titleFont);
    p.setPen(kTitle);
    QRect titleRect(card.left() + pad, card.top() + 26, card.width() - pad * 2, 24);
    p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, m_title);

    QFont detailFont = font();
    detailFont.setPointSizeF(qMax(7.0, font().pointSizeF() - 1.0));
    p.setFont(detailFont);
    p.setPen(kDetail);
    QRect detailRect(card.left() + pad, card.top() + 56, card.width() - pad * 2, 20);
    p.drawText(detailRect, Qt::AlignLeft | Qt::AlignVCenter, m_detail);

    // ---- 蓝色进度条 ----
    const QRect bar(card.left() + pad, card.top() + 100, card.width() - pad * 2, 8);
    p.setPen(Qt::NoPen);
    p.setBrush(kTrack);
    p.drawRoundedRect(bar, 4, 4);

    QLinearGradient grad(bar.topLeft(), bar.topRight());
    grad.setColorAt(0.0, kBlueFrom);
    grad.setColorAt(1.0, kBlueTo);

    p.save();
    QPainterPath clip;
    clip.addRoundedRect(bar, 4, 4);
    p.setClipPath(clip);
    p.setBrush(grad);

    if (!m_loop) {
        const int w = qRound(bar.width() * m_fraction);
        if (w > 0)
            p.drawRect(QRect(bar.left(), bar.top(), w, bar.height()));
        // 已经走到的地方再扫一层更亮的高光，表示还在动
        const int sheenW = qMax(40, bar.width() / 8);
        const int x = bar.left() - sheenW + qRound((bar.width() + sheenW) * m_phase);
        QLinearGradient sheen(bar.topLeft(), bar.topRight());
        sheen.setColorAt(0.0, QColor(255, 255, 255, 0));
        sheen.setColorAt(0.5, QColor(255, 255, 255, 90));
        sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
        p.setBrush(sheen);
        p.drawRect(QRect(x, bar.top(), sheenW, bar.height()));
    } else {
        // 不确定进度：一段渐变滑块从左跑到右，循环
        const int segW = qMax(72, bar.width() / 3);
        const int x = bar.left() - segW + qRound((bar.width() + segW) * m_phase);
        p.drawRect(QRect(x, bar.top(), segW, bar.height()));
    }
    p.restore();
}
