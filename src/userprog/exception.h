#ifndef USERPROG_EXCEPTION_H
#define USERPROG_EXCEPTION_H

/* Page fault error code bits that describe the cause of the exception.  */
#define PF_P 0x1    /* 0: not-present page. 1: access rights violation. */
#define PF_W 0x2    /* 0: read, 1: write. */
#define PF_U 0x4    /* 0: kernel, 1: user process. */

/* Number of bytes below stack pointer to check for valid stack access. */
#define PUSHA_BYTES_BELOW (32)
#define PUSH_BYES_BELOW (4)

void exception_init (void);
void exception_print_stats (void);

#endif /* userprog/exception.h */
