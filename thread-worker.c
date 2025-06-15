#include "thread-worker.h"
#include <signal.h>
#include <time.h>

// Global counter for total context switches and
// average turn around and response time
long tot_cntx_switches = 0;
double avg_turn_time = 0;
double avg_resp_time = 0;
long tot_threads = 0;
ucontext_t uctx_main;

tcb *tcb_curr;

MLFQueue *mlfq_run_queue;
Queue *psjf_run_queue;

bool is_scheduler_initialized;
bool did_worker_exit = false;

worker_t worker_num = 0;

static void schedule();

void handle_error(char *msg)
{
  perror(msg);
  exit(EXIT_FAILURE);
}

Queue *init_queue()
{
  Queue *queue = malloc(sizeof(Queue));
  queue->back = NULL;
  queue->front = NULL;
  return queue;
}

bool is_queue_empty(Queue *queue)
{
  if (queue == NULL || queue->front == NULL)
    return true;
  return false;
}

void enqueue(Queue *queue, tcb *data)
{
  ListNode *list_node = malloc(sizeof(ListNode));
  list_node->data = data;
  list_node->next = NULL;

  if (queue->back == NULL)
    queue->front = list_node;
  else
    queue->back->next = list_node;
  queue->back = list_node;
}

tcb *dequeue(Queue *queue)
{
  if (queue->front == NULL)
    return NULL;
  ListNode *temp = queue->front;
  tcb *data = temp->data;

  queue->front = temp->next;
  if (queue->front == NULL)
    queue->back = NULL;
  free(temp);
  return data;
}

tcb *find_elem(Queue *queue, worker_t target_tid)
{
  ListNode *curr_node = queue->front;
  while (curr_node != NULL)
  {
    if (curr_node->data->tid == target_tid)
      return curr_node->data;
    curr_node = curr_node->next;
  }
  return NULL;
}

tcb *rem_elem(Queue *queue, worker_t target_tid)
{
  ListNode *prev_node = NULL;
  ListNode *curr_node = queue->front;

  while (curr_node != NULL)
  {
    if (curr_node->data->tid == target_tid)
    {
      if (prev_node != NULL)
      {
        prev_node->next = curr_node->next;
        if (curr_node->next == NULL)
          queue->back = prev_node;
      }
      else
      {
        queue->front = curr_node->next;
        if (queue->front == NULL)
          queue->back = NULL;
      }
      tcb *res = curr_node->data;
      free(curr_node);
      return res;
    }
    prev_node = curr_node;
    curr_node = curr_node->next;
  }
  return NULL;
}

tcb *find_min_elapsed(Queue *queue)
{
  if (queue->front == NULL)
    return NULL;
  ListNode *curr_node = queue->front;
  tcb *min_elapsed = NULL;
  while (curr_node != NULL)
  {
    if (min_elapsed == NULL && curr_node->data->status == READY)
      min_elapsed = curr_node->data;
    else if (curr_node->data->status == READY && curr_node->data->elapsed < min_elapsed->elapsed)
      min_elapsed = curr_node->data;
    curr_node = curr_node->next;
  }
  return min_elapsed;
}

tcb *rem_min_elapsed(Queue *queue)
{
  tcb *tcb_target = find_min_elapsed(queue);
  if (tcb_target == NULL)
    return NULL;
  return rem_elem(queue, tcb_target->tid);
}

void print_queue(Queue *queue)
{
  ListNode *curr = queue->front;
  while (curr != NULL)
  {
    printf("(%d,%d)->", curr->data->tid, curr->data->status);
    curr = curr->next;
  }
  printf("\n");
}

MLFQueue *init_mlfq(int size)
{
  MLFQueue *mlfq = malloc(sizeof(MLFQueue));

  mlfq->queues = malloc(sizeof(Queue *) * size);
  mlfq->size = size;

  for (int i = 0; i < size; i++)
  {
    mlfq->queues[i] = init_queue();
  }

  return mlfq;
}

void mlfq_enqueue(MLFQueue *mlfq, tcb *data)
{
  enqueue(mlfq->queues[data->priority], data);
}

tcb *mlfq_dequeue(MLFQueue *mlfq)
{
  for (int i = 0; i < mlfq->size; i++)
  {
    if (mlfq->queues[i]->front != NULL)
    {
      return dequeue(mlfq->queues[i]);
    }
  }

  return NULL;
}

tcb *mlfq_find_elem(MLFQueue *mlfq, worker_t tid)
{
  for (int i = 0; i < mlfq->size; i++)
  {
    tcb *tcb_ptr = find_elem(mlfq->queues[i], tid);

    if (tcb_ptr != NULL)
    {
      return tcb_ptr;
    }
  }

  return NULL;
}

ucontext_t *init_ucontext()
{
  ucontext_t *uctx = malloc(sizeof(ucontext_t));
  if (getcontext(uctx) == -1)
    printf("getcontext\n");
  uctx->uc_stack.ss_sp = malloc(STKSZ);
  uctx->uc_stack.ss_size = STKSZ;
  uctx->uc_link = NULL;
  return uctx;
}

