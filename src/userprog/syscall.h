#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

/* Process identifier. */
typedef int pid_t;
#define PID_ERROR ((pid_t) -1)

/* Maximum number of bytes */
#define MAX_BYTES_PUTBUF 300 

/* Maximum console file size in bytes. */
#define MAX_CONSOLE_FILE_SIZE 500		

#define STDIN_FILENO 0
#define STDOUT_FILENO 1

void syscall_init (void);
void filesys_lock_init (void);
void terminate_userprog (int status);

#endif /* userprog/syscall.h */
