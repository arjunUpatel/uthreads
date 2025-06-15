#ifndef WORKER_T_H
#define WORKER_T_H

#define _GNU_SOURCE

/* To use Linux pthread Library in Benchmark, comment the USE_WORKERS macro */
#define USE_WORKERS 1

// thread states
#define READY 0
#define SCHEDULED 1
#define BLOCKED 2

// thread stack size
#define STKSZ SIGSTKSZ

// context switch time quantum
#define QUANTUM 1
#define PRIORITY_LEVELS 4

/* include lib header files that you need here: */
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <string.h>
#include <sys/time.h>

typedef unsigned int worker_t;

typedef struct TCB tcb;
struct TCB
{
  /* add important states in a thread control block */
  worker_t tid;
  int status;
  int priority;
  ucontext_t *uctx;
  unsigned int elapsed;
  struct timeval schedule_time;
  struct timeval arrival_time;
  struct itimerval remaining_time;
  tcb *tcb_waitee;
  void *ret;
};

typedef struct ListNode ListNode;
struct ListNode
{
  tcb *data;
  ListNode *next; // pointer to next node in linked list
};

typedef struct Queue
{
  ListNode *front; // front (head) of the queue
  ListNode *back;  // back (tail) of the queue
} Queue;

/* mutex struct definition */
typedef struct worker_mutex_t
{
  bool initialized;
  bool locked;
  tcb *holder;
  Queue *wait_queue;
} worker_mutex_t;

typedef struct MLFQueue
{
  struct Queue **queues;
  int size;
} MLFQueue;

/* create a new thread */
int worker_create(worker_t *thread, pthread_attr_t *attr, void *(*function)(void *), void *arg);

/* give CPU pocession to other user level worker threads voluntarily */
int worker_yield();

/* terminate a thread */
void worker_exit(void *value_ptr);

/* wait for thread termination */
int worker_join(worker_t thread, void **value_ptr);

/* initial the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex, const pthread_mutexattr_t
                                                 *mutexattr);

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex);

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex);

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex);

/* Function to print global statistics. Do not modify this function.*/
void print_app_stats(void);

// tcb queue api
void enqueue(Queue *queue, tcb *data);
tcb *dequeue(Queue *queue);
tcb *rem_elem(Queue *queue, worker_t target_tid);
tcb *find_min_elapsed(Queue *queue);
tcb *rem_min_elapsed(Queue *queue);

#ifdef USE_WORKERS
#define pthread_t worker_t
#define pthread_mutex_t worker_mutex_t
#define pthread_create worker_create
#define pthread_exit worker_exit
#define pthread_join worker_join
#define pthread_mutex_init worker_mutex_init
#define pthread_mutex_lock worker_mutex_lock
#define pthread_mutex_unlock worker_mutex_unlock
#define pthread_mutex_destroy worker_mutex_destroy
#endif

#endif

