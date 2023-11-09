#include "../userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "../threads/interrupt.h"
#include "../threads/thread.h"
#include "../devices/shutdown.h"
#include "../userprog/process.h"
#include "../userprog/pagedir.h"
#include "../threads/vaddr.h"

#define MAX_CONSOLE_FILE_SIZE 500   /* Maximum Console File Size */

/* Terminates by calling shutdown_power_off()
   Seldom used because you lose information about possible deadlock situations. */
static void
halt (void)
{
	shutdown_power_off ();
}

/* Terminates the current user program, sending its exit status to the kernel.
   Conventionally, a status of 0 indicates success and nonzero values indicate errors. */
static void
exit (int status)
{
	struct thread *cur = thread_current ();
	/* Output termination message (only if it is not a kernel thread). */
	printf ("%s: exit(%d)\n", cur->name, status);

	/* Send exit status to kernel */
	cur->rs_manager->exit_status = status;

	/* Terminate current process */
	process_exit ();
}

/* Runs the executable whose name is given in cmd line, passing any given arguments, and
   returns the new process’s program id (pid). Must return pid -1, which otherwise should not
   be a valid pid, if the program cannot load or run for any reason. */
static pid_t
exec (const char *file)
{
	/* TODO */
	return 0;
}

/* Waits for a child process pid and retrieves the child’s exit status. */
static int
wait (pid_t pid)
{
	/* TODO */
	return 0;
}

static bool
create (const char *file, unsigned initial_size)
{
	/* TODO */
	return 0;
}

static bool
remove (const char *file)
{
	/* TODO */
	return 0;
}

static int
open (const char *file)
{
	/* TODO */
	return 0;
}

static int
filesize (int fd)
{
	/* TODO */
	return 0;
}

static int
read (int fd, void *buffer, unsigned size)
{
	/* TODO */
	return 0;
}

static int
write (int fd, const void *buffer, unsigned size)
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
seek (int fd, unsigned position)
{
	/* TODO */
}

static unsigned
tell (int fd)
{
	/* TODO */
	return 0;
}

static void
close (int fd)
{
	/* TODO */
}

/* Array of function pointers for system calls for efficient function lookup. */
void (*system_call_function[]) (void) = {
		[SYS_HALT] = halt,
		[SYS_EXIT] = exit,
		/* Casting to appropriate function pointer type. */
		[SYS_EXEC] = (void (*)(void)) exec,
		[SYS_WAIT] = (void (*)(void)) wait,
		[SYS_CREATE] = (void (*)(void)) create,
		[SYS_REMOVE] = (void (*)(void)) remove,
		[SYS_OPEN] = (void (*)(void)) open,
		[SYS_FILESIZE] = (void (*)(void)) filesize,
		[SYS_READ] = (void (*)(void)) read,
		[SYS_WRITE] = (void (*)(void)) write,
		[SYS_SEEK] = (void (*)(void)) seek,
		[SYS_TELL] = (void (*)(void)) tell,
		[SYS_CLOSE] = (void (*)(void)) close,
};

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Sets up arguments ESP, SYSCALL_NO, ARGC, ARGV. */
static void setup_args (void *esp, int* syscall_no, int* argc, char** argv) {
	*syscall_no = *(int *) esp;
	*argc = *((int *) (esp + sizeof (int)));
	/* TODO: Check Syntax */
	/* TODO: Make clean ways of reading and writing data to virtual memory */

	for (int i = 0; i < *argc; i++) {
		/* Increment esp, retrieve current argument and add to argv array. */
		esp += sizeof (char *);
		/* Store the pointer to the current argument in argv[i]. */
		argv[i] = *(char **)esp;
	}
}


static void
syscall_handler (struct intr_frame *f)
{
	/* Verification of user provided pointers, and dereferences. */
	/* TODO: Change to Method 2 */
	if (is_user_vaddr (f))
	{
		if (pagedir_get_page (thread_current ()->pagedir, f->frame_pointer) != NULL)
		{
			/* De-reference frame pointer. */
			f->frame_pointer = (void *) (*(uint32_t *)f->frame_pointer);

			/* Initialise and setup arguments. */
			char **argv = (char **) (f->esp + sizeof(int) * 2); /* Increments by 4 bytes */
			int syscall_no;
			int *argc;
			setup_args(f->esp, &syscall_no, argc, argv);
			void* result;

			/* Basic Syscall Handler - Each case calls the specific function for the specified syscall */
			void* (*func_pointer) () = system_call_function[syscall_no];

			switch (*argc) {
				case 0:
					/* TODO: Add Exception Handing for if user enters a syscall and the wrong number of arguments for it */
					result = func_pointer ();
					break;
				case 1:
					result = func_pointer (argv[0]);
					break;
				case 2:
					result = func_pointer (argv[0], argv[1]);
					break;
				case 3:
					result = func_pointer (argv[0], argv[1], argv[2]);
					break;
				default:
					/* TODO: Error for invalid Number of Arguments */
					break;
			}

			/* Store the Result in f->eax */
			f->eax = *(uint32_t *) result;
		}
	}
	/* TODO: Remove (Or make sure it only happens if there's an Error) */
  thread_exit ();
}
