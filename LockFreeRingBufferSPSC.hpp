//LockFreeRingBufferSPSC.hpp

#pragma once
#include<atomic>
#include<new>
#include<stdexcept> // for std::invalid_argument
#include<cstddef> // for size_t
#include<bit>  //For bit_ceil();
#include <memory> //For unique_ptr

//using namespace std; in a header is dangerous because it pollutes the namespace of every file that includes this header.
//using namespace std; 
//Also using for same reason above:
//using std::atomic;

template<typename T>
class LockFreeRingBufferSPSC
{

  private:
    //T* _buffer;
    std::unique_ptr<T[]> _buffer;
    std::size_t _capacity;
    std::size_t _mask;

    alignas(std::hardware_destructive_interference_size) std::atomic<size_t> _writeIndex{0};
    alignas(std::hardware_destructive_interference_size) std::atomic<size_t> _readIndex{0};

    uint32_t roundUpToNextPow2(uint32_t x)                                                                                                                                                         
    {
      if (x == 0) return 1;
      
      x--; //To handle if already power of 2, after shifting all bits ++x below make power of 2 [eg 31 to 32]
      
      //Fill all bits below the highest 1‑bit. E.g. 10010 (18) to 11111 (31)
      x |= x >> 1;
      x |= x >> 2;
      x |= x >> 4;
      x |= x >> 8;
      x |= x >> 16;
      x++; //Make .. 7 to 8 or 15 to 16 or 31 to 32.
      return x;
}


  public:
    explicit LockFreeRingBufferSPSC(size_t capacity)
    {
        if(capacity == 0) {
            throw std::invalid_argument("Capacity can't be 0");
        } 

        _capacity = capacity;
        // We require capacity to be a power of two so that:
        // (index & mask_) == (index % capacity)
        // This allows extremely fast wrap‑around using bitwise AND.
        if( (_capacity & (_capacity-1) ) != 0) 
        {
            //round up to next power of 2
            //_capacity = roundUpToNextPow2(_capacity);
            _capacity = bit_ceil(_capacity);
        } 
      
        _mask = _capacity - 1;
        //_buffer = new T[_capacity];
        _buffer = std::make_unique<T[]>(_capacity);

    }

    /* not required for unique_ptr
    ~LockFreeRingBufferSPSC(){
        delete[] _buffer;
    }*/
    //Destructor not required for unique_ptr
    //~LockFreeRingBufferSPSC() = default; // or remove entirely

    LockFreeRingBufferSPSC(const LockFreeRingBufferSPSC&) = delete;
    LockFreeRingBufferSPSC& operator=(const LockFreeRingBufferSPSC&) = delete;
    LockFreeRingBufferSPSC(LockFreeRingBufferSPSC&&) = delete;
    LockFreeRingBufferSPSC& operator=( LockFreeRingBufferSPSC&&) = delete;

    bool push(const T& data)
    {
        // Relaxed is safe because ONLY the producer thread writes to writeIndex_.
        const std::size_t writeIndex = _writeIndex.load(std::memory_order_relaxed);

        // Compute the next write index with wrap‑around.
        // This is where the producer *would* write next.
        const std::size_t nextWriteIndex = (writeIndex+1) & _mask;

        // FULL CONDITION:
        // We intentionally leave one slot unused so that:
        //     readIndex == writeIndex       → queue is empty
        //     nextWriteIndex == readIndex   → queue is full
        //
        // This avoids ambiguity without needing extra state.
        
        // Acquire ensures we see the consumer's latest readIndex_ update.
        if(nextWriteIndex == _readIndex.load(std::memory_order_acquire)){
            return false; //Buffer is full
        }
        
        //Moving from a const object is equivalent to copying.
        //If we want true move semantics, add an overload:
        //bool push(T&& data) with same implementation as push(const T& data) except _buffer[writeIndex] = std::move(data);
        //_buffer[writeIndex] = std::move(data);  ==> Wrong here for push(const T& data)
        _buffer[writeIndex] = data;
        
        //Release ensures the _buffer write (anything above this line) happens‑before the consumer sees writeIndex_.
        _writeIndex.store(nextWriteIndex,std::memory_order_release);

        return true;
    }

    bool pop(T& data)
    {
        // Relaxed is safe because ONLY the consumer thread writes to readIndex_.
        const std::size_t readIndex = _readIndex.load(std::memory_order_relaxed);

        // EMPTY CONDITION:
        // If readIndex == writeIndex, there is no data to consume.
        
        // Acquire ensures we see the producer's buffer writes before reading buffer_[readIndex].
        if(readIndex == _writeIndex.load(std::memory_order_acquire)){
            return false; //Buffer is empty
        }

        // Safe because only the consumer reads from this slot.
        //Can be moved as _buffer[readIndex] will not be read again and will be overwritten by push()
        data = std::move(_buffer[readIndex]);
        
        // Compute the next read index with wrap‑around.
        const std::size_t nextReadIndex = (readIndex+1) & _mask;
        
        // Release ensures the consumer's read is visible before the producer checks readIndex_.
        _readIndex.store(nextReadIndex, std::memory_order_release);
        return true;
    }
};
