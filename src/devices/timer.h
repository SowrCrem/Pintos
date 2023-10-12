#ifndef DEVICES_TIMER_H
#define DEVICES_TIMER_H
#include "threads/thread.h"
#include <round.h>
#include <stdint.h>
#include  "lib/kernel/list.h"
#include "threads/synch.h"


/* Struct sleep_sema as a wrap-up container for semaphore*/
struct sleep_sema {
  struct list_elem list_elem;         /* List_elem to add to list*/
  struct semaphore semaphore;         /* Semaphore */ 
  int64_t wakeup_time;                /* Timestamp for when thread should be woken up*/
};

/* Number of timer interrupts per second. */
#define TIMER_FREQ 100

void timer_init (void);
void timer_calibrate (void);
int64_t timer_ticks (void);
int64_t timer_elapsed (int64_t);

/* Sleep and yield the CPU to other threads. */
void timer_sleep (int64_t ticks);
void timer_msleep (int64_t milliseconds);
void timer_usleep (int64_t microseconds);
void timer_nsleep (int64_t nanoseconds);

/* Busy waits. */
void timer_mdelay (int64_t milliseconds);
void timer_udelay (int64_t microseconds);
void timer_ndelay (int64_t nanoseconds);

void timer_print_stats (void);

/* Comparison function to order list */
bool thread_wakeup_less(const struct list_elem *, const struct list_elem *, void *aux);

#endif /* devices/timer.h */
