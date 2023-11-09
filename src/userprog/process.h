#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include <debug.h>
#include <list.h>
#include "threads/synch.h"

#define ERROR -1
#define RUNNING 1
#define OKAY 0

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

struct rs_manager 
    {
        /* TODO: Point to actual parent  */
        struct thread *parent;   /* Point to parent thread    */ 
        /* struct thread *thread*/  /* Points to actual thread */
        struct list children;    /* List of all child threads */
        int exit_status;         /* Exit status of the thread */
        struct semaphore wait_sema; /* Semaphore to indicate termination of thread */
    };

#endif /* userprog/process.h */
