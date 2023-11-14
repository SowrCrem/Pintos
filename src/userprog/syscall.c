#include "../userprog/syscall.h"
#include "../threads/interrupt.h"
#include "../threads/thread.h"
#include "../devices/shutdown.h"
#include "../userprog/process.h"
#include "../userprog/pagedir.h"
#include "../threads/vaddr.h"
#include "../lib/syscall-nr.h"
#include <stdio.h>
#include <syscall-nr.h>

#define MAX_CONSOLE_FILE_SIZE 500		/* Maximum console file size. */

#define SYS_MIN SYS_HALT  /* Minimum system call number. */
#define SYS_MAX SYS_CLOSE /* Maximum system call number. */

#define STDIN_FILENO 0
#define STDOUT_FILENO 1

/* Memory access functions. */
static int get_user (const uint8_t *uaddr);
static bool put_user (uint8_t *udst, uint8_t byte);
static int get_user_safe (const uint8_t *uaddr);
static int32_t get_user_word_safe (const uint8_t *uaddr);
static bool put_user_safe (uint8_t *udst, uint8_t byte);
static int32_t syscall_get_arg (struct intr_frame *if_, int arg_num);
static bool syscall_invalid_arg (struct intr_frame *if_, int arg_num);
static bool syscall_get_args (struct intr_frame *if_, int argc, char** argv);

/* System call functions. */
static void syscall_halt (void);
static void syscall_exit (int status);
static pid_t syscall_exec (const char *file);
static int syscall_wait (pid_t pid);
static bool syscall_create (const char *file, unsigned initial_size);
static bool syscall_remove (const char *file);
static int syscall_open (const char *file);
static int syscall_filesize (int fd);
static int syscall_read (int fd, void *buffer, unsigned size);
static int syscall_write (int fd, const void *buffer, unsigned size);
static void syscall_seek (int fd, unsigned position);
static unsigned syscall_tell (int fd);
static void syscall_close (int fd);

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
	// printf("(syscall-get-arg) argument: %d\n", arg);
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
		printf("(setup-argv) syscall arg: %d\n", syscall_arg);
		if (syscall_arg == ERROR)
			return false;
		argv[i] = (char *) &syscall_arg;
	}
	return true;
}




/* Terminates a user process with given status. */
static void
terminate_userprog (int status)
{
	struct thread *cur = thread_current();

	/* Send exit status to kernel. */
	cur->process->exit_status = status;

	/* Output termination message (only if it is not a kernel thread). */
	printf ("%s: exit(%d)\n", cur->name, status);

	/* Terminate current process. */
	thread_exit ();
}

/* Terminates by calling shutdown_power_off(). Seldom used because
	 you lose information about possible deadlock situations. */
static void
syscall_halt (void)
{
	shutdown_power_off ();
}

/* Terminates the current user program, sending its exit status to 
   the kernel. Conventionally, a status of 0 indicates success and
	 nonzero values indicate errors. */
static void
syscall_exit (int status)
{
	terminate_userprog (status);
}

/* Runs the executable whose name is given in cmd line, passing any
 	 given arguments, and returns the new process’s program id (pid).
	 Must return pid -1, which otherwise should not be a valid pid, if
	 the program cannot load or run for any reason. */
static pid_t
syscall_exec (const char *cmd_line)
{
	return 0;
	// /* Runs executable of given cmd line file */
	// pid_t pid = (pid_t) process_execute (cmd_line);
	// /* Returns the new process program pid */

	// /* Check validity of pid */
	// if (pid == TID_ERROR)
	// {
	// 	return (pid_t) ERROR;
	// }

	// /* Cannot load or run for any reason */
	// struct process *parent = thread_current ()->process;

	// /* Find the file's corresponding process */
	// struct list *children = &parent->children;
  	// struct process *child;
	// for (struct list_elem *e = list_begin (children); e != list_end (children); 
	// 												e = list_next (e))
	// {
	// 	child = list_entry (e, struct process, child_elem);
	// 	/* Match corresponding child_tid to the child thread */
	// 	if (child->thread->tid == (tid_t) pid)
	// 	break;
	// 	child = NULL;
	// }

	// /* Check process validity */
	// if (child == NULL)
	// {
	// 	return (pid_t) ERROR;
	// }

	// /* Block parent process (cur) from running when child attempting to load executable */
	// sema_down (&parent->load_sema);

	// /* Check if child is loaded */
	// if (!parent->loaded)
	// {
	// 	return (pid_t) ERROR;
	// }

	// return pid;
}

/* Waits for a child process pid and retrieves the child’s exit status. */
static int
syscall_wait (pid_t pid)
{
	return process_wait ((tid_t) pid);
}

/* Creates a new file called file initially initial size bytes in size. 

	 Returns true if successful, false otherwise. Creating a new file 
	 does not open it: opening the new file is a separate operation which
	 would require a open system call. */
static bool
syscall_create (const char *file, unsigned initial_size)
{
	//return filesys_create (file, initial_size);
	return false;
}

