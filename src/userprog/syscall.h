#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

/* Process identifier. */
typedef int pid_t;
#define PID_ERROR ((pid_t) -1)

void syscall_init (void);
void filesys_lock_init (void);

#endif /* userprog/syscall.h */
