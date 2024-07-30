#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <string>

#include "inner.hpp"

namespace cgx::sch {

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
            m_deadline    = m_timer.make_deadline(ticks);
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
    std::size_t                                         m_index{0};
    std::array<std::function<direction_t(stage_t&)>, N> m_stages;

    inner::timer_t&        m_timer{inner::timer_t::instance()};
    bool                   m_is_sleeping{false};
    inner::timer_t::time_t m_deadline{0};
};

class task_t {
   public:
    using time_t     = inner::timer_t::time_t;
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
        m_ticks_left = _ticks_left();
        if (m_ticks_left <= 0) {
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

        m_exec_time.stop();
        m_exec_time.start();
        m_status = status_t::running;
        if (m_period_tick >= 0) {
            m_last_run_tick = m_timer.now();
        } else {
            m_last_run_tick = m_timer.now() - ticks_left();
        }
        auto       _watch = m_run_time.measure();
        const auto keep   = m_callback();
        if (keep) {
            m_status = status_t::paused;
        } else {
            m_status = status_t::invalid;
        }
    }

    void invalidate() { m_status = status_t::invalid; }
    void stop() { m_status = status_t::stopped; }
    void start() {
        m_status = status_t::paused;
        m_run_time.reset();
        m_exec_time.reset();
        m_exec_time.start();
    }

    operator bool() const { return m_status != status_t::invalid; }

    const auto& name() const { return m_name; }
    const auto& period() const { return m_period_tick; }
    const auto& actual_period() const { return m_exec_time.duration(); }
    const auto& last_run_tick() const { return m_last_run_tick; }
    const auto& run_time() const { return m_run_time.duration(); }
    auto&       run_time() { return m_run_time.duration(); }
    void        reset_run_time() { m_run_time.reset(); }
    duration_t  ticks_left() const {
        if (m_status != status_t::paused) {
            return 0;
        }
        if (m_period_tick == 0) {
            return 0;
        }
        return m_ticks_left;
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
          m_status(status_t::running) {
        const auto len = std::strlen(name);
        memcpy(m_name.data(), name, len > 8 ? 8 : len);
    }

    task_t& operator=(const task_t& other) {
        m_name          = other.m_name;
        m_callback      = other.m_callback;
        m_period_tick   = other.m_period_tick;
        m_last_run_tick = other.m_last_run_tick;
        m_status        = other.m_status;
        m_run_time      = other.m_run_time;
        m_exec_time     = other.m_exec_time;
        return *this;
    }
    task_t(const task_t& other) {
        m_name          = other.m_name;
        m_callback      = other.m_callback;
        m_period_tick   = other.m_period_tick;
        m_last_run_tick = other.m_last_run_tick;
        m_status        = other.m_status;
        m_run_time      = other.m_run_time;
        m_exec_time     = other.m_exec_time;
    }
    task_t(task_t&&) = default;

    ~task_t() = default;

   private:
    std::array<char, 9> m_name{"\0"};
    std::function<bool()> m_callback{nullptr};
    duration_t            m_period_tick;
    duration_t            m_actual_period_tick{};
    inner::timer_t&       m_timer{inner::timer_t::instance()};

    mutable duration_t m_ticks_left{};
    volatile time_t m_last_run_tick{0};
    time_t m_exec_ticks{0};
    mutable time_t m_prev{0};

    inner::stop_watch_t m_run_time;
    inner::stop_watch_t m_exec_time;

    volatile status_t m_status{status_t::invalid};

    duration_t _ticks_left() const {
        if (m_period_tick < 0) {
            if (m_status == status_t::delayed) {
                return -m_timer.elapsed(m_last_run_tick);
            }
            //  return -m_period_tick - m_timer.elapsed(m_last_run_tick);
            auto current = m_timer.now() % -m_period_tick;
            if (current < m_prev) {
                m_prev = current;
                return -m_timer.elapsed(m_last_run_tick);
            }
            m_prev = current;
            return -m_period_tick - current;
        }
        return m_period_tick - m_timer.elapsed(m_last_run_tick);
    }
};

class thread_t {
   public:
    virtual void run() noexcept = 0;
    virtual bool add(const task_t& task) noexcept = 0;
    virtual bool pkill(const char* name) noexcept = 0;
    virtual bool start(const char* name) noexcept = 0;
    virtual bool stop(const char* name) noexcept = 0;
    virtual void reset_stats() noexcept = 0;

    virtual const inner::stop_watch_t& watch() const noexcept = 0;

    virtual std::size_t size() const noexcept { return 0; }

    virtual const task_t* begin() const noexcept = 0;
    virtual task_t* begin() noexcept = 0;
    virtual const task_t* end() const noexcept = 0;
    virtual task_t* end() noexcept = 0;

    virtual void lock() const noexcept = 0;
    virtual void unlock() const noexcept = 0;

