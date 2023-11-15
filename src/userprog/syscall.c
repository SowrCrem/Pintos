#include "filesys/file.h"
#include "filesys/filesys.h"
#include "../userprog/syscall.h"
#include "../threads/interrupt.h"
#include "../threads/thread.h"
#include "../devices/shutdown.h"
#include "../userprog/process.h"
#include "../userprog/pagedir.h"
#include "../threads/vaddr.h"
#include "../lib/syscall-nr.h"
#include "../devices/input.h"
#include <stdio.h>
#include <syscall-nr.h>

#define MAX_CONSOLE_FILE_SIZE 500		/* Maximum console file size. */

#define SYS_MIN SYS_HALT  /* Minimum system call number. */
#define SYS_MAX SYS_CLOSE /* Maximum system call number. */

#define STDIN_FILENO 0
#define STDOUT_FILENO 1

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
	struct process *p  = cur->process;

	/* Send exit status to kernel. */
	p->exit_status = status;

	/* Output termination message (only if it is not a kernel thread). */
	printf ("%s: exit(%d)\n", cur->name, status);


	/* Terminate current process. */
	thread_exit ();
}

// static void
// store_result (uint32_t result_ptr, struct intr_frame *if_) {
// 	/* De-Reference result_ptr and store it in if_eax */
// 	/* TODO: Check if result_ptr is a valid address (is this needed?) */
// 	if_->eax = *result_ptr;
// }


/* Syscall Functions */
/* TODO: Move to separate file i.e. syscall_func.c, syscall_func.h */

/* Terminates by calling shutdown_power_off(). Seldom used because
	 you lose information about possible deadlock situations. */
