#ifndef SYSCALL_FUNC_H
#define SYSCALL_FUNC_H

#include "../userprog/memory-access.h"
#include "../userprog/process.h"
#include "../devices/shutdown.h"

/* Process identifier. */
typedef int pid_t;
#define PID_ERROR ((pid_t) -1)

/* Maximum number of bytes */
#define MAX_BYTES_PUTBUF (300)

/* Maximum console file size in bytes. */
#define MAX_CONSOLE_FILE_SIZE (500)


/* System call helper functions */
void syscall_halt     (struct intr_frame *if_);
void syscall_exit     (struct intr_frame *if_);
void syscall_exec     (struct intr_frame *if_);
void syscall_wait     (struct intr_frame *if_);
void syscall_create   (struct intr_frame *if_);
void syscall_remove   (struct intr_frame *if_);
void syscall_open     (struct intr_frame *if_);
void syscall_filesize (struct intr_frame *if_);
void syscall_read     (struct intr_frame *if_);
void syscall_write    (struct intr_frame *if_);
void syscall_seek     (struct intr_frame *if_);
void syscall_tell     (struct intr_frame *if_);
void syscall_close    (struct intr_frame *if_);

#endif //SYSCALL_FUNC_H
