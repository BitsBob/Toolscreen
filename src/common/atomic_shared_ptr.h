#pragma once

// std::atomic<std::shared_ptr<T>> (P0718) is not implemented by every standard library Toolscreen is
// built with (libc++ on macOS lacks it). AtomicSharedPtr<T> is that type where available and an
// equivalent lock-based implementation elsewhere.

#include <atomic>
#include <memory>
#include <mutex>

#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
template <class T> using AtomicSharedPtr = std::atomic<std::shared_ptr<T>>;
#else
template <class T> class AtomicSharedPtr {
public:
    AtomicSharedPtr() noexcept = default;
    AtomicSharedPtr(std::shared_ptr<T> value) noexcept : m_value(std::move(value)) {}
    AtomicSharedPtr(const AtomicSharedPtr&) = delete;
    AtomicSharedPtr& operator=(const AtomicSharedPtr&) = delete;

    std::shared_ptr<T> load(std::memory_order = std::memory_order_seq_cst) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_value;
    }
    void store(std::shared_ptr<T> value, std::memory_order = std::memory_order_seq_cst) {
        std::shared_ptr<T> previous;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            previous = std::exchange(m_value, std::move(value));
        }
        // `previous` is released outside the lock so a destructor never runs while it is held.
    }
    std::shared_ptr<T> exchange(std::shared_ptr<T> value, std::memory_order = std::memory_order_seq_cst) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return std::exchange(m_value, std::move(value));
    }
    bool compare_exchange_strong(std::shared_ptr<T>& expected, std::shared_ptr<T> desired, std::memory_order = std::memory_order_seq_cst,
                                 std::memory_order = std::memory_order_seq_cst) {
        std::shared_ptr<T> previous;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_value == expected) {
                previous = std::exchange(m_value, std::move(desired));
                return true;
            }
            expected = m_value;
        }
        return false;
    }
    operator std::shared_ptr<T>() const { return load(); }

private:
    mutable std::mutex m_mutex;
    std::shared_ptr<T> m_value;
};
#endif
