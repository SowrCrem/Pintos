#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

/* Exit status definitions. */
#define SUCCESS_CODE (0)                /* Success code. */
#define ERROR_CODE (-1)                 /* Error code. */

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
