#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <limits>

namespace cgx::sch {
namespace inner {

class timer_t {
   public:
    using time_t = std::uint64_t;
    using duration_t = std::int64_t;

    void set_now_cb(std::function<time_t()> cb) { m_on_now_cb = cb; }

    time_t now() const {
        if (m_on_now_cb == nullptr) {
            return 0;
        }
        return m_on_now_cb();
    }

    duration_t elapsed(const time_t start) const {
        return static_cast<duration_t>(now()) - static_cast<duration_t>(start);
    }

    time_t make_deadline(const time_t delay) const { return now() + delay; }

    bool is_expired(const time_t deadline) const { return now() >= deadline; }

    static timer_t& instance() {
        static timer_t instance;
        return instance;
    }

   private:
    std::function<time_t()> m_on_now_cb;

    timer_t() = default;
    ~timer_t() = default;
    timer_t(const timer_t&) = delete;
    timer_t& operator=(const timer_t&) = delete;
    timer_t(timer_t&&) = delete;
    timer_t& operator=(timer_t&&) = delete;
};

template <typename T, std::size_t N = 32>
class min_max_mean_t {
   public:
    constexpr void add(const T value) {
        if (!m_is_mean_valid) {
            m_values.fill(value);
            m_is_mean_valid = true;
        }
        m_min = std::min(m_min, value);
        m_max = std::max(m_max, value);
        m_values[m_index] = value;
        m_index = (m_index + 1) % N;
        m_mean = 0;
        for (const auto& v : m_values) {
            m_mean += v;
        }
        m_mean /= N;
    }

    constexpr operator T() const { return m_mean; }
    constexpr T operator()() const { return m_mean; }
    constexpr T operator()(const T value) {
        add(value);
        return m_mean;
    }
    constexpr T operator=(const T value) {
        add(value);
        return m_mean;
    }

    constexpr T min() const { return m_min; }
    constexpr T max() const { return m_max; }
    constexpr T mean() const { return m_mean; }

    constexpr void reset() {
        m_min = std::numeric_limits<T>::max();
        m_max = std::numeric_limits<T>::lowest();
        m_mean = 0;
        m_index = 0;
        m_values.fill(T{});
        m_is_mean_valid = false;
    }

   private:
    T m_min{std::numeric_limits<T>::max()};
    T m_max{std::numeric_limits<T>::lowest()};
    T m_mean{0};
    bool m_is_mean_valid{false};
    size_t m_index{0};
    std::array<T, N> m_values{};
};

class stop_watch_t {
   public:
    class disposable_stop_watch_t {
       public:
        disposable_stop_watch_t(stop_watch_t& sw) : m_sw(sw) { m_sw.start(); }
        ~disposable_stop_watch_t() { m_sw.stop(); }

       private:
        stop_watch_t& m_sw;
    };

    disposable_stop_watch_t measure() { return disposable_stop_watch_t(*this); }

    void start() { m_start = inner::timer_t::instance().now(); }

    void stop() { m_duration = inner::timer_t::instance().elapsed(m_start); }

    void reset() { m_duration.reset(); }

    const auto& duration() const { return m_duration; }

   private:
    inner::timer_t::time_t m_start{0};
    inner::min_max_mean_t<inner::timer_t::time_t> m_duration;
};

}  // namespace inner
}  // namespace cgx::sch
