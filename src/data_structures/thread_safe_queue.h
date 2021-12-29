/**
 * thread_safe_queue.h
 *
 * Numerous classes (most notably the server<T> class) use boost::circular_buffer
 * as a means of creating a fixed-length FIFO queue. This is ideal for purposes
 * of logging. However, since many applications require concurrent access of
 * boost::circular_buffer from multiple threads, it is prudent to create a
 * simple, thread-safe wrapper around boost::circular_buffer. The class
 * ThreadSafeQueue should accomplish this by maintaining a circular buffer as
 * a private member and implementing read-write locking around the most relevant
 * methods of the boost::circular_buffer class.
 */
#ifndef CHORD_AND_DHASH_THREAD_SAFE_QUEUE_H
#define CHORD_AND_DHASH_THREAD_SAFE_QUEUE_H

#include "thread_safe.h"
#include <boost/circular_buffer.hpp>

/**
 * Thread safe wrapper around circular queue.
 * @tparam T Type of data being stored in circular buffer.
 */
template<typename T>
class ThreadSafeQueue : ThreadSafe {
public:
    /**
     * Initialize a ThreadSafeQueue with a fixed size.
     * @param size The fixed size of the FIFO queue.
     */
    explicit ThreadSafeQueue(int size)
        : queue_(size)
    {}

    /**
     * Initialize a ThreadSafeQueue from a non-thread-safe circular buffer.
     * @param queue Circular buffer to be moved into queue_ field.
     */
    explicit ThreadSafeQueue(boost::circular_buffer<T> queue)
        : queue_(std::move(queue))
    {}

    /**
     * Disable copy-assignment for thread safety.
     */
    ThreadSafeQueue(const ThreadSafeQueue &rhs) = delete;

    /**
     * Create thread-safe move constructor by locking rhs and then moving its
     * members.
     * @param rhs Queue to copy-construct from.
     */
    ThreadSafeQueue(ThreadSafeQueue &&rhs) noexcept
    {
        ReadLock(rhs.mutex_);
        queue_ = std::move(rhs.queue_);
    }

    /**
     * Destructor.
     */
    ~ThreadSafeQueue() = default;

    /**
     * Add el to back of queue, push front element out of queue if it exceeds
     * max length.
     * @param el El to insert at back of queue.
     */
    void PushBack(const T &el)
    {
        WriteLock lock(mutex_);
        queue_.push_back(el);
    }

    /**
     * Add el to front of queue, push back elemenet out of queue if it exceeds
     * max length.
     * @param el El to push to front of queue.
     */
    void PushFront(const T &el)
    {
        WriteLock lock(mutex_);
        queue_.push_front(el);
    }

    /**
     * Remove back element of queue.
     */
    void PopBack()
    {
        WriteLock lock(mutex_);
        queue_.pop_back();
    }

    /**
     * Remove front element of queue.
     */
    void PopFront()
    {
        WriteLock lock(mutex_);
        queue_.pop_front();
    }

    /**
     * @return Number of elements currently in queue.
     */
    unsigned long Size()
    {
        ReadLock lock(mutex_);
        return queue_.size();
    }

    /**
     * @param index A position in the queue.
     * @return The element at position "index" in queue, or throw an error
     *         if that position does not exist.
     */
    T At(int index)
    {
        ReadLock lock(mutex_);
        return queue_.at(index);
    }

    /**
     * Overload bracket operator for array-style index lookups.
     * @param index Index of array passed between brackets.
     * @return The element at position "index" in queue.
     */
    T operator [](int index)
    {
        ReadLock lock(mutex_);
        return queue_[index];
    }

    /**
     * @return The circular buffer into which we insert/read elements. This
     *         is not guaranteed to be up-to-date, since queued write operations
     *         may execute immediately after GetBuffer releases its read lock.
     */
    boost::circular_buffer<T> GetBuffer()
    {
        ReadLock lock(mutex_);
        return queue_;
    }

private:
    /// The class which boost couldn't bother to make thread-safe.
    boost::circular_buffer<T> queue_;
};

#endif