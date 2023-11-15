#include "../userprog/syscall.h"
#include "../threads/interrupt.h"
#include "../threads/thread.h"
#include "../devices/shutdown.h"
#include "../userprog/process.h"
#include "../userprog/pagedir.h"
#include "../threads/vaddr.h"
#include "../lib/syscall-nr.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "../devices/input.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "../devices/input.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "../lib/kernel/stdio.h"

#define SYS_MIN SYS_HALT  	/* Minimum system call number. */
#define SYS_MAX SYS_CLOSE 	/* Maximum system call number. */

/* Memory access functions. */
static int     get_user            (const uint8_t *uaddr);
static bool    put_user            (uint8_t *udst, uint8_t byte);
static int     get_user_safe       (const uint8_t *uaddr);
static int32_t get_user_word_safe  (const uint8_t *uaddr);
static bool    put_user_safe       (uint8_t *udst, uint8_t byte);
static int32_t syscall_get_arg     (struct intr_frame *if_, int arg_num);
static bool    syscall_invalid_arg (struct intr_frame *if_, int arg_num);
static bool    syscall_get_args    (struct intr_frame *if_, int argc, char** argv);
/* terminate_userprog */
static void    store_result        (uint32_t result_ptr, struct intr_frame *if_);

/* System call functions. */
static void     halt     (void);
static void     exit     (int status);
static pid_t    exec     (const char *file);
static int      wait     (pid_t pid);
static bool     create   (const char *file, unsigned initial_size);
static bool     remove   (const char *file);
static int      open     (const char *file);
static int      filesize (int fd);
static int      read     (int fd, void *buffer, unsigned size);
static int      write    (int fd, const void *buffer, unsigned size);
static void     seek     (int fd, unsigned position);
static unsigned tell     (int fd);
static void     close    (int fd);

/* System call helper functions */
static void syscall_halt     (struct intr_frame *if_);
static void syscall_exit     (struct intr_frame *if_);
static void syscall_exec     (struct intr_frame *if_);
static void syscall_wait     (struct intr_frame *if_);
static void syscall_create   (struct intr_frame *if_);
static void syscall_remove   (struct intr_frame *if_);
static void syscall_open     (struct intr_frame *if_);
static void syscall_filesize (struct intr_frame *if_);
static void syscall_read     (struct intr_frame *if_);
static void syscall_write    (struct intr_frame *if_);
static void syscall_seek     (struct intr_frame *if_);
static void syscall_tell     (struct intr_frame *if_);
static void syscall_close    (struct intr_frame *if_);

static void syscall_handler (struct intr_frame *);

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
	int result;
	asm ("movl $1f, %0; movzbl %1, %0; 1:"
			: "=&a" (result) : "m" (*uaddr));
	return result;
}

/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
	int error_code;
	asm ("movl $1f, %0; movb %b2, %1; 1:"
			: "=&a" (error_code), "=m" (*udst) : "q" (byte));
	return error_code != -1;
}

/* Checks if UADDR is valid (Points below PHYS_BASE) and retrieves if it is. 
   Returns -1 otherwise. */
static int
get_user_safe (const uint8_t *uaddr)
{
	if (is_user_vaddr (uaddr)) /* Checks if UADDR is below PHYS_BASE */
		return get_user (uaddr);
	
	terminate_userprog (ERROR);
	// exit (ERROR);
	return ERROR;
}

/* Checks if UADDR is valid (Points below PHYS_BASE) and writes BYTE to it if it is.
	 Returns false otherwise. */
static bool
put_user_safe (uint8_t *udst, uint8_t byte)
{
	if (is_user_vaddr (udst))
		return put_user (udst, byte);
	return false;
}

/* Retrieves a word from UADDR and returns -1 otherwise. */
static int32_t
get_user_word_safe (const uint8_t *uaddr)
{
	/* TODO: Refactor */
	int32_t word = 0;
	for (int i = 0; i < WORD_SIZE; i++)
	{
		int byte = get_user_safe (uaddr + i);

		if (byte == ERROR)
			return ERROR;

		word |= byte << (BYTE_SIZE_BITS * i);
	}
	return word;
}

/* Returns system call number for a given interrupt frame if_, -1 if not successful */
static int32_t
get_syscall_no (struct intr_frame *if_)
{
	int32_t syscall_no = get_user_word_safe ((uint8_t *) if_->esp);
	if (syscall_no < SYS_MIN || syscall_no > SYS_MAX)
		syscall_no = ERROR;
	return syscall_no;
}

/* Returns argc for a given interrupt frame if_, -1 if not successful */
static int
get_argc (struct intr_frame *if_)
{
	int32_t argc = get_user_word_safe ((uint8_t *) if_->esp + WORD_SIZE);
	if (argc < 0)
		argc = ERROR;
	return argc;
}