    void safe(std::function<void(thread_t*)> cb) {
        this->lock();
        cb(this);
        this->unlock();
    }

    virtual ~thread_t() = default;
};

template <std::size_t N>
class thread : public thread_t {
   public:
    void run() noexcept final {
        if (this->size() == 0) {
            return;
        }

        this->lock();

        auto _watch = m_watch.measure();
        while (!m_tasks_list[m_index]) {
            m_index = (m_index + 1) % N;
        }
        auto& task = m_tasks_list[m_index];
        if (task.is_ready()) {
            task.run();
        }
        m_index = (m_index + 1) % N;

        this->unlock();
    }

    std::size_t size() const noexcept final {
        this->lock();
        std::size_t count{0};
        for (const auto& task : m_tasks_list) {
            if (task) {
                count++;
            }
        }
        this->unlock();
        return count;
    }

    bool add(const task_t& task) noexcept final {
        this->lock();
        for (auto& t : m_tasks_list) {
            if (!t) {
                t = task;
                this->unlock();
                return true;
            }
        }
        this->unlock();
        return false;
    }

    bool pkill(const char* name) noexcept final {
        this->lock();
        for (auto& task : m_tasks_list) {
            if (task && std::strncmp(task.name().data(), name, 8) == 0) {
                task.invalidate();
                this->unlock();
                return true;
            }
        }
        this->unlock();
        return false;
    }

    bool start(const char* name) noexcept final {
        this->lock();
        for (auto& task : m_tasks_list) {
            if (task && std::strncmp(task.name().data(), name, 8) == 0) {
                task.start();
                this->unlock();
                return true;
            }
        }
        this->unlock();
        return false;
    }

    bool stop(const char* name) noexcept final {
        this->lock();
        for (auto& task : m_tasks_list) {
            if (task && std::strncmp(task.name().data(), name, 8) == 0) {
                task.stop();
                this->unlock();
                return true;
            }
        }
        this->unlock();
        return false;
    }

    void reset_stats() noexcept final {
        this->lock();
        for (auto& task : m_tasks_list) {
            task.reset_run_time();
        }
        m_watch.reset();
        this->unlock();
    }

    const inner::stop_watch_t& watch() const noexcept final { return m_watch; }

    const task_t* begin() const noexcept final { return m_tasks_list.data(); }
    task_t* begin() noexcept final { return m_tasks_list.data(); }
    const task_t* end() const noexcept final {
        return m_tasks_list.data() + m_tasks_list.size();
    }
    task_t* end() noexcept final {
        return m_tasks_list.data() + m_tasks_list.size();
    }

    void set_lock_unlock_cb(std::function<void()> lock_cb,
                            std::function<void()> unlock_cb) {
        m_lock_cb = lock_cb;
        m_unlock_cb = unlock_cb;
    }

    void lock() const noexcept final {
        if (m_lock_cb) {
            m_lock_cb();
        }
    }
    void unlock() const noexcept final {
        if (m_unlock_cb) {
            m_unlock_cb();
        }
    }

   private:
    std::array<task_t, N> m_tasks_list;
    inner::stop_watch_t m_watch;
    std::size_t m_index{0};

    std::function<void()> m_lock_cb{nullptr};
    std::function<void()> m_unlock_cb{nullptr};
};

class scheduler_t {
   public:
    using time_t     = inner::timer_t::time_t;
    using duration_t = inner::timer_t::duration_t;

    template <typename T>
    using observer_ptr = T*;

    void run(const uint8_t thread = 0) {
        if (thread >= m_threads.size()) {
            return;
        }
        m_threads[thread]->run();
    }

    bool add(observer_ptr<thread_t> thread) {
        for (auto& t : m_threads) {
            if (!t) {
                t = thread;
                return true;
            }
        }
        return false;
    }
    bool add(const task_t& task, const uint8_t thread = 0) {
        if (thread >= m_threads.size()) {
            return false;
        }
        return m_threads[thread]->add(task);
    }

    bool pkill(const char* name) {
        for (auto& t : m_threads) {
            if (t && t->pkill(name)) {
                return true;
            }
        }
        return false;
    }

    bool start(const char* name) {
        for (auto& t : m_threads) {
            if (t && t->start(name)) {
                return true;
            }
        }
        return false;
    }

    bool stop(const char* name) {
        for (auto& t : m_threads) {
            if (t && t->stop(name)) {
                return true;
            }
        }
        return false;
    }

    const auto& threads() const { return m_threads; }

    void reset_stats() {
        for (auto& t : m_threads) {
            if (t) {
                t->reset_stats();
            }
        }
    }

    scheduler_t(std::function<time_t()> on_now_cb) {
        inner::timer_t::instance().set_now_cb(on_now_cb);
    }

   private:
    std::array<observer_ptr<thread_t>, 8> m_threads{nullptr};
};

extern scheduler_t scheduler;

}  // namespace cgx::sch
