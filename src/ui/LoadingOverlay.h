#pragma once

#include <QPixmap>
#include <QString>
#include <QWidget>

class QTimer;

// 打开工程时盖在主窗口上的一层：
//   整窗是主窗口的模糊快照 + 一层薄纱，中间一块圆角卡片，
//   上面写“加载中”，下面一条蓝色进度条一直在跑。
// 加载期间主窗口不接鼠标（遮罩在最上面），所以不会点出奇怪的状态。
class LoadingOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit LoadingOverlay(QWidget* parent);

    // 开始显示。owner 是被盖住的那个窗口（用来拍背景快照）。
    void start(QWidget* owner, const QString& title);
    // 主标题 + 副标题（比如“正在扫描源文件 320 / 1200”）
    void setStatus(const QString& title, const QString& detail);
    // total <= 0 表示不知道总进度，进度条循环跑；否则按百分比画
    void setProgress(int done, int total);
    void finish();

    bool isRunning() const { return m_running; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void captureBackdrop(QWidget* owner);
    void onTick();

    QPixmap m_small;      // 缩小后的快照，模糊全靠它
    QPixmap m_backdrop;   // 放大到控件尺寸的版本，每帧直接贴
    QString m_title;
    QString m_detail;
    qreal   m_fraction = 0.0;   // 确定进度时已完成的百分比
    bool    m_loop = true;      // 不确定进度：进度条循环跑
    qreal   m_phase = 0.0;
    bool    m_running = false;
    QTimer* m_timer = nullptr;
};
