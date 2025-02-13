# Utils API Documentation

## Overview
The Utils library provides essential utility functions and classes that support the core functionality of the Signals framework. These utilities include logging, time management, scheduling, and various helper functions.

## Key Components

### Logging
- **Log**: Centralized logging system
- **LogSink**: Interface for log output
```cpp
// Example: Logging usage
Log::info("Starting application...");
Log::error("An error occurred");
```

### Time Management
- **Clock**: Abstract clock interface
- **SystemClock**: Real-time system clock
- **Scheduler**: Task scheduling based on time
```cpp
// Example: Using SystemClock
auto clock = std::make_shared<SystemClock>();
auto now = clock->now();
```

### Data Structures
- **SmallMap**: Lightweight map implementation
- **FIFO**: Lock-free FIFO queue
```cpp
// Example: Using SmallMap
SmallMap<int, std::string> map;
map[1] = "one";
map[2] = "two";
```

### Profiling
- **Profiler**: Performance profiling tools
```cpp
// Example: Profiling a function
Profiler profiler("FunctionName");
profiler.start();
// Function code
profiler.stop();
```

### Helper Functions
- **Tools**: General-purpose utility functions
- **Format**: String formatting utilities
```cpp
// Example: Using Tools
auto formatted = format("Value: %d", 42);
```

### Threading
- **Scheduler**: Task scheduling and execution
- **QueueLockFree**: Lock-free queue for thread-safe data exchange
```cpp
// Example: Using Scheduler
Scheduler scheduler;
scheduler.schedule([]() {
    // Task code
}, 1000); // Schedule task to run after 1000ms
```

## Best Practices

### Logging
- Use centralized logging for consistency
- Implement custom LogSink for specific output needs

### Time Management
- Use SystemClock for real-time applications
- Leverage Scheduler for timed tasks

### Data Structures
- Use SmallMap for small, fast maps
- Use FIFO for lock-free data exchange

### Profiling
- Profile performance-critical sections
- Use Profiler to identify bottlenecks

### Helper Functions
- Utilize Tools for common tasks
- Use Format for safe string formatting

## Example Workflow
```cpp
// Example: Comprehensive usage
#include "lib_utils/log.hpp"
#include "lib_utils/clock.hpp"
#include "lib_utils/scheduler.hpp"
#include "lib_utils/tools.hpp"

int main() {
    Log::info("Application started");

    auto clock = std::make_shared<SystemClock>();
    Scheduler scheduler;

    scheduler.schedule([]() {
        Log::info("Task executed");
    }, 1000);

    auto formatted = format("Current time: %lld", clock->now());
    Log::info(formatted);

    return 0;
}
```