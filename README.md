# uthreads

A user-level threading library written in C.

## Overview

`uthreads` is a lightweight threading library that provides user-level thread management for C programs. It allows the creation, synchronization, and management of multiple threads within a single process, without relying on kernel-level thread support by leveraging the Linux ucontexts

## Features

- Lightweight user-level context switching
- Thread creation and termination
- Multiple thread scheduling algorithms like PSJF and MLFQ to cater to different workloads
- Support for thread synchronization primitives via mutex locks
- Easy customization of timer interrupt intervals

## Getting Started

### Prerequisites

- GCC or any C99-compliant compiler
- GNU Make

### Building

1. (Optional) Edit defines at the top of `thread-worker.h`. Includes options like changing the number of priority levesl in MLFQ or changing the time Quantum
2. Navigate to the root of the project and run `make SCHED=<schduler type>`, where `scheduler type` can be `PSJF` of `MLFQ`. This will build the `thread-worker.o` executable and `libthread-worker.a` library

### Usage

1. Include the `thread-worker.h` header in your C project
2. Link to the library
3. And now you can run multi-threaded code by using the following API

## API

- `int worker_create(worker_t *thread, pthread_attr_t *attr, void *(*function)(void *), void *arg);`  
  Create a new thread.

- `int worker_yield();`  
  Yield execution to another thread.

- `void worker_exit(void *value_ptr);`  
  Exit the current thread.

- `int worker_join(worker_t thread, void **value_ptr);`  
  Wait for a thread to finish.

- `int worker_mutex_init(worker_mutex_t *mutex, const pthread_mutexattr_t *attr);`  
  Initialize a mutex.

- `int worker_mutex_lock(worker_mutex_t *mutex);`  
  Lock a mutex.

- `int worker_mutex_unlock(worker_mutex_t *mutex);`  
  Unlock a mutex.

- `int worker_mutex_destroy(worker_mutex_t *mutex);`  
  Destroy a mutex.

## Example

Here is a very simple example. More examples can be found in the `benchmarks` directory.

```c
#include "[path to directory]/thread-worker.h"
#include <stdio.h>

void thread_func(void* arg) {
    printf("Hello from thread %d\n", *(int*)arg);
    uthread_exit();
}

int main() {
    uthread_init();
    int arg = 1;
    pthread_create(thread_func, &arg);
    pthread_run();
    return 0;
}
```

## Contributors
- [Arjun Patel](https://github.com/arjunUpatel)
- [Ivan Wang](https://github.com/ivanwang123)

