#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"
#include <debug.h>
#include <list.h>

/* Wasn't compiling withoßut this typedef from thread.h */
typedef int tid_t;


/* Global variable for filesystem lock */
struct lock filesys_lock;

/* Process exit status codes. */
#define ERROR -1                            /* Process exited in error. */
#define SUCCESS 0                           /* Process exited normally. */
#define NOT_EXITED 1                        /* Process has not exited. */

/* Exit status definitions. */
#define SUCCESS_CODE (0)                /* Success code. */
#define ERROR_CODE (-1)                 /* Error code. */


/* Process Struct */
struct process 
{
    struct process *parent_process;   /* Pointer to thread's parent process. */
    struct list children;                   /* List of all child process. */
    struct list_elem child_elem;            /* List elem for children list.  */ 

    struct thread *thread;                  /* Pointer to actual THREAD. */

    bool loaded;                            /* Boolean for PROCESS loading executable */
    struct semaphore load_sema;             /* Control parent process when child loading executable */
    
    bool success;                           /* Boolean for THREAD exit status. */
    int exit_status;                        /* Exit status of THREAD. */
    struct semaphore wait_sema;             /* Semaphore for waiting on THREAD. */
};


tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

void process_init (struct thread *t, struct process *parent);
void process_free (struct process *p);

#endif /* userprog/process.h */
