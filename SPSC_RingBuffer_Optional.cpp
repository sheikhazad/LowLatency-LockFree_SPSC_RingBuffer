//SPSC_RingBuffer_Optional.cpp
#include <iostream>
#include <string>
#include <optional>   // <-- REQUIRED for std::optional

#include<vector>
#include<thread>
#include<atomic>
#include<new>
#include<stdexcept> // for std::invalid_argument
#include<cstddef> // for size_t
#include<bit>  //For bit_ceil();
#include <memory> //For unique_ptr
#include <cassert> //For assert in tests

//using namespace std; in a header is dangerous because it pollutes the namespace of every file that includes this header.
using namespace std; 
//Also using for same reason above:
//using std::atomic;

template<typename T>
class LockFreeRingBufferSPSCQueue
{

  private:
    //T* _buffer;                    //Need default constructor for T
    // std::unique_ptr<T[]> _buffer; //Need default constructor for T
    //1. Modification to allow without default constructor for T
    std::unique_ptr<std::optional<T>[]> _buffer;  // optional<T>  No need for default constructor for T, but adds some overhead due to extra bool flag in optional<T>

    std::size_t _capacity;
    std::size_t _mask;

    alignas(64) std::atomic<size_t> _writeIndex{0};
    alignas(64) std::atomic<size_t> _readIndex{0};

    //My own bit_ceil()                                                                                    
    uint32_t roundUpToNextPow2(uint32_t x)                                                                                                                                                         
    {
      if (x == 0) return 1;
      //x = 18 = 10010
      //x-- = 17 = 10001
      x--; //If x is already a power of two, keep it unchanged after the final increment [eg 31 to 32]
           
      
      //Fill all bits below the highest 1‑bit. E.g. 10010 (18) to 11111 (31)
      x |= x >> 1;  //11001
      x |= x >> 2;  //11111
      x |= x >> 4;  //11111
      x |= x >> 8;  //11111
      x |= x >> 16; //11111 = 31

      x++; //Make .. 7 to 8 or 15 to 16 or 31 to 32.
      //x++ = 32
      return x;
}


  public:
    explicit LockFreeRingBufferSPSCQueue(size_t capacity)
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
        //_buffer = std::make_unique<T[]>(_capacity);
        //2. Modification to allow without default constructor for T
        _buffer = std::make_unique<std::optional<T>[]>(_capacity);  // optional array starts empty

    }

    /* not required for unique_ptr
    ~LockFreeRingBufferSPSC(){
        delete[] _buffer;
    }*/
    //Destructor not required for unique_ptr
    //~LockFreeRingBufferSPSC() = default; // or remove entirely

    LockFreeRingBufferSPSCQueue(const LockFreeRingBufferSPSCQueue&) = delete;
    LockFreeRingBufferSPSCQueue& operator=(const LockFreeRingBufferSPSCQueue&) = delete;
    LockFreeRingBufferSPSCQueue(LockFreeRingBufferSPSCQueue&&) = delete;
    LockFreeRingBufferSPSCQueue& operator=(LockFreeRingBufferSPSCQueue&&) = delete;

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
       // _buffer[writeIndex] = data;
       ////3. Modification to allow without default constructor for T
        _buffer[writeIndex].emplace(data);  // construct T in-place
        
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
        //data = std::move(_buffer[readIndex]);
        //4. Modification to allow without default constructor for T
        data = std::move(_buffer[readIndex].value());  // extract T
        _buffer[readIndex].reset();                    // destroy T and mark slot empty
        
        // Compute the next read index with wrap‑around.
        const std::size_t nextReadIndex = (readIndex+1) & _mask;
        
        // Release ensures the consumer's read is visible before the producer checks readIndex_.
        _readIndex.store(nextReadIndex, std::memory_order_release);
        return true;
    }
};
class TestObject {
public:
    int id;
    std::string payload;

   // TestObject() : id(0), payload("") {}

    TestObject(int i, std::string p)
        : id(i), payload(std::move(p)) {}

    TestObject(const TestObject& other)
        : id(other.id), payload(other.payload)
    {
        // std::cout << "Copy ctor\n";
    }

    TestObject(TestObject&& other) noexcept
        : id(other.id), payload(std::move(other.payload))
    {
        // std::cout << "Move ctor\n";
    }

    TestObject& operator=(const TestObject& other)
    {
        id = other.id;
        payload = other.payload;
        return *this;
    }

    TestObject& operator=(TestObject&& other) noexcept
    {
        id = other.id;
        payload = std::move(other.payload);
        return *this;
    }
};


void test_single_thread()
{
    LockFreeRingBufferSPSCQueue<TestObject> q(8);

    for (int i = 0; i < 5; ++i) {
        bool ok = q.push(TestObject(i, "obj_" + std::to_string(i)));
        assert(ok);
    }

    for (int i = 0; i < 5; ++i) {
        TestObject out(0, "");
        bool ok = q.pop(out);
        assert(ok);
        assert(out.id == i);
        assert(out.payload == "obj_" + std::to_string(i));
    }

    std::cout << "[OK] Single-thread TestObject test passed\n";
}


void test_spsc_multithread()
{
    constexpr size_t N = 200000;
    LockFreeRingBufferSPSCQueue<TestObject> q(1024);

    std::atomic<bool> producerDone{false};
    std::vector<TestObject> consumed;
    consumed.reserve(N);

    std::thread producer([&]() {
        for (size_t i = 0; i < N; ++i) {
            TestObject obj(i, "payload_" + std::to_string(i));
            while (!q.push(obj)) {
                // queue full — spin
            }
        }
        producerDone.store(true, std::memory_order_release);
    });

    std::thread consumer([&]() {
        TestObject out(0, "");
        while (true) {
            while (q.pop(out)) {
                consumed.push_back(std::move(out));
            }
            if (producerDone.load(std::memory_order_acquire) &&
                !q.pop(out)) {
                break;
            }
        }
    });

    producer.join();
    consumer.join();

    assert(consumed.size() == N);

    for (size_t i = 0; i < N; ++i) {
        assert(consumed[i].id == i);
        assert(consumed[i].payload == "payload_" + std::to_string(i));
    }

    std::cout << "[OK] Multi-thread TestObject test passed\n";
}

int main()
{
    test_single_thread();
    test_spsc_multithread();

    std::cout << "All TestObject tests passed successfully\n";
    return 0;
}
