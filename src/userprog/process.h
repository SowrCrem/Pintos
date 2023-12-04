#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "../threads/synch.h"
#include "../lib/kernel/hash.h"
#include "../filesys/off_t.h"

/* Code duplication from thread.h, however, does not compile without. */
typedef int tid_t;

/* Process exit status codes. */
#define ERROR (-1)                            /* Process exited in error. */
#define SUCCESS (0)                           /* Process exited normally. */

/* File descriptors start from 2. */
#define FD_START (2)

/* Number of characters allowed to be processed from command line. */
#define MAX_CMDLINE_LEN (128)

/* Represents an entry in the hash table for supplemental page table. */
struct spt_entry
{
	uint8_t *upage;             /* User virtual page. */

	struct file *file;          /* File pointer. */
	off_t ofs;                  /* Offset of page in file. */
	size_t page_read_bytes;     /* Number of bytes to read from file. */
	size_t page_zero_bytes;     /* Number of bytes to zero. */
	bool writable;              /* True if page is writable. */
	
	bool loaded;                /* True if page is loaded. */
	bool swapped;               /* True if page is swapped. */
  bool stack_access;          /* True if page fault is a stack access */
	
	struct hash_elem elem; 			/* Hash table element. */
};

bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                   uint32_t read_bytes, uint32_t zero_bytes, bool writable);

/* Represents an entry in the hash table for files held by a process. */
struct file_entry
{
		struct hash_elem file_elem;               /* Hash elem for file_table. */
		struct file *file;                        /* Pointer to file. */
    char file_name[MAX_CMDLINE_LEN];          /* File name. */
		int fd;                                   /* File identifier. */
};

/* A relationship manager for user processes.

   Each user process has an rs_manager, used to store its children
   and its exit status, and also some synchronization primitives. */
struct rs_manager
{
  tid_t tid;                              /* Process identifier. */
  struct rs_manager *parent_rs_manager;   /* Pointer to parent rs_manager. */
  struct list children;                   /* List of all child rs_manager. */
  struct list_elem child_elem;            /* List elem for children list.  */
  
  struct hash file_table;                 /* Hash table for files. */
  struct lock file_table_lock;            /* Synchronize table accesses. */
  char exe_name[MAX_CMDLINE_LEN];         /* Store executable file name. */
  int fd_next;                            /* Counter for fd value. */

  struct semaphore child_load_sema;       /* Semaphore for process load. */
  bool load_success;                      /* Boolean for load status. */

  struct semaphore child_exit_sema;       /* Semaphore for process exit. */
  struct lock exit_lock;                  /* Lock for exiting child process. */
  bool running;                           /* Boolean for running status. */
  int exit_status;                        /* Exit status of process. */
};

void rs_manager_init (struct rs_manager *parent, struct thread *child);
struct rs_manager * get_child (struct thread *, tid_t);
struct file_entry * file_entry_lookup (int );

tid_t process_execute (const char *);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
bool install_page (void *, void *, bool);

#endif /* userprog/process.h */
  