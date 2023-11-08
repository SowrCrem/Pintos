#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

struct rs_manager 
    {
        struct thread *parent;   /* Point to parent thread    */
        struct list children;    /* List of all child threads */
        struct list_elem elem;   /* Elem for rs_manager list  */
        int exit_status;         /* Exit status of the thread */
    };∂

#endif /* userprog/process.h */
