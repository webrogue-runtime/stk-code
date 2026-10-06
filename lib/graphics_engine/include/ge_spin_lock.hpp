#ifndef HEADER_GE_SPIN_LOCK_HPP
#define HEADER_GE_SPIN_LOCK_HPP

#ifdef __wasi__
#include <pthread.h>
#else
#include <atomic>
#endif

class GESpinLock
{
#ifdef __wasi__
    mutable pthread_mutex_t m_locked = PTHREAD_MUTEX_INITIALIZER;
#else
    mutable std::atomic_flag m_locked = ATOMIC_FLAG_INIT;
#endif
public:
#ifdef __wasi__
    void lock() const   { pthread_mutex_lock(&m_locked); }
    void unlock() const { pthread_mutex_unlock(&m_locked); }
#else
    void lock() const
                  { while (m_locked.test_and_set(std::memory_order_acquire)); }
    void unlock() const          { m_locked.clear(std::memory_order_release); }
#endif
};

#endif
