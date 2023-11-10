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
#define NOT_EXITED 1                        /* Process has not exited. */

/* Exit status definitions. */
#define SUCCESS_CODE (0)                /* Success code. */
#define ERROR_CODE (-1)                 /* Error code. */

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

/* A relationship manager for user processes. 

   Each user process has an rs_manager, used to store its children
   and its exit status. */
struct rs_manager 
{
    struct rs_manager *parent_rs_manager;   /* Pointer to thread's parent rs_manager. */
    struct list children;                   /* List of all child rs_manager. */
    struct list_elem child_elem;            /* List elem for children list.  */ 

    struct thread *thread;                  /* Pointer to actual THREAD. */
    
    bool success;                           /* Boolean for THREAD exit status. */
    int exit_status;                        /* Exit status of THREAD. */
    struct semaphore wait_sema;             /* Semaphore for waiting on THREAD. */
};

#endif /* userprog/process.h */
