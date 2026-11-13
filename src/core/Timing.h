#pragma once

#include <QElapsedTimer>
#include <QString>

#include <cstdio>

// 打开工程慢的时候用这个定位：设环境变量 MDK_OSE_TIMING=1，
// 各个阶段耗时就会打到 stderr，正常跑没有任何输出、也没有开销。
namespace Timing {

inline bool enabled()
{
    static const bool on = !qEnvironmentVariableIsEmpty("MDK_OSE_TIMING");
    return on;
}

// 作用域结束时打印 "名称: N ms"
class Scope
{
public:
    explicit Scope(const char* name) : m_name(name)
    {
        if (enabled())
            m_timer.start();
    }

    ~Scope()
    {
        if (m_timer.isValid())
            std::fprintf(stderr, "[timing] %-24s %6lld ms\n", m_name,
                         static_cast<long long>(m_timer.elapsed()));
    }

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    const char*   m_name;
    QElapsedTimer m_timer;
};

// 手动检查点，用来细分一个 Scope 内部的耗时
class Probe
{
public:
    explicit Probe(const char* label) : m_label(label)
    {
        if (enabled())
            m_timer.start();
    }

    void mark(const char* what)
    {
        if (!m_timer.isValid())
            return;
        std::fprintf(stderr, "[timing]   %s/%s %6lld ms\n", m_label, what,
                     static_cast<long long>(m_timer.restart()));
    }

private:
    const char*   m_label;
    QElapsedTimer m_timer;
};

} // namespace Timing
