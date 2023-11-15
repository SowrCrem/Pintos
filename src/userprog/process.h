#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"
#include <debug.h>
#include <list.h>
#include <hash.h>

/* Wasn't compiling without this typedef from thread.h - ASK UTA ! */
typedef int tid_t;

/* Process exit status codes. */
#define ERROR -1                            /* Process exited in error. */
#define SUCCESS 0                           /* Process exited normally. */
#define NOT_EXITED 1                        /* Process has not exited. */

/* File descriptors start from 2. */
#define FD_START 2

struct file_entry
{
  struct hash_elem file_elem;               /* Hash elem for file_table. */
  struct file* file;                        /* Pointer to file. */
  int fd;                                   /* File identifier. */
};

/* A relationship manager for user processes. 

   Each user process has an rs_manager, used to store its children
   and its exit status, and also some synchronization primitives. */
struct rs_manager 
{
  struct rs_manager *parent_rs_manager;   /* Pointer to parent rs_manager. */
  struct list children;                   /* List of all child rs_manager. */
  struct list_elem child_elem;            /* List elem for children list.  */ 

  struct thread *thread;                  /* Pointer to actual process. */
  tid_t tid;                              /* Thread identifier. */
  
  struct hash file_table;                 /* Hash table for files. */
  struct lock file_table_lock;            /* TODO: Synchronize table accesses. */
  struct file* executing;                 /* TODO: Store deny writes for open file. */
  int fd_next;                            /* Counter for fd value. */

  struct semaphore child_load_sema;       /* Semaphore for process load. */
  bool load_success;                      /* Boolean for load status. */
  
  struct semaphore child_exit_sema;       /* Semaphore for process exit. */
  struct lock exit_lock;                  /* Lock for exiting child process. */
  bool error;                             /* Boolean for exit error status. */

  int exit_status;                        /* Exit status of process. */
};

tid_t process_execute (const char *);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
void rs_manager_init (struct rs_manager *, struct thread *);
struct rs_manager * get_child (struct thread *parent, tid_t tid);
struct file_entry * file_entry_lookup (int fd);

#endif /* userprog/process.h */
