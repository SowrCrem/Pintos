#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"
#include <debug.h>
#include <list.h>
#include <hash.h>
#include "threads/synch.h"

/* Wasn't compiling without this typedef from thread.h - ASK UTA ! */
typedef int tid_t;

/* Process exit status codes. */
#define ERROR -1                            /* Process exited in error. */
#define SUCCESS 0                           /* Process exited normally. */
#define NOT_EXITED 1                        /* Process has not exited. */

struct lock filesys_lock;  /* TODO: Remove and make fine grained */

tid_t process_execute (const char *);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

/* Computes and returns the hash value for hash element E, given
   auxiliary data AUX. */
unsigned file_hash_func (const struct hash_elem *e, void *aux);

/* Compares the value of two hash elements A and B, given
   auxiliary data AUX.  Returns true if A is less than B, or
   false if A is greater than or equal to B. */
bool file_less_func (const struct hash_elem *a,
                             const struct hash_elem *b,
                             void *aux);

struct file_entry *get_file_entry (int fd);

void file_action_func (struct hash_elem *e, void *aux);

/* File Entry for Hash Table */
struct file_entry 
{
    struct hash_elem hash_elem;         /* Hash Elem for input into Hash Table */
    int fd;                             /* Integer fd (file descriptors) */
    struct file *file;                  /* Struct file */
};


/* A relationship manager for user processes. 

   Each user process has an rs_manager, used to store its children
   and its exit status, and also some synchronization primitives. */
struct process
{
    struct thread *thread;           /* Pointer to actual THREAD */
    int pid;

    struct process *parent_process;  /* Pointer to thread's parent process */
    struct list children;            /* List of child processes */
    struct list_elem child_elem;    /* List elem for child processes */

    bool loaded;                     /* Boolean for PROCESS loading executable */
    struct semaphore load_sema;      /* Control parent process when child loads exectuable */

    bool exited;                     /* Boolean for THREAD exit status */
    struct semaphore exit_sema;      /* Control parent process when child is exiting */
    int exit_status;                 /* Exit Status of the THREAD */

    struct lock filesys_lock;        /* Lock for file table */
    struct hash file_table;         /* Hash Table for mapping files owned by process to fd */
    int fd_new;                      /*  Fd value of the next file to be executed */
    struct file *executable;         /* Executable file of process */

};

#endif /* userprog/process.h */
