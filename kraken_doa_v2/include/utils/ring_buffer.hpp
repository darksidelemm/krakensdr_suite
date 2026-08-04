#pragma once

#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <memory>
#include <cstring>

template<typename T>
class RingBufferBase {
protected:
    std::vector<T> buffer;
    std::atomic<size_t> write_pos{0};
    std::atomic<size_t> read_pos{0};
    size_t size_mask;
    
    std::atomic<size_t> total_writes{0};
    std::atomic<size_t> total_reads{0};
    std::atomic<size_t> overruns{0};
    std::atomic<size_t> underruns{0};
    
public:
    RingBufferBase(size_t size);
    virtual ~RingBufferBase() = default;
    
    size_t available() const;
    size_t capacity() const;
    float fill_percentage() const;
    void get_stats(size_t& writes, size_t& reads, size_t& over, size_t& under) const;
    void reset_stats();
    void clear();
};

template<typename T>
class BlockingRingBuffer : public RingBufferBase<T> {
private:
    mutable std::mutex cv_mutex;
    mutable std::condition_variable data_available;
    std::atomic<bool> shutdown_flag{false};
    
public:
    BlockingRingBuffer(size_t size);
    
    bool write(const T* data, size_t count);
    bool write_overwrite(const T* data, size_t count);
    size_t read(T* data, size_t max_count);
    size_t read_blocking_min(T* data, size_t max_count, size_t min_count, 
                            std::chrono::milliseconds timeout = std::chrono::milliseconds(1000));
    void shutdown();
};

class AudioRingBuffer : public RingBufferBase<float> {
public:
    AudioRingBuffer(size_t size);
    size_t write(const float* samples, size_t count);
    void read(float* output, size_t count);
};

// Template implementations
template<typename T>
RingBufferBase<T>::RingBufferBase(size_t size) {
    size_t actual_size = 1;
    while (actual_size < size) actual_size <<= 1;
    
    buffer.resize(actual_size);
    size_mask = actual_size - 1;
}

template<typename T>
size_t RingBufferBase<T>::available() const {
    return (write_pos.load(std::memory_order_acquire) - read_pos.load(std::memory_order_acquire)) & size_mask;
}

template<typename T>
size_t RingBufferBase<T>::capacity() const { 
    return buffer.size(); 
}

template<typename T>
float RingBufferBase<T>::fill_percentage() const {
    return (available() * 100.0f) / capacity();
}

template<typename T>
void RingBufferBase<T>::get_stats(size_t& writes, size_t& reads, size_t& over, size_t& under) const {
    writes = total_writes.load();
    reads = total_reads.load();
    over = overruns.load();
    under = underruns.load();
}

template<typename T>
void RingBufferBase<T>::reset_stats() {
    total_writes = 0;
    total_reads = 0;
    overruns = 0;
    underruns = 0;
}

template<typename T>
void RingBufferBase<T>::clear() {
    write_pos.store(0, std::memory_order_release);
    read_pos.store(0, std::memory_order_release);
}

template<typename T>
BlockingRingBuffer<T>::BlockingRingBuffer(size_t size) : RingBufferBase<T>(size) {}

template<typename T>
bool BlockingRingBuffer<T>::write(const T* data, size_t count) {
    if (shutdown_flag.load()) return false;
    
    size_t current_write = this->write_pos.load();
    size_t current_read = this->read_pos.load();
    
    size_t available_space = (current_read - current_write - 1) & this->size_mask;
    if (count > available_space) {
        return false;
    }
    
    for (size_t i = 0; i < count; i++) {
        this->buffer[(current_write + i) & this->size_mask] = data[i];
    }
    
    this->write_pos.store((current_write + count) & this->size_mask);
    this->total_writes.fetch_add(1);
    
    {
        std::lock_guard<std::mutex> lock(cv_mutex);
        data_available.notify_one();
    }
    
    return true;
}

template<typename T>
bool BlockingRingBuffer<T>::write_overwrite(const T* data, size_t count) {
    if (shutdown_flag.load()) return false;
    
    size_t current_write = this->write_pos.load();
    size_t current_read = this->read_pos.load();
    
    size_t available_space = (current_read - current_write - 1) & this->size_mask;
    
    size_t to_write = std::min(count, available_space);
    
    if (to_write < count) {
        this->overruns.fetch_add(count - to_write);
    }
    
    for (size_t i = 0; i < to_write; i++) {
        this->buffer[(current_write + i) & this->size_mask] = data[i];
    }
    
    this->write_pos.store((current_write + to_write) & this->size_mask);
    this->total_writes.fetch_add(1);
    
    {
        std::lock_guard<std::mutex> lock(cv_mutex);
        data_available.notify_one();
    }
    
    return true;
}

template<typename T>
size_t BlockingRingBuffer<T>::read(T* data, size_t max_count) {
    size_t current_write = this->write_pos.load();
    size_t current_read = this->read_pos.load();
    
    size_t available = (current_write - current_read) & this->size_mask;
    size_t to_read = std::min(max_count, available);
    
    for (size_t i = 0; i < to_read; i++) {
        data[i] = this->buffer[(current_read + i) & this->size_mask];
    }
    
    this->read_pos.store((current_read + to_read) & this->size_mask);
    if (to_read > 0) this->total_reads.fetch_add(1);
    
    return to_read;
}

template<typename T>
size_t BlockingRingBuffer<T>::read_blocking_min(T* data, size_t max_count, size_t min_count, 
                        std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(cv_mutex);
    
    if (!data_available.wait_for(lock, timeout, [this, min_count]() {
        return shutdown_flag.load() || this->available() >= min_count;
    })) {
        lock.unlock();
        return read(data, max_count);
    }
    
    if (shutdown_flag.load()) return 0;
    
    lock.unlock();
    return read(data, max_count);
}

template<typename T>
void BlockingRingBuffer<T>::shutdown() {
    shutdown_flag = true;
    {
        std::lock_guard<std::mutex> lock(cv_mutex);
        data_available.notify_all();
    }
}
