#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#define MAX_BYTES_PUTBUF 300

/* Process identifier. */
typedef int pid_t;
#define PID_ERROR ((pid_t) -1)

void syscall_init (void);
 void
terminate_userprog (int status);

#endif /* userprog/syscall.h */
