// LockFreeRingBufferSPSCQueue_study.hpp
//
// STUDY VERSION
//
// This file preserves:
// 1. Original implementation (commented)
// 2. Placement-new implementation (commented)
// 3. Active C++20 implementation using construct_at()/destroy_at()
//
// Purpose: compare approaches side-by-side.

#pragma once

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

template<typename T>
class LockFreeRingBufferSPSCQueue
{
private:

    /*
     * OPTION 1 (Original implementation)
     *
     * std::unique_ptr<T[]> _buffer;
     *
     * _buffer = std::make_unique<T[]>(_capacity);
     *
     * Pros:
     *  - Simple
     *  - Automatic destruction
     *
     * Cons:
     *  - Requires T to be default constructible
     */

    /*
     * OPTION 2 (Placement-new implementation)
     *
     * T* _buffer;
     *
     * new (&_buffer[idx]) T(value);
     * _buffer[idx].~T();
     *
     * Pros:
     *  - Supports non-default-constructible types
     *
     * Cons:
     *  - Manual lifetime management
     */

    /*
     * OPTION 3 (Current implementation)
     *
     * Raw aligned storage +
     * std::construct_at()
     * std::destroy_at()
     */

    T* _buffer{nullptr};

    std::size_t _capacity{};
    std::size_t _mask{};

    alignas(std::hardware_destructive_interference_size)
    std::atomic<size_t> _writeIndex{0};

    alignas(std::hardware_destructive_interference_size)
    std::atomic<size_t> _readIndex{0};

public:

    explicit LockFreeRingBufferSPSCQueue(size_t capacity)
    {
        if (capacity == 0)
        {
            throw std::invalid_argument("Capacity can't be 0");
        }

        _capacity = capacity;

        if ((_capacity & (_capacity - 1)) != 0)
        {
            _capacity = std::bit_ceil(_capacity);
        }

        _mask = _capacity - 1;

        // OPTION 1:
        // _buffer = std::make_unique<T[]>(_capacity);

        // OPTION 2 & 3:
        _buffer = static_cast<T*>(::operator new(sizeof(T) * _capacity, std::align_val_t(alignof(T))));
        
    }

     //OPTION 2 & 3 (Option 1 does not need a custom destructor)
     //~LockFreeRingBufferSPSC() = default; // or remove entirely for option 1
    ~LockFreeRingBufferSPSCQueue()
    {
        size_t read = _readIndex.load(std::memory_order_relaxed);
        size_t write = _writeIndex.load(std::memory_order_relaxed);

        while (read != write)
        {
            // OPTION 2:
            // _buffer[read].~T();

            // OPTION 3 (ACTIVE)
            std::destroy_at(&_buffer[read]);

            read = (read + 1) & _mask;
        }

        ::operator delete(_buffer, std::align_val_t(alignof(T)));
    }
    

    LockFreeRingBufferSPSCQueue(const LockFreeRingBufferSPSCQueue&) = delete;
     //q2 = q1; => ERROR because operator=() deleted 
    LockFreeRingBufferSPSCQueue& operator=(const LockFreeRingBufferSPSCQueue&) = delete;
    LockFreeRingBufferSPSCQueue(LockFreeRingBufferSPSCQueue&&) = delete;
    LockFreeRingBufferSPSCQueue& operator=(LockFreeRingBufferSPSCQueue&&) = delete;

    bool push(const T& data)
    {
        const std::size_t writeIndex = _writeIndex.load(std::memory_order_relaxed);
        const std::size_t nextWriteIndex = (writeIndex + 1) & _mask;

        if (nextWriteIndex == _readIndex.load(std::memory_order_acquire))
        {
            return false; //Buffer is full
        }

        // OPTION 1:
        // _buffer[writeIndex] = data;

        // OPTION 2:
        // new (&_buffer[writeIndex]) T(data);

        // OPTION 3 (ACTIVE) => Need copy constructor()
        std::construct_at(&_buffer[writeIndex], data);

        _writeIndex.store(nextWriteIndex, std::memory_order_release);

        return true;
    }

    bool push(T&& data)
    {
        const std::size_t writeIndex = _writeIndex.load(std::memory_order_relaxed);
        const std::size_t nextWriteIndex = (writeIndex + 1) & _mask;

        if (nextWriteIndex == _readIndex.load(std::memory_order_acquire))
        {
            return false; //Buffer is full
        }

        // OPTION 2:
        // new (&_buffer[writeIndex]) T(std::move(data));
        // OPTION 3 (ACTIVE) => Need move constructor()
        std::construct_at(&_buffer[writeIndex], std::move(data));

        _writeIndex.store(nextWriteIndex, std::memory_order_release);

        return true;
    }

    bool pop(T& data)
    {
        const std::size_t readIndex = _readIndex.load(std::memory_order_relaxed);

        if (readIndex == _writeIndex.load(std::memory_order_acquire))
        {
            return false; //Buffer is empty
        }

        // Common for OPTION 1, 2 & 3:
        data = std::move(_buffer[readIndex]);

        // OPTION 2:
        // _buffer[readIndex].~T();

        // OPTION 3 (ACTIVE): 
        std::destroy_at(&_buffer[readIndex]);

        const std::size_t nextReadIndex = (readIndex + 1) & _mask;
        _readIndex.store(nextReadIndex, std::memory_order_release);

        return true;
    }
};
