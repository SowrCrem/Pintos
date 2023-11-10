#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"
#include <debug.h>
#include <list.h>

/* Wasn't compiling without this typedef from thread.h */
typedef int tid_t;

/* Process exit status codes. */
#define ERROR -1                            /* Process exited in error. */
#define SUCCESS 0                           /* Process exited normally. */
#define RUNNING 1                           /* Process is running. */

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (vosid);


struct rs_manager 
{
    struct rs_manager *parent_rs_manager;   /* Pointer to thread's parent rs_manager. */
    struct list children;                   /* List of all child rs_manager. */
    struct list_elem child_elem;            /* List elem for children list.  */ 

    struct thread *thread;                  /* Pointer to actual thread. */
    
    int exit_status;                        /* Exit status of THREAD. */
    struct semaphore wait_sema;             /* Semaphore for waiting on THREAD. */
};

#endif /* userprog/process.h */