/* Retrieves argument arg_num from argv array. */
static int32_t
syscall_get_arg (struct intr_frame *if_, int arg_num)
{
	int32_t arg = 
		get_user_word_safe ((uint8_t *) if_->esp + (WORD_SIZE * (arg_num)));
	// printf("(syscall-get-arg) integer argument %d: %d for %s\n", arg_num, 
	// 			 arg, thread_current ()->name);
	return arg;
}

/* Checks if if_ contains an argument at arg_num */
static bool
syscall_invalid_arg (struct intr_frame *if_, int arg_num)
{
	return syscall_get_arg (if_, arg_num) == ERROR;
}

/* Populates argv array. If at any point an argument is invalid 
	 (i.e. not enough arguments provided), returns false */
static bool
syscall_get_args (struct intr_frame *if_, int argc, char** argv)
{
	// printf("(syscall-get-args) entered\n");
	for (int i = 0; i < argc; i++)
	{
		int32_t syscall_arg = syscall_get_arg (if_, i + 1);
		// printf("(setup-argv) syscall arg: %d\n", syscall_arg);
		if (syscall_arg == ERROR)
			return false;
		argv[i] = (char *) &syscall_arg;
	}
	return true;
}

/* Terminates a user process with given status. */
void
terminate_userprog (int status)
{
	struct thread *cur = thread_current();

	/* Send exit status to kernel. */
	cur->rs_manager->exit_status = status;

	// printf ("%s current tid %d\n", cur->name, cur->tid);

	/* Output termination message (only if it is not a kernel thread). */
	printf ("%s: exit(%d)\n", cur->name, status);

	/* Terminate current process. */
	thread_exit ();
}

/* Terminates by calling shutdown_power_off(). Seldom used because
	 you lose information about possible deadlock situations. */
static void
halt (void)
{
	shutdown_power_off ();
}

/* Terminates the current user program, sending its exit status to 
   the kernel. Conventionally, a status of 0 indicates success and
	 nonzero values indicate errors. */
static void
exit (int status)
{
	// printf ("(sycall_exit) running exit(%d)\n", status);
	terminate_userprog (status);
}

/* Runs the executable whose name is given in cmd line, passing any
 	 given arguments, and returns the new process’s program id (pid).
	 Must return pid -1, which otherwise should not be a valid pid, if
	 the program cannot load or run for any reason. */
static pid_t
exec (const char *cmd_line)
{
	tid_t tid = process_execute (cmd_line);

	/* Find newly created child process and decrement child_load_sema. */
  struct rs_manager *child_rs_manager = get_child (thread_current (), tid);
	
	if (child_rs_manager == NULL)
	{
		// printf ("(syscall_exec) newly created child process not found\n");
		return ERROR;
	}

  // printf ("(syscall_exec) about to decrement load sema for %s\n",
	// 				child_rs_manager->thread->name);
  sema_down (&child_rs_manager->child_load_sema);

	// printf ("(syscall_exec) just decremented load sema for %s, with tid %d\n",
	// 				child_rs_manager->thread->name, tid);

  /* Continues (and returns) only after child process has loaded successfully
     (or failed load). */

	/* Return TID if child process loaded succesfully, else -1. */
  if (child_rs_manager->load_success)
    return tid;
  else
    return ERROR;
}

/* Waits for a child process pid and retrieves the child’s exit status. */
static int
wait (pid_t pid)
{
	return process_wait (pid);
}

/* Creates a new file called file initially initial size bytes in size. 

	 Returns true if successful, false otherwise. Creating a new file 
	 does not open it: opening the new file is a separate operation which
	 would require a open system call. */
static bool
create (const char *file, unsigned initial_size)
{
	if (file == NULL)
	{
		terminate_userprog (ERROR);
	}

	lock_acquire (&filesys_lock);
	bool result = filesys_create (file, initial_size);
	lock_release (&filesys_lock);

	return result;
}

/* Deletes the file called file. 

	 Returns true if successful, false otherwise. A file may be removed
	 regardless of whether it is open or closed, and removing an open 
	 file does not close it. */
static bool
remove (const char *file)
{
	if (file == NULL)
	{
		return false;
	}

	lock_acquire (&filesys_lock);
	bool result = filesys_remove (file);
	lock_release (&filesys_lock);

	return result;
}

