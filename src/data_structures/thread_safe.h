#ifndef CHORD_AND_DHASH_THREAD_SAFE_H
#define CHORD_AND_DHASH_THREAD_SAFE_H

#include <mutex>
#include <shared_mutex>

class ThreadSafe {
public:
    ThreadSafe() = default;

//    ThreadSafe(ThreadSafe &&rhs) noexcept
//    {
//        WriteLock rhs_lk(rhs.mutex_);
//    }

    mutable std::shared_mutex mutex_;
    using WriteLock = std::unique_lock<std::shared_mutex>;
    using ReadLock = std::shared_lock<std::shared_mutex>;
};


#endif