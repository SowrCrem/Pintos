#ifndef SYSCALL_FUNC_H
#define SYSCALL_FUNC_H

#include "../userprog/memory-access.h"
#include "../userprog/process.h"
#include "../devices/shutdown.h"

/* Process identifier. */
typedef int pid_t;
typedef int mapid_t;

#define PID_ERROR ((pid_t) -1)

/* Void Syscall Error */
#define VOID_SYSCALL_ERROR (-2)

/* Maximum number of bytes */
#define MAX_BYTES_PUTBUF (300)

/* Maximum console file size in bytes. */
#define MAX_CONSOLE_FILE_SIZE (500)


/* System call helper functions */
intptr_t syscall_halt     (struct intr_frame *if_);
intptr_t syscall_exit     (struct intr_frame *if_);
intptr_t syscall_exec     (struct intr_frame *if_);
intptr_t syscall_wait     (struct intr_frame *if_);
intptr_t syscall_create   (struct intr_frame *if_);
intptr_t syscall_remove   (struct intr_frame *if_);
intptr_t syscall_open     (struct intr_frame *if_);
intptr_t syscall_filesize (struct intr_frame *if_);
intptr_t syscall_read     (struct intr_frame *if_);
intptr_t syscall_write    (struct intr_frame *if_);
intptr_t syscall_seek     (struct intr_frame *if_);
intptr_t syscall_tell     (struct intr_frame *if_);
intptr_t syscall_close    (struct intr_frame *if_);
intptr_t syscall_mmap     (struct intr_frame *if_);
intptr_t syscall_munmap   (struct intr_frame *if_);

#endif /* SYSCALL_FUNC_H */