/* Deletes the file called file. 

	 Returns true if successful, false otherwise. A file may be removed
	 regardless of whether it is open or closed, and removing an open 
	 file does not close it. */
static bool
syscall_remove (const char *file)
{
	// if (file == NULL)
	// 	return false;

	// return filesys_remove (file);
	return false;
}

static int
syscall_open (const char *file)
{
	/* TODO */
	return 0;
}

static int
syscall_filesize (int fd)
{
	/* TODO */
	return 0;
}

static int
syscall_read (int fd, void *buffer, unsigned size)
{
	/* TODO */
	return 0;
}

static int
syscall_write (int fd, const void *buffer, unsigned size)
{
	if (!(size > MAX_CONSOLE_FILE_SIZE))
	{
		if (fd == STDOUT_FILENO)
		{
			putbuf((char*) buffer, size);
		}
	}
	return 0;
}

static void
syscall_seek (int fd, unsigned position)
{
	/* TODO */
}

static unsigned
syscall_tell (int fd)
{
	/* TODO */
	return 0;
}

static void
syscall_close (int fd)
{
	/* TODO */
}


/* Call Handler Functions */

/* Array used for system calls for efficient function lookup. */
void *system_call_function[] = {
		[SYS_HALT]     = (uint32_t (*)(void)) syscall_halt,
		[SYS_EXIT]     = (uint32_t (*)(void)) syscall_exit,
		[SYS_EXEC]     = (uint32_t (*)(void)) syscall_exec,
		[SYS_WAIT]     = (uint32_t (*)(void)) syscall_wait,
		[SYS_CREATE]   = (uint32_t (*)(void)) syscall_create,
		[SYS_REMOVE]   = (uint32_t (*)(void)) syscall_remove,
		[SYS_OPEN]     = (uint32_t (*)(void)) syscall_open,
		[SYS_FILESIZE] = (uint32_t (*)(void)) syscall_filesize,
		[SYS_READ]     = (uint32_t (*)(void)) syscall_read,
		[SYS_WRITE]    = (uint32_t (*)(void)) syscall_write,
		[SYS_SEEK]     = (uint32_t (*)(void)) syscall_seek,
		[SYS_TELL]     = (uint32_t (*)(void)) syscall_tell,
		[SYS_CLOSE]    = (uint32_t (*)(void)) syscall_close,
};

/* Number of arguments for each system call. */
static int syscall_expected_argcs[] =
{
		[SYS_HALT]     = 0,
		[SYS_EXIT]     = 1,
		[SYS_EXEC]     = 1,
		[SYS_WAIT]     = 1,
		[SYS_CREATE]   = 2,
		[SYS_REMOVE]   = 1,
		[SYS_OPEN]     = 1,
		[SYS_FILESIZE] = 1,
		[SYS_READ]     = 3,
		[SYS_WRITE]    = 3,
		[SYS_SEEK]     = 2,
		[SYS_TELL]     = 1,
		[SYS_CLOSE]    = 1
};

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}


void
	syscall_execute_function (uint32_t (*func_pointer)(), int no_args, struct intr_frame *if_, uint32_t* result)
{
	/* Each case calls the specific function for the specified syscall */
	switch (no_args) {
		case 0:
			*result = func_pointer ();
			break;
		case 1:
			*result = func_pointer (syscall_get_arg(if_, 1));
			break;
		case 2:
			*result = func_pointer (syscall_get_arg(if_, 1), syscall_get_arg(if_, 2));
			break;
		case 3:
			*result = func_pointer (syscall_get_arg(if_, 1), syscall_get_arg(if_, 2), syscall_get_arg(if_, 3));
			break;
		default:
			/* Error for invalid Number of Arguments */
			thread_exit ();
			break;
	}
}

static void
syscall_handler (struct intr_frame *if_)
{
	int32_t syscall_no = get_syscall_no(if_);
	
	int expected_args = syscall_expected_argcs[syscall_no];

	/* Verification of user provided pointer happens within get_user_safe(), 
		 and dereferences. */
	/* TODO: Remove pagedir check and modify page_fault() in exception.c to
		 catch invalid user pointers */
	if (syscall_no != ERROR && pagedir_get_page 
								(thread_current ()->pagedir, if_->frame_pointer) != NULL)
	{

		/* De-reference frame pointer. */
		if_->frame_pointer = (*(uint32_t *) if_->frame_pointer);

		/* Initialise and setup arguments. */
		//printf ("(syscall_handler) setting up arguments\n");

		uint32_t result;

		/* Basic Syscall Handler */
		void* (*func_pointer) () = system_call_function[syscall_no];
		syscall_execute_function (func_pointer, expected_args, if_, &result);

		/* Store the Result in f->eax */
		if_->eax = result;
	}
	else
	{
		terminate_userprog (ERROR);
	}

	/* Handler Finishes - Exit the current Thread */
	//printf ("(syscall_handler) end of function.\n");

}
