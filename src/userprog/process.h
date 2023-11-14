#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"
#include <debug.h>
#include <list.h>

/* Wasn't compiling without this typedef from thread.h - ASK UTA ! */
typedef int tid_t;

/* Process exit status codes. */
#define ERROR -1                            /* Process exited in error. */
#define SUCCESS 0                           /* Process exited normally. */
#define NOT_EXITED 1                        /* Process has not exited. */

tid_t process_execute (const char *);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

/* A relationship manager for user processes. 

   Each user process has an rs_manager, used to store its children
   and its exit status, and also some synchronization primitives. */
struct process
{
    struct thread *thread;           /* Pointer to actual THREAD */

    struct process *parent_process;  /* Pointer to thread's parent process */
    struct list children;            /* List of child processes */
    struct list_elem child_elem;    /* List elem for child processes */

    bool loaded;                     /* Boolean for PROCESS loading executable */
    struct semaphore load_sema;      /* Control parent process when child loads exectuable */

    bool exited;                     /* Boolean for THREAD exit status */
    struct semaphore exit_sema;      /* Control parent process when child is exiting */
    int exit_status;                 /* Exit Status of the THREAD */

    struct hash *file_table;         /* Hash Table for mapping files owned by process to fd */
    int fd_current;                  /* Current fd value of the executable file */

};

#endif /* userprog/process.h */