void free_ucontext(ucontext_t *uctx)
{
  free(uctx->uc_stack.ss_sp);
  free(uctx);
}

suseconds_t int_to_suseconds(int time)
{
  return time * 1000;
}

tcb *init_tcb(worker_t tid)
{
  tcb *tcb_to_init = malloc(sizeof(tcb));
  tcb_to_init->tid = tid;
  tcb_to_init->status = READY;
  tcb_to_init->priority = 0;
  tcb_to_init->uctx = init_ucontext();
  tcb_to_init->elapsed = 0;
  gettimeofday(&tcb_to_init->arrival_time, NULL);
  gettimeofday(&tcb_to_init->schedule_time, NULL);
  tcb_to_init->tcb_waitee = NULL;

  tcb_to_init->remaining_time.it_interval.tv_sec = 0;
  tcb_to_init->remaining_time.it_interval.tv_usec = 0;
  tcb_to_init->remaining_time.it_value.tv_sec = 0;
  tcb_to_init->remaining_time.it_value.tv_usec = QUANTUM * 1000;

  return tcb_to_init;
}

void free_tcb(tcb *tcb)
{
  free_ucontext(tcb->uctx);
  free(tcb);
}

void free_queue(Queue *queue)
{
  ListNode *curr = queue->front;
  while (curr != NULL)
  {
    free_tcb(curr->data);
    ListNode *temp = curr;
    curr = curr->next;
    free(temp);
  }
  free(queue);
}

void free_mlfq(MLFQueue *mlfq)
{
  for (int i = 0; i < mlfq->size; i++)
  {
    free_queue(mlfq->queues[i]);
  }

  free(mlfq->queues);
  free(mlfq);
}

void start_timer()
{
  setitimer(ITIMER_PROF, &tcb_curr->remaining_time, NULL);
}

void stop_timer()
{
  getitimer(ITIMER_PROF, &tcb_curr->remaining_time);
  struct itimerval timer;
  timer.it_interval.tv_sec = 0;
  timer.it_value.tv_usec = 0;
  timer.it_value.tv_sec = 0;
  timer.it_value.tv_usec = 0;
  setitimer(ITIMER_PROF, &timer, NULL);
}

void set_priority()
{
  if (tcb_curr->priority < PRIORITY_LEVELS - 1)
  {
    tcb_curr->priority++;
  }
}

void handle_sigprof(int signo)
{
  tcb_curr->status = READY;
  tcb_curr->elapsed++;
  set_priority();
  stop_timer();
  tcb_curr->remaining_time.it_value.tv_usec = QUANTUM * 1000;
  if (swapcontext(tcb_curr->uctx, &uctx_main) == -1)
    printf("swapcontext\n");
}

/* give CPU possession to other user-level worker threads voluntarily */
int worker_yield()
{
  tcb_curr->status = READY;
  stop_timer();
  if (swapcontext(tcb_curr->uctx, &uctx_main) == -1)
    printf("swapcontext");
  return 0;
};

/* terminate a thread */
void worker_exit(void *value_ptr)
{
  stop_timer();
  if (tcb_curr->tcb_waitee != NULL)
  {
    tcb_curr->tcb_waitee->status = READY;
    tcb_curr->tcb_waitee = NULL;
  }

  struct timeval completion_time;
  gettimeofday(&completion_time, NULL);

  double turn_sum = avg_turn_time * tot_threads;
  double resp_sum = avg_turn_time * tot_threads;

  struct timeval turn_time, resp_time;
  timersub(&completion_time, &tcb_curr->arrival_time, &turn_time);
  timersub(&tcb_curr->schedule_time, &tcb_curr->arrival_time, &resp_time);

  turn_sum += turn_time.tv_sec * 1000 + turn_time.tv_usec / 1000.0;
  resp_sum += resp_time.tv_sec * 1000 + resp_time.tv_usec / 1000.0;

  tot_threads++;
  avg_turn_time = turn_sum / tot_threads;
  avg_resp_time = resp_sum / tot_threads;

  free_tcb(tcb_curr);
  did_worker_exit = true;
  setcontext(&uctx_main);
};

void wrapper_function(void *(*function)(void *), void *arg)
{
  worker_exit(function(arg));
}

void init_scheduler()
{
  // Initialize run queue
#ifdef MLFQ
    mlfq_run_queue = init_mlfq(PRIORITY_LEVELS);
#elif PSJF
    psjf_run_queue = init_queue();
#endif

  if (getcontext(&uctx_main) == -1)
    printf("getcontext\n");
  uctx_main.uc_link = NULL;
  uctx_main.uc_stack.ss_sp = malloc(STKSZ);
  uctx_main.uc_stack.ss_size = STKSZ;
  makecontext(&uctx_main, (void (*)())schedule, 0);

  // Set signal handler
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = &handle_sigprof;
  sigaction(SIGPROF, &sa, NULL);
  worker_t tid = worker_num++;
  tcb_curr = init_tcb(tid);
  start_timer();

  is_scheduler_initialized = true;
}

