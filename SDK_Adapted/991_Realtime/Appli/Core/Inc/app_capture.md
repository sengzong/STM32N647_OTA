# Latest completed camera frame

app_capture.h has one ISR producer and one NN-thread consumer. Its three aligned
input buffers are owned by capture, inference and an optional completed frame.
On each frame event, an older pending frame can be reclaimed for capture; the
inference-owned buffer cannot be reclaimed. The ready semaphore carries at most
one notification, including an ISR between the consumer taking that notification
and entering its critical section. Short PRIMASK sections preserve the prior IRQ
mask. No copying or cache maintenance runs in the ISR.

The consumer obtains an output slot before claiming input so backpressure does
not age an input while waiting for postprocessing. It invalidates input after
inference before releasing ownership. Ticks and sequence follow the completed
input, including unsigned wrap. Host tests exercise production code under normal,
overloaded and raced delivery; camera DMA timing still needs hardware validation.
