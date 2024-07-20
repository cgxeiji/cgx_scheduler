#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <string>

#include "inner.hpp"

namespace sch {

enum class direction_t {
    next,
    stay,
    reset,
};

#define SCH_SLEEP(ticks) [](auto& s) { return s.sleep(ticks); }

template <std::size_t N>
class stage_t {
   public:
    void run() {
        if (!m_stages[m_index]) {
            m_index = 0;
        }
        const auto dir = m_stages[m_index](*this);
        switch (dir) {
            case direction_t::next:
                m_index = (m_index + 1) % N;
                break;
            case direction_t::stay:
                break;
            case direction_t::reset:
                m_index = 0;
                break;
        }
    }

    direction_t sleep(const std::size_t ticks) {
        if (!m_is_sleeping) {
            m_deadline = m_timer.make_deadline(ticks);
            m_is_sleeping = true;
        }
        if (m_timer.is_expired(m_deadline)) {
            m_is_sleeping = false;
            return direction_t::next;
        }
        return direction_t::stay;
    }

    stage_t(std::array<std::function<direction_t(stage_t&)>, N> stages)
        : m_stages(stages) {}

   private:
    std::size_t m_index{0};
    std::array<std::function<direction_t(stage_t&)>, N> m_stages;

    inner::timer_t& m_timer{inner::timer_t::instance()};
    bool m_is_sleeping{false};
    inner::timer_t::time_t m_deadline{0};
};

class task_t {
   public:
    using time_t = inner::timer_t::time_t;
    using duration_t = inner::timer_t::duration_t;

    enum class status_t {
        invalid,
        running,
        stopped,
        paused,
        delayed,
    };

    bool is_ready() const {
        if (m_status == status_t::invalid) {
            return false;
        }
        if (m_status == status_t::stopped) {
            return false;
        }
        if (m_period_tick > 0 &&
            m_timer.elapsed(m_last_run_tick) >= m_period_tick) {
            return true;
        }
        if (m_period_tick < 0 &&
            m_timer.elapsed(m_last_run_tick) >= -m_period_tick) {
            return true;
        }
        return false;
    }

    void run() {
        if (m_status == status_t::invalid) {
            return;
        }
        if (m_status == status_t::stopped) {
            return;
        }

        m_status = status_t::running;
        if (m_period_tick > 0) {
            m_last_run_tick = m_timer.now();
        } else {
            m_last_run_tick = m_timer.now() - ticks_left();
        }
        auto _watch = m_run_time.measure();
        const auto keep = m_callback();
        if (keep) {
            m_status = status_t::paused;
        } else {
            m_status = status_t::invalid;
        }
    }

    operator bool() const { return m_status != status_t::invalid; }

    const auto& name() const { return m_name; }
    const auto& period() const { return m_period_tick; }
    const auto& run_time() const { return m_run_time.duration(); }
    auto& run_time() { return m_run_time.duration(); }
    void reset_run_time() { m_run_time.reset(); }
    duration_t ticks_left() const {
        if (m_status != status_t::paused) {
            return 0;
        }
        if (m_period_tick < 0) {
            return -m_period_tick - m_timer.elapsed(m_last_run_tick);
        }
        return m_period_tick - m_timer.elapsed(m_last_run_tick);
    }
    const auto status() const {
        if (ticks_left() < 0) {
            return status_t::delayed;
        }
        return m_status;
    }

    task_t() = default;
    task_t(const char* name, const duration_t period,
           std::function<bool()> callback)
        : m_callback(callback),
          m_period_tick(period),
          m_status(status_t::paused) {
        const auto len = std::strlen(name);
        memcpy(m_name.data(), name, len > 8 ? 8 : len);
    }

    task_t& operator=(const task_t& other) {
        m_name = other.m_name;
        m_callback = other.m_callback;
        m_period_tick = other.m_period_tick;
        m_last_run_tick = other.m_last_run_tick;
        m_status = other.m_status;
        m_run_time = other.m_run_time;
        return *this;
    }
    task_t(const task_t& other) {
        m_name = other.m_name;
        m_callback = other.m_callback;
        m_period_tick = other.m_period_tick;
        m_last_run_tick = other.m_last_run_tick;
        m_status = other.m_status;
        m_run_time = other.m_run_time;
    }
    task_t(task_t&&) = default;

    ~task_t() = default;

   private:
    std::array<char, 8> m_name{"\0"};
    std::function<bool()> m_callback{nullptr};
    duration_t m_period_tick;
    inner::timer_t& m_timer{inner::timer_t::instance()};

    time_t m_last_run_tick{0};
    time_t m_exec_ticks{0};

    inner::stop_watch_t m_run_time;

    volatile status_t m_status{status_t::invalid};
};

class scheduler_t {
   public:
    using time_t = inner::timer_t::time_t;
    using duration_t = inner::timer_t::duration_t;

