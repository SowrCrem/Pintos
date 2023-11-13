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


#endif /* userprog/process.h */
