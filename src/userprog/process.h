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
void rs_manager_init (struct rs_manager *, struct thread *);
struct rs_manager* get_child (struct thread *parent, tid_t tid);

/* A relationship manager for user processes. 

   Each user process has an rs_manager, used to store its children
   and its exit status, and also some synchronization primitives. */
struct rs_manager 
{
    struct rs_manager *parent_rs_manager;   /* Pointer to parent rs_manager. */
    struct list children;                   /* List of all child rs_manager. */
    struct list_elem child_elem;            /* List elem for children list.  */ 

    struct thread *thread;                  /* Pointer to actual process. */
    
    struct semaphore child_exit_sema;       /* Semaphore for process exit. */
    struct semaphore child_load_sema;       /* Semaphore for process load. */

    bool load_success;                      /* Boolean for load status. */
    bool error;                             /* Boolean for exit error status. */
    int exit_status;                        /* Exit status of process. */
};

#endif /* userprog/process.h */
