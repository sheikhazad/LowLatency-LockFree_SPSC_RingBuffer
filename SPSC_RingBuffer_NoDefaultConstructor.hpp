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
     * std::unique_ptr<T[]> _t_buffer;
     *
     * _t_buffer = std::make_unique<T[]>(_capacity);
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
     * T* _t_buffer;
     *
     * new (&_t_buffer[idx]) T(value);
     * _t_buffer[idx].~T();
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

    T* _t_buffer{nullptr};

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
        //Both below needs T()
        //_t_buffer = new T[_capacity]; => Raw without unique_ptr
        //_t_buffer = std::make_unique<T[]>(_capacity);

        // OPTION 2 & 3:
        //std::align_val_t(alignof(T)) guarantees that the start address of the
        //allocated memory block (_buffer) is properly aligned for objects of type T.
        //It does NOT guarantee that every object stored in the buffer starts on a
        //cache-line boundary.
        //Individual objects are cache-line aligned only if T itself has cache-line
        //alignment requirements, for example:
        //struct alignas(64) T { ... };
        //In that case alignof(T) == 64 and each element in the array will also be
        //64-byte aligned   

         //Give me at least 'size' bytes whose starting address is aligned to 8 bytes(say alignof(T) = 8).
        _t_buffer = static_cast<T*>(::operator new(sizeof(T) * _capacity,
                                    std::align_val_t(alignof(T)) ) );
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
            // _t_buffer[read].~T();

            // OPTION 3 (ACTIVE)
            std::destroy_at(&_t_buffer[read]);

            read = (read + 1) & _mask;
        }

        ::operator delete(_t_buffer, std::align_val_t(alignof(T)));
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
        // _t_buffer[writeIndex] = data;

        // OPTION 2:
        // new (&_t_buffer[writeIndex]) T(data);

        // OPTION 3 (ACTIVE) => Need copy constructor()
        std::construct_at(&_t_buffer[writeIndex], data);

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
        // new (&_t_buffer[writeIndex]) T(std::move(data));
        // OPTION 3 (ACTIVE) => Need move constructor()
        std::construct_at(&_t_buffer[writeIndex], std::move(data));

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
        data = std::move(_t_buffer[readIndex]);

        // OPTION 2:
        // _t_buffer[readIndex].~T();

        // OPTION 3 (ACTIVE): 
        std::destroy_at(&_t_buffer[readIndex]);

        const std::size_t nextReadIndex = (readIndex + 1) & _mask;
        _readIndex.store(nextReadIndex, std::memory_order_release);

        return true;
    }
};
