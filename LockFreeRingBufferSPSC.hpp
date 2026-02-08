//LockFreeRingBufferSPSC.hpp

#pragma once
#include<atomic>
#include<new>
#include<stdexcept> // for std::invalid_argument
#include<cstddef> // for size_t

using namespace std;

template<typename T>
class LockFreeRingBufferSPSC
{
    public:
        explicit LockFreeRingBufferSPSC(size_t capacity):_capacity(capacity){

                // We require capacity to be a power of two so that:
                //     (index & mask_) == (index % capacity)
                // This allows extremely fast wrap‑around using bitwise AND.
                if(capacity == 0 || (capacity & (capacity-1)) != 0) {
                    throw invalid_argument("Capacity must be a power of 2 and greater than 0");
                }              
                _mask = capacity - 1;
                _buffer = new T[capacity];
            }

        ~LockFreeRingBufferSPSC(){
            delete[] _buffer;
        }

        LockFreeRingBufferSPSC(const LockFreeRingBufferSPSC&) = delete;
        LockFreeRingBufferSPSC& operator=(const LockFreeRingBufferSPSC&) = delete;
        LockFreeRingBufferSPSC(LockFreeRingBufferSPSC&&) = delete;
        LockFreeRingBufferSPSC& operator=( LockFreeRingBufferSPSC&&) = delete;

        bool push(const T& data){
            // Relaxed is safe because ONLY the producer thread writes to writeIndex_.
            const size_t writeIndex = _writeIndex.load(std::memory_order_relaxed);

            // Compute the next write index with wrap‑around.
            // This is where the producer *would* write next.
            const size_t nextWriteIndex = (writeIndex+1) & _mask;

            // FULL CONDITION:
            // We intentionally leave one slot unused so that:
            //     readIndex == writeIndex       → queue is empty
            //     nextWriteIndex == readIndex   → queue is full
            //
            // This avoids ambiguity without needing extra state.
            //
            // Acquire ensures we see the consumer's latest readIndex_ update.
            if(nextWriteIndex == _readIndex.load(std::memory_order_acquire)){
                return false; //Buffer is full
            }
            // Safe because only the producer writes to this slot.
            _buffer[writeIndex] = data;
            //Release ensures the _buffer write (anything above this line) happens‑before the consumer sees writeIndex_.
            _writeIndex.store(nextWriteIndex,std::memory_order_release);

            return true;
        }

        bool pop(T& data){
            // Relaxed is safe because ONLY the consumer thread writes to readIndex_.
            const size_t readIndex = _readIndex.load(std::memory_order_relaxed);

            // EMPTY CONDITION:
            // If readIndex == writeIndex, there is no data to consume.
            //
            // Acquire ensures we see the producer's buffer writes before reading buffer_[readIndex].
            if(readIndex == _writeIndex.load(std::memory_order_acquire)){
                return false; //Buffer is empty
            }

            // Safe because only the consumer reads from this slot.
            data = _buffer[readIndex];
            // Compute the next read index with wrap‑around.
            const size_t nextReadIndex = (readIndex+1) & _mask;
            // Release ensures the consumer's read is visible before the producer checks readIndex_.
            _readIndex.store(nextReadIndex, std::memory_order_release);
            return true;
        }

    private:

        T* _buffer;
        size_t _capacity;
        size_t _mask;

        alignas(/*std::hardware_destructive_interference_size*/ 64) atomic<size_t> _writeIndex{0};
        alignas(/*std::hardware_destructive_interference_size*/ 64) atomic<size_t> _readIndex{0};
};
