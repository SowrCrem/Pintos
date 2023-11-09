#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include <debug.h>
#include <list.h>
#include "threads/synch.h"

#define ERROR -1
#define RUNNING 1
#define SUCCESS 0

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (vosid);

struct rs_manager 
    {
        struct rs_manager *parent_rs_manager; /* Points to thread's parent_rs_manager */
        struct list children;    /* List of all child rs_managers */
        struct list_elem child_elem; /* List elem for child rs_manager */ 

        struct thread *thread;  /* Points to actual thread */
        
        int exit_status;         /* Exit status of the thread */
        struct semaphore wait_sema; /* Semaphore to indicate termination of thread */
    };

#endif /* userprog/process.h */