    void run(const uint8_t priority = 0) {
        if (priority >= m_tasks_list.size()) {
            return;
        }
        const auto available_tasks = std::count_if(
            m_tasks_list[priority].begin(), m_tasks_list[priority].end(),
            [](const task_t& t) { return bool(t); });

        if (available_tasks == 0) {
            return;
        }

        auto _watch = m_task_watches[priority].measure();
        auto& index = m_task_indices[priority];
        while (!m_tasks_list[priority][index]) {
            index = (index + 1) % m_tasks_list[priority].size();
        }
        auto& task = m_tasks_list[priority][index];
        if (task.is_ready()) {
            task.run();
        }
        index = (index + 1) % m_tasks_list[priority].size();

        // for (auto& task : m_tasks_list[priority]) {
        //     if (task && task.is_ready()) {
        //         task.run();
        //     }
        // }
    }

    bool add(const char* name, const duration_t period,
             std::function<bool()> cb, const uint8_t priority = 0) {
        if (priority >= m_tasks_list.size()) {
            return false;
        }
        task_t task{name, period, cb};

        for (auto& t : m_tasks_list[priority]) {
            if (!t) {
                t = task;
                return true;
            }
        }
        return false;
    }

    void stats(std::function<void(const char*)> print) {
        static int32_t last_lines = 0;
        std::array<char, 128> buf;
        // print("\033[2J");
        print("\033[H");

        int32_t lines = 0;
        for (uint8_t p = 0; p < m_tasks_list.size(); ++p) {
            const auto available_tasks =
                std::count_if(m_tasks_list[p].begin(), m_tasks_list[p].end(),
                              [](const task_t& t) { return bool(t); });

            if (available_tasks == 0) {
                continue;
            }

            const auto min = m_task_watches[p].duration().min() ==
                                     std::numeric_limits<time_t>::max()
                                 ? 0
                                 : m_task_watches[p].duration().min();
            const auto max = m_task_watches[p].duration().max() ==
                                     std::numeric_limits<time_t>::lowest()
                                 ? 0
                                 : m_task_watches[p].duration().max();
            std::snprintf(buf.data(), buf.size(),
                          "== PRIORITY %2u == [ tasks: %-2u, mean: %lluus, "
                          "min: %lluus, max: %lluus ]",
                          p, available_tasks,
                          m_task_watches[p].duration().mean(), min, max);
            std::snprintf(buf.data() + std::strlen(buf.data()), buf.size(),
                          "%*s\n", 78 - std::strlen(buf.data()), "");
            print("\033[30;42m");
            print(buf.data());
            print("\033[0m");
            lines++;

            std::snprintf(buf.data(), buf.size(),
                          "   %10s %12s %12s %12s %12s %12s\n", "task", "every",
                          "next", "run (us)", "min (us)", "max (us)");
            print("\033[90m");
            print(buf.data());
            print("\033[0m");
            lines++;

            for (const auto& task : m_tasks_list[p]) {
                if (!task) {
                    continue;
                }
                char state[3] = "  ";
                switch (task.status()) {
                    case task_t::status_t::running:
                        state[0] = 'O';
                        break;
                    case task_t::status_t::stopped:
                        state[1] = 'S';
                        break;
                    case task_t::status_t::paused:
                        state[1] = 'p';
                        break;
                    case task_t::status_t::delayed:
                        state[0] = 'd';
                        break;
                    case task_t::status_t::invalid:
                        state[1] = '-';
                        break;
                }
                const auto run_time = task.run_time();
                const auto min =
                    run_time.min() == std::numeric_limits<time_t>::max()
                        ? 0
                        : run_time.min();
                const auto max =
                    run_time.max() == std::numeric_limits<time_t>::lowest()
                        ? 0
                        : run_time.max();

                std::snprintf(buf.data(), buf.size(),
                              "%2s [%8s] %12lld %12lld %12llu %12llu %12llu\n",
                              state, task.name().data(), task.period(),
                              task.ticks_left(), run_time.mean(), min, max);
                print(buf.data());
                lines++;
            }
            print("\033[2K");
            print("\n");
            lines++;
        }

        for (int32_t i = 0; i < last_lines - lines; ++i) {
            print("\033[2K");
            print("\n");
        }
    }

    void reset_stats() {
        for (uint8_t p = 0; p < m_tasks_list.size(); ++p) {
            auto& tasks = m_tasks_list[p];
            m_task_watches[p].reset();

            for (auto& task : tasks) {
                task.reset_run_time();
            }
        }
    }

    scheduler_t(std::function<time_t()> on_now_cb) {
        inner::timer_t::instance().set_now_cb(on_now_cb);
    }

   private:
    constexpr static std::size_t m_max_tasks = 10;

    using task_list_t = std::array<task_t, m_max_tasks>;
    std::array<task_list_t, m_max_tasks> m_tasks_list;
    std::array<inner::stop_watch_t, m_max_tasks> m_task_watches;
    std::array<uint8_t, m_max_tasks> m_task_indices;
};

extern scheduler_t scheduler;

}  // namespace sch
