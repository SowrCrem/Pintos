#ifndef MEMORY_ACCESS_H
#define MEMORY_ACCESS_H

#include <stdio.h>
#include <stdlib.h>
#include <syscall-nr.h>
#include "../threads/interrupt.h"
#include "../threads/thread.h"
#include "../threads/vaddr.h"
#include "../userprog/process.h"
#include "../userprog/pagedir.h"
#include "../filesys/filesys.h"
#include "../filesys/file.h"
#include "../devices/input.h"

#define SYS_MIN SYS_HALT  	/* Minimum system call number. */
#define SYS_MAX SYS_CLOSE 	/* Maximum system call number. */

/* Memory access functions. */
int get_user (const uint8_t *uaddr);
bool put_user (uint8_t *udst, uint8_t byte);
int get_user_safe (const uint8_t *uaddr);
int32_t get_user_word_safe (const uint8_t *uaddr);
int32_t get_syscall_no (struct intr_frame *if_);
int get_argc (struct intr_frame *if_);
bool put_user_safe (uint8_t *udst, uint8_t byte);
int32_t syscall_get_arg (struct intr_frame *if_, int arg_num);
bool syscall_invalid_arg (struct intr_frame *if_, int arg_num);
bool syscall_get_args (struct intr_frame *if_, int argc, char** argv);

#endif /* userprog/memory-access.h */
