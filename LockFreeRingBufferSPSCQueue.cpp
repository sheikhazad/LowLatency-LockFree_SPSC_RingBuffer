// LockFreeRingBufferSPSCQueue.cpp
#include "LockFreeRingBufferSPSCQueue.hpp"
#include<iostream>
#include<thread>
#include<vector>
//#include<immintrin.h> // for _mm_pause

using namespace std;

static constexpr size_t NUM_ITEMS{1'000'000};

int main(int argc, char* argv[]){

    cout<< "Starting LockFreeRingBufferSPSCQueue test with " << NUM_ITEMS << " items..." << endl;

    LockFreeRingBufferSPSCQueue<size_t> rb(1024); // Capacity must be a power of 2

    std::atomic<bool> producerDone{false};
    std::atomic<bool>consumerDone{false};
    
    vector<size_t> consumedData_vec;

    ///// Producer thread/////////
    std::thread producer([&]{
        for(size_t i{0}; i < NUM_ITEMS; ++i){
            while(!rb.push(i)){
                // Buffer is full, spin‑wait and try again
                //_mm_pause(); // Hint to the CPU that we're in a spin‑wait loop
                this_thread::yield();
            }//while() 
        }// for()
        producerDone.store(true,std::memory_order_release);     
    });
    ///// Consumer thread/////////
    std::thread consumer([&]{
        size_t data;
        while(true){
            if(rb.pop(data)){
                consumedData_vec.push_back(data);
            }
            else{
                // Buffer is empty, spin‑wait and try again
                //_mm_pause(); // Hint to the CPU that we're in a spin‑wait loop
                this_thread::yield();
                if(producerDone.load(memory_order_acquire)){
                    // If producer is done and buffer is empty, we can exit
                    break;
                }
            }
        }//while()
        consumerDone.store(true,memory_order_release);  
    });

    producer.join();
    consumer.join();

    if(consumedData_vec.size() != NUM_ITEMS){
        cerr << "Error: Expected " << NUM_ITEMS << " items, but consumed " << consumedData_vec.size() << " items." << endl;
        return 1;
    }   
    for(size_t i{0}; i < NUM_ITEMS; ++i){
        if(consumedData_vec[i] != i){
            cerr << "Error: Out of order at index " << i <<" Expected : " << i << " but got " << consumedData_vec[i] << endl;
            return 1;
        }
    }
    cout << "Test passed successfully! Number of items produced and consumed: " << consumedData_vec.size() << endl;
    return 0;
}