static void
halt (void)
{
	// printf ("(halt) shutting down\n");
	shutdown_power_off ();
	// printf ("(halt) shouldn't reach this point\n");
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

	/* Runs executable of given cmd line file */
	pid_t pid = (pid_t) process_execute (cmd_line);
	/* Returns the new process program pid */

	/* Check validity of pid */
	if (pid == TID_ERROR)
	{
		return (pid_t) ERROR;
	}

	/* Cannot load or run for any reason */
	struct process *parent = thread_current ()->process;

	/* Find the file's corresponding process */
	struct list *children = &parent->children;
  	struct process *child;
	for (struct list_elem *e = list_begin (children); e != list_end (children); 
													e = list_next (e))
	{
		child = list_entry (e, struct process, child_elem);
		/* Match corresponding child_tid to the child thread */
		if (child->pid ==  pid)
			break;
		child = NULL;
	}

	/* Check process validity */
	if (child == NULL)
	{
		return (pid_t) ERROR;
	}

	/* Block parent process (cur) from running when child attempting to load executable */
	sema_down (&child->load_sema);

	/* Check if child is loaded */
	if (!child->loaded)
	{
		return (pid_t) ERROR;
	}

	return pid;
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
	struct process *p = thread_current ()->process;

	if (file == NULL)
	{
		terminate_userprog(ERROR);
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
	struct process *p = thread_current ()->process;

	if (file == NULL)
		return false;

	lock_acquire (&filesys_lock);
	bool result = filesys_remove (file);
	lock_release (&filesys_lock);

	return result;
}

static int
open (const char *file)
{

	/* Process that opened this file */
	struct process *p = thread_current ()->process;

	/* Check validity of file name */
	if (file == NULL)
	{
		return ERROR;
	}

	/* Add file and corresponding fd to process's hash table */

	/* Open file */
	lock_acquire (&filesys_lock);
	struct file *f = filesys_open (file);
	lock_release (&filesys_lock);
	/* Check validity of open file */
	if (f == NULL)
	{
		return ERROR;
	}

	/* Find fd */

	/* Dynamically allocate the file entry */
	struct file_entry *entry = malloc (sizeof (struct file_entry));


	entry->file = f;
	entry->fd   = p->fd_new;

	p->fd_new++;

	/* Insert the file entry into the process's table */
	hash_insert (&p->file_table, &entry->hash_elem);

	return p->fd_new;
}


static int
filesize (int fd)
{
	struct process *p = thread_current ()->process;

	struct file_entry *f = get_file_entry (fd);
	if (f->file == NULL)
	{
		return ERROR;
	}
	lock_acquire(&filesys_lock);
	int result = file_length (f->file);
	lock_release(&filesys_lock);

	return result;

}

static int
read (int fd, void *buffer, unsigned size)
{	
	struct process *p = thread_current ()->process;

	if (buffer == NULL || !is_user_vaddr (buffer + size))
	{
		terminate_userprog (ERROR);
		return;
	}

	if (fd == STDOUT_FILENO)
	{
		/* Cannot read from standard output. */
		return ERROR;
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
		struct file_entry *e = get_file_entry (fd);
		if (e == NULL)
		{
			return SUCCESS; /* TODO: Allows for file_entry to work */
		}
		struct file *file = e->file;

		lock_acquire (&filesys_lock);
		int result = file_read (file, buffer, size);
		lock_release (&filesys_lock);

		return result;
	}
}

static int
write (int fd, const void *buffer, unsigned size)
{
	if (buffer == NULL)
	{
		return ERROR;
	}
	if (!(size > MAX_CONSOLE_FILE_SIZE))
	{
		if (fd == STDOUT_FILENO)
		{
			putbuf((char*) buffer, size);
		}
	}
	return size;
}

// static void
// seek (int fd, unsigned position)
// {
// 	struct file_entry *e = get_file_entry (fd);
// 	if (file == NULL)
// 	{
// 		return -1;
// 	}

// 	file_seek (file, (off_t) position);
// }

// static unsigned
// tell (int fd)
// {
// 	struct file_entry *e = get_file_entry (fd);

// 	file_tell (fd);
// }

static void
close (int fd)
{
	struct process *p = thread_current ()->process;
	struct file_entry *file_entry = get_file_entry (fd);

	if (file_entry == NULL)
	{
		return ERROR;
	}

	/* Call file_close on file */
	lock_acquire (&filesys_lock);
	file_close (file_entry->file);
	lock_release (&filesys_lock);

	/* Remove entry from table */
	hash_delete (&p->file_table, &file_entry->hash_elem);

	/* Decrement fd_current - maybe not necessary */

	/* Free entry cause malloced */
	free (file_entry);
	file_entry->file = NULL;
}


/* Syscall Helper Functions */

static void
syscall_halt (struct intr_frame *if_ UNUSED)
{
	halt ();
}

static void
syscall_exit (struct intr_frame *if_)
{
	int status = syscall_get_arg (if_, 1);
	exit (status);
}

static void
syscall_exec (struct intr_frame *if_)
{
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
	//
}

static void
syscall_open (struct intr_frame *if_)
{
	/* TODO: Retrieve file from if_ or an argv array */
	char *file = (char *) syscall_get_arg (if_, 1);

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
	int fd = (int) syscall_get_arg (if_, 1);
	void *buffer = syscall_get_arg (if_, 2);
	unsigned size = syscall_get_arg (if_, 3);

	/* Get result and store it in if_->eax */
	int result = read (fd, buffer, size);
	
	if_->eax = result;
}

static void
syscall_write (struct intr_frame *if_)
{
	/* TODO: Retrieve fd, buffer, size from if_ or an argv array */
	int fd = syscall_get_arg (if_, 1);
	void *buffer = syscall_get_arg (if_, 2);
	unsigned size = syscall_get_arg (if_, 3);

	/* Get result and store it in if_->eax */
	int result = write (fd, buffer, size);
	
	if_->eax = result;
}

static void
syscall_seek (struct intr_frame *if_)
{
	/* TODO: Retrieve fd, buffer, size from if_ or an argv array */
	int fd = syscall_get_arg (if_, 1);
	unsigned position = syscall_get_arg (if_, 2);

	struct process *cur = thread_current ();
	struct file_entry *e = get_file_entry (fd);
	if (e == NULL)
	{
		if_->eax = ERROR;
		return;
	}

	/* Execute seek syscall with arguments */
	lock_acquire (&cur->filesys_lock);
	file_seek (fd, position);
	lock_release (&cur->filesys_lock);

}

static void
syscall_tell (struct intr_frame *if_)
{
	/* TODO: Retrieve fd, buffer, size from if_ or an argv array */
	int fd = syscall_get_arg (if_, 1);

	/* Get result and store it in if_->eax */
	struct process *cur = thread_current ();
	struct file_entry *e = get_file_entry (fd);
	if (e == NULL)
	{
		if_->eax = ERROR;
		return;
	}

	/* Execute seek syscall with arguments */
	lock_acquire (&cur->filesys_lock);
	unsigned result = file_tell (fd);
	lock_release (&cur->filesys_lock);
	if_->eax = result;
}

static void
syscall_close (struct intr_frame *if_)
{
	/* TODO: Retrieve fd, buffer, size from if_ or an argv array */
	int fd = (int) syscall_get_arg (if_, 1);

	/* Execute close syscall with arguments */
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
	/* printf ("(syscall_handler) syscall num: %d", syscall_no); */
	/* Verification of user provided pointer happens within get_user_safe(), and dereferences. */

	/* TODO: Remove page-dir check and modify page_fault() in exception.c to catch invalid user pointers */
	void *page = pagedir_get_page (thread_current ()->pagedir, if_->frame_pointer);

	if (syscall_no != ERROR && page != NULL)
	{
		/* De-reference frame pointer. */
		if_->frame_pointer = (*(uint32_t *) if_->frame_pointer);

		/* Execute Syscall */
		syscall_execute_function (syscall_no, if_);
	}
	else
	{
		terminate_userprog (ERROR);
	}

	/* Handler Finishes - Exit the current Thread */
	/* printf ("(syscall_handler) end of function for %s\n",	thread_current ()->name); */
}

/* Helper function to return file_entry for corresponding fd*/
struct file_entry *
get_file_entry (int fd)
{

	struct process *p = thread_current ()->process;

	/* Get file from fd value */
	struct file_entry target_entry;
	target_entry.fd = fd;

	struct hash_elem *elem = hash_find (&p->file_table, &target_entry.hash_elem);
	

	if (elem == NULL)
	{
		return NULL;
	}
	return hash_entry (elem, struct file_entry, hash_elem);

}
