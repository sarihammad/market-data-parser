# Zero-Copy Market Data Parser

C++20 NASDAQ ITCH 5.0 parser with zero-copy parsing, an MPMC queue, and asynchronous logging. Focused on predictable latency and clean, inspectable code.

## Scope

- ITCH 5.0 message parsing with packed wire structs
- Lock-free MPMC queue for handoff
- Async logger with MMAP / O_DIRECT / buffered modes
- System utilities for CPU pinning, scheduling, and memory locking

## Architecture

- `include/itch_parser.hpp`: zero-copy parsing on mapped buffers
- `include/mpmc_queue.hpp`: bounded, cache-line aligned ring with sequence numbers
- `include/async_logger.hpp`: background writer thread and aligned buffers
- `include/system_utils.hpp`: affinity, priority, TSC helpers

## Build

Prereqs: C++20 compiler, CMake 3.20+, Linux for system-level features.

```bash
./build.sh

# Or manually
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

## Test

```bash
cd build
./parser_test
```

## Demo

```bash
cd build
./parser_demo
```

## Benchmark

```bash
cd build
./parser_benchmark
./parser_benchmark 50000000
```

## Usage

```cpp
#include "itch_parser.hpp"
#include "async_logger.hpp"
#include "system_utils.hpp"

using namespace fast_market;

int main() {
    ScopedCPUPin pin(0);

    ITCHParser parser;
    AsyncLogger logger("output.bin", AsyncLogger::WriteMode::MMAP);
    logger.start();

    while (receiving_data) {
        uint8_t* data = get_next_message();
        size_t size = get_message_size();

        if (auto parsed = parser.parse(data, size)) {
            logger.log(*parsed);
            if (parsed->type == MessageType::ADD_ORDER) {
                handle_add_order(parsed->add_order);
            }
        }
    }

    logger.stop();
    return 0;
}
```

## References

- NASDAQ ITCH Specification
- Intel 64 and IA-32 Architectures Optimization Reference Manual
- Linux kernel documentation: CPU isolation
- C++20 standard library reference

## License

MIT. See `LICENSE`.
