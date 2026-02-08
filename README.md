It's lock free Ultra Low-latency SPSC Ring (circular) buffer.

This SPSC queue works super fast because:
------------------------------------------
-Only the producer writes writeIndex
-Only the consumer writes readIndex
-No two threads ever modify the same variable
-No CAS, no locks, no contention
-This is why the memory ordering is simple and fast.

A ring buffer is naturally FIFO(Queue), not LIFO(Stack)
------------------------------------------------------
A ring buffer is defined by:

-a fixed‑size circular array
-a read index
-a write index
-wrap‑around using index & mask (or modulo)

This structure inherently supports FIFO:
producer writes at writeIndex
consumer reads at readIndex
both move forward monotonically
That is exactly what a queue is.