static int
open (const char *file_name)
{
	if (file_name == NULL)
	{
		return ERROR;
	}

	/* Add file and corresponding fd to process's hash table. */
	lock_acquire (&filesys_lock);
	struct file *file = filesys_open (file_name);
	lock_release (&filesys_lock);
	
	/* Return error if file could not be opened. */
	if (file == NULL)
	{
		return ERROR;
	}

	struct rs_manager *rs = thread_current ()->rs_manager;
	
	/* Dynamically allocate the file entry. */
	struct file_entry *entry = malloc (sizeof (struct file_entry));

	if (entry == NULL)
	{
		printf ("(open) malloc failed\n");
		return ERROR;
	}

	entry->file = file;

	/* Get file descriptor and increment fd_next for next file descriptor. */
	entry->fd = rs->fd_next++;

	/* Insert file entry into the file table. */
	lock_acquire (&rs->file_table_lock);
	hash_insert (&rs->file_table, &entry->file_elem);
	lock_release (&rs->file_table_lock);

	return entry->fd;
}

static int
filesize (int fd)
{
	struct file *file = file_entry_lookup (fd)->file;

	lock_acquire (&filesys_lock);
	int result = file_length (file);
	lock_release (&filesys_lock);

	return result;
}

static int
read (int fd, void *buffer, unsigned size)
{
	if (buffer == NULL || !is_user_vaddr (buffer + size))
	{
		terminate_userprog (ERROR);
	}

	if (fd == STDOUT_FILENO)
	{
		/* Cannot read from standard output. */
		return;
	} 
	else if (fd == STDIN_FILENO)
	{
		/* Can read from standard input. */
		for (unsigned i = 0; i < size; i++) 
		{
			((uint8_t *) buffer) [i] = input_getc ();
		}
		return size;
	}
	else
	{
		struct file *file = file_entry_lookup (fd)->file;

		if (file == NULL)
		{
			return ERROR;
		}

		lock_acquire (&filesys_lock);
		int result = file_read (file, buffer, size);
		lock_release (&filesys_lock);

		return result;
	}
}

static int
write (int fd, const void *buffer, unsigned size)
{
	if (fd == STDIN_FILENO)
 	{
		/* Cannot write to standard input; return 0 (number of bytes read). */
		return SUCCESS;

 	} else if (fd == STDOUT_FILENO)
 	{
		/* Write to standard output. */
		int i = size;
		if (size > MAX_BYTES_PUTBUF)
		{
			/* Write in chunks to avoid stack overflow. */
			while (i > MAX_BYTES_PUTBUF)
			{
				putbuf (buffer, MAX_BYTES_PUTBUF);
				buffer += MAX_BYTES_PUTBUF;
				i -= MAX_BYTES_PUTBUF;
			}
		}
 		
		putbuf (buffer, i);
		return size;

	} else
	{
		struct file *file = file_entry_lookup (fd)->file;
		if (file == NULL)
		{
			/* Return 0 (for number of bytes read). */
			return SUCCESS;
		}

		lock_acquire (&filesys_lock);
		int result = (int) file_write (file, buffer, size);
		lock_release (&filesys_lock);

		return result;
	}
}

static void
seek (int fd, unsigned position)
{
	struct file *file = file_entry_lookup (fd)->file;
	
	lock_acquire (&filesys_lock);
	file_seek (file, (off_t) position);
	lock_release (&filesys_lock);
}

static unsigned
tell (int fd)
{
	struct file *file = file_entry_lookup (fd)->file;

	lock_acquire (&filesys_lock);
	unsigned result = (unsigned) file_tell (file);	
	lock_release (&filesys_lock);
	
	return result;
}

static void
close (int fd)
{
	// printf ("(close) entered, closing fd %d\n", fd);

	struct rs_manager *rs = thread_current ()->rs_manager;
	// printf ("(close) reached 1\n");
	struct file_entry *file_entry = file_entry_lookup (fd);
	// printf ("(close) reached 2\n");

	if (file_entry == NULL)
	{
	// printf ("(close) reached 3\n");
		terminate_userprog (ERROR);
	}
	// printf ("(close) reached 4\n");


	/* Remove entry from table. */
	lock_acquire (&rs->file_table_lock);
	hash_delete (&rs->file_table, &file_entry->file_elem);
	lock_release (&rs->file_table_lock);

	lock_acquire (&filesys_lock);
	file_close (file_entry->file);
	lock_release (&filesys_lock);

	free (file_entry);
}


/* Syscall Helper Functions */

static void
syscall_halt (UNUSED struct intr_frame *if_)
{
	halt ();
}

static void
syscall_exit (UNUSED struct intr_frame *if_)
{
	int status = syscall_get_arg (if_, 1);
	//int status = syscall_get_arg (if_, 0);
	exit (status);
}

static void
syscall_exec (struct intr_frame *if_)
{
	/* TODO: Retrieve cmd_line from if_ or an argv array */
	char *cmd_line = syscall_get_arg (if_, 1);

	/* Get result and store it in if_->eax */
	pid_t result = exec (cmd_line);
	if_->eax = result;
}