/* create a new thread */
int worker_create(worker_t *thread, pthread_attr_t *attr,
                  void *(*function)(void *), void *arg)
{
  if (!is_scheduler_initialized)
    init_scheduler();

  *thread = worker_num++;
  tcb *tcb_wrkr = init_tcb(*thread);
  makecontext(tcb_wrkr->uctx, (void (*)())wrapper_function, 2, function, arg);
  stop_timer();
#ifdef MLFQ
    mlfq_enqueue(mlfq_run_queue, tcb_wrkr);
#elif PSJF
    enqueue(psjf_run_queue, tcb_wrkr);
#endif
  start_timer();
  return 0;
};

/* Wait for thread termination */
int worker_join(worker_t thread, void **value_ptr)
{
  stop_timer();
  tcb *tcb_waiter = NULL;
#ifdef MLFQ
    tcb_waiter = mlfq_find_elem(mlfq_run_queue, thread);
#elif PSJF
    tcb_waiter = find_elem(psjf_run_queue, thread);
#endif

  if (tcb_waiter == NULL)
  {
    start_timer();
    return -1;
  }
  tcb_curr->status = BLOCKED;
  tcb_waiter->tcb_waitee = tcb_curr;
  if (swapcontext(tcb_curr->uctx, &uctx_main) == -1)
    printf("swapcontext\n");
  return 0;
};

/* initialize the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex,
                      const pthread_mutexattr_t *mutexattr)
{
  if (mutex->initialized)
    return 1;
  mutex->wait_queue = init_queue();
  mutex->holder = NULL;
  mutex->locked = false;
  mutex->initialized = true;
  return 0;
};

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex)
{
  // - use the built-in test-and-set atomic function to test the mutex
  // - if the mutex is acquired successfully, enter the critical section
  // - if acquiring mutex fails, push current thread into block list and
  // context switch to the scheduler thread

  if (!mutex->initialized)
    return 1;
  while (__atomic_test_and_set((volatile void *)&(mutex->locked), __ATOMIC_RELAXED))
  {
    tcb_curr->status = BLOCKED;
    stop_timer();
    enqueue(mutex->wait_queue, tcb_curr);
    if (swapcontext(tcb_curr->uctx, &uctx_main) == -1)
      printf("swapcontext\n");
  }
  mutex->holder = tcb_curr;
  mutex->locked = true;
  return 0;
};

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex)
{
  // - release mutex and make it available again.
  // - put threads in block list to run queue
  // so that they could compete for mutex later.

  if (!mutex->initialized || tcb_curr != mutex->holder)
    return 1;
  stop_timer();
  while (!is_queue_empty(mutex->wait_queue))
  {
    tcb *wait_tcb = dequeue(mutex->wait_queue);
    wait_tcb->status = READY;
  }
  mutex->holder = NULL;
  mutex->locked = false;
  start_timer();
  return 0;
};

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex)
{
  if (mutex->initialized == false)
    return 1;

  if (mutex->locked == true)
    return 2;
  stop_timer();
  while (!is_queue_empty(mutex->wait_queue))
  {
    tcb *wait_tcb = dequeue(mutex->wait_queue);
    wait_tcb->status = READY;
  }
  free(mutex->wait_queue);
  mutex->wait_queue = NULL;
  mutex->initialized = false;
  mutex->holder = NULL;
  start_timer();
  return 0;
};

/* Pre-emptive Shortest Job First (POLICY_PSJF) scheduling algorithm */
static void sched_psjf()
{
  tcb_curr = rem_min_elapsed(psjf_run_queue);
}

/* Preemptive MLFQ scheduling algorithm */
static void sched_mlfq()
{
  tcb_curr = mlfq_dequeue(mlfq_run_queue);
}

/* scheduler */
static void schedule()
{
  if (!did_worker_exit)
  {
#ifdef MLFQ
      mlfq_enqueue(mlfq_run_queue, tcb_curr);
#elif PSJF
      enqueue(psjf_run_queue, tcb_curr);
#endif
  }
  did_worker_exit = false;
  tot_cntx_switches++;
#ifdef PSJF
    sched_psjf();
#elif MLFQ
    sched_mlfq();
#endif

  // nothing left to schedule
  if (tcb_curr == NULL)
  {
#ifdef MLFQ
      free_mlfq(mlfq_run_queue);
#else
      free_queue(psjf_run_queue);
#endif
    exit(EXIT_SUCCESS);
  }
  if (tcb_curr->schedule_time.tv_sec == 0 && tcb_curr->schedule_time.tv_usec == 0)
  {
    gettimeofday(&tcb_curr->schedule_time, NULL);
  }
  tcb_curr->status = SCHEDULED;
  tot_cntx_switches++;

  // populate tcb time_left with apt time
  start_timer();

  if (setcontext(tcb_curr->uctx) == -1)
    printf("setcontext\n");
}

/* Function to print global statistics. Do not modify this function.*/
void print_app_stats(void)
{
  fprintf(stderr, "Total context switches %ld \n", tot_cntx_switches);
  fprintf(stderr, "Average turnaround time %lf \n", avg_turn_time);
  fprintf(stderr, "Average response time  %lf \n", avg_resp_time);
}