static void
syscall_wait (struct intr_frame *if_)
{
	/* TODO: Retrieve pid from if_ or an argv array */
	pid_t pid = syscall_get_arg (if_, 1);

	/* Get result and store it in if_->eax */
	int result = wait (pid);
	if_->eax = result;
}

static void
syscall_create (struct intr_frame *if_)
{
	/* TODO: Retrieve file, initial_size from if_ or an argv array */
	char *file = syscall_get_arg (if_, 1);
	unsigned initial_size = syscall_get_arg (if_, 2);

	/* Get result and store it in if_->eax */
	bool result = create (file, initial_size);
	if_->eax = result;
}

static void
syscall_remove (struct intr_frame *if_)
{
	/* TODO: Retrieve file from if_ or an argv array */
	char *file = syscall_get_arg (if_, 1);

	/* Get result and store it in if_->eax */
	int result = remove (file);
	if_->eax = result;
}

static void
syscall_open (struct intr_frame *if_)
{
	/* TODO: Retrieve file from if_ or an argv array */
	char *file = syscall_get_arg (if_, 1);

	/* Get result and store it in if_->eax */
	int result = open (file);
	if_->eax = result;
}

static void
syscall_filesize (struct intr_frame *if_)
{
	/* TODO: Retrieve fd from if_ or an argv array */
	int fd = syscall_get_arg (if_, 1);

	/* Get result and store it in if_->eax */
	int result = filesize (fd);
	if_->eax = result;
}

static void
syscall_read (struct intr_frame *if_)
{
	/* TODO: Retrieve fd, buffer, size from if_ or an argv array */
	int fd = syscall_get_arg (if_, 1);
	void *buffer = syscall_get_arg (if_, 2);
	unsigned size = syscall_get_arg (if_, 3);

	/* Get result and store it in if_->eax */
	int result = read (fd, buffer, size);
	if_->eax = result;
}

static void
syscall_write (struct intr_frame *if_)
{
	/* Retrieve fd, buffer, size from if_ or an argv array */
	int fd = syscall_get_arg (if_, 1);
	void *buffer = syscall_get_arg (if_, 2);
	unsigned size = syscall_get_arg (if_, 3);

	/* Get result and store it in if_->eax */
	int result = write (fd, buffer, size);
	if_->eax = result;
}

static void
syscall_seek (UNUSED struct intr_frame *if_)
{
	int fd = syscall_get_arg (if_, 1);
	unsigned position = syscall_get_arg (if_, 2);

	/* Execute seek syscall with arguments. */
	seek (fd, position);
}

static void
syscall_tell (struct intr_frame *if_)
{
	/* Retrieve fd from if_ or an argv array. */
	int fd = syscall_get_arg (if_, 1);

	/* Get result and store it in if_->eax. */
	unsigned result = tell (fd);
	if_->eax = result;
}

static void
syscall_close (UNUSED struct intr_frame *if_)
{
	int fd = syscall_get_arg (if_, 1);
	close (fd);
}


/* Call Handler Functions */

/* Array used to lookup system call functions */
void *system_call_function[] = {
		[SYS_HALT]     = syscall_halt,
		[SYS_EXIT]     = syscall_exit,
		[SYS_EXEC]     = syscall_exec,
		[SYS_WAIT]     = syscall_wait,
		[SYS_CREATE]   = syscall_create,
		[SYS_REMOVE]   = syscall_remove,
		[SYS_OPEN]     = syscall_open,
		[SYS_FILESIZE] = syscall_filesize,
		[SYS_READ]     = syscall_read,
		[SYS_WRITE]    = syscall_write,
		[SYS_SEEK]     = syscall_seek,
		[SYS_TELL]     = syscall_tell,
		[SYS_CLOSE]    = syscall_close,
};


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void
syscall_execute_function (int32_t syscall_no, struct intr_frame *if_)
{
	void* (*func_pointer) (struct intr_frame *if_) = system_call_function[syscall_no];
	func_pointer (if_);
}

static void
syscall_handler (struct intr_frame *if_)
{
	int32_t syscall_no = get_syscall_no(if_);
	
	// printf ("(syscall_handler) syscall num: %d\n", syscall_no);
	
	/* Verification of user provided pointer happens within get_user_safe(), and dereferences. */

	/* TODO: Remove page-dir check and modify page_fault() in exception.c to catch invalid user pointers */
	void *page = pagedir_get_page (thread_current ()->pagedir, if_->frame_pointer);

	if (syscall_no != ERROR && page != NULL)
	{
		/* De-reference frame pointer. */
		if_->frame_pointer = (*(uint32_t *) if_->frame_pointer);

		/* Execute Syscall */
		syscall_execute_function (syscall_no, if_);

	} else
	{
		terminate_userprog (ERROR);
	}
}
