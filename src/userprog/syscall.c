#include "../userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "../threads/interrupt.h"
#include "../threads/thread.h"

/* Terminates by calling shutdown_power_off()
   Seldom used because you lose information about possible deadlock situations. */
void
halt (void)
{
	shutdown_power_off ();
}

/* Terminates the current user program, sending its exit status to the kernel. If the process’s
   parent waits for it (see below), this is the status that will be returned.
   Conventionally, a status of 0 indicates success and nonzero values indicate errors. */
void
exit (int status)
{
	/* TODO */
}

/* Runs the executable whose name is given in cmd line, passing any given arguments, and
   returns the new process’s program id (pid). Must return pid -1, which otherwise should not
   be a valid pid, if the program cannot load or run for any reason. */
pid_t
exec (const char *file)
{
	/* TODO */
}

/* Waits for a child process pid and retrieves the child’s exit status. */
int
wait (pid_t pid)
{
	/* TODO */
}

bool
create (const char *file, unsigned initial_size)
{
	/* TODO */
}

bool
remove (const char *file)
{
	/* TODO */
}

int
open (const char *file)
{
	/* TODO */
}

int
filesize (int fd)
{
	/* TODO */
}

int
read (int fd, void *buffer, unsigned size)
{
	/* TODO */
}

int
write (int fd, const void *buffer, unsigned size)
{
	/* TODO */
}

void
seek (int fd, unsigned position)
{
	/* TODO */
}

unsigned
tell (int fd)
{
	/* TODO */
}

void
close (int fd)
{
	/* TODO */
}

/* Array of function pointers for system calls for efficient function lookup. */
void (*system_call_function[])(void) = {
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
static void setup_args(void *esp, int *syscall_no, int *argc, char **argv) {
	*syscall_no = *(int *)esp;
	*argc = *((int *) (esp + sizeof(int)) );
	/* TODO: Check Syntax */
	/* TODO: Make clean ways of reading and writing data to virtual memory */

	for (int i = 0; i < *argc; i++) {
		/* Increment esp, retrieve current argument and add to argv array. */
		esp += sizeof(char *);
		/* Store the pointer to the current argument in argv[i]. */
		argv[i] = *(char **)esp;
	}
}


static void
syscall_handler (struct intr_frame *f)
{
	/* Verification of user provided pointers, and dereferences. */
	/* TODO: Change to Method 2 */
	if (is_user_vaddr(f))
	{
		if (pagedir_get_page(thread_current ()->pagedir, f->frame_pointer) != NULL)
		{
			/* De-reference frame pointer. */
			f->frame_pointer = (void *) (*(uint32_t *)f->frame_pointer);

			/* Initialise and setup arguments. */
			char **argv = (char **)(f->esp + sizeof(int) * 2); /* Increments by 4 bytes */
			int syscall_no;
			int *argc;
			setup_args(f->esp, &syscall_no, argc, argv);
			(void *) result;

			/* Basic Syscall Handler - Each case calls the specific function for the specified syscall */
			void *func_pointer = system_call_function[syscall_no];
			switch (argc) {
				case 0:
					result = func_pointer();
				case 1:
					result = func_pointer(argv[0]);
				case 2:
					result = func_pointer(argv[0], argv[1]);
				case 3:
					result = func_pointer(argv[0], argv[1], argv[2]);
				default:
					/* TODO: Error for invalid Number of Arguments */
			}

			/* Store the Result in f->eax */
			f->eax = result;
		}
		/* TODO: Implement Process Termination Messages */
	}
	/* TODO: Remove (Or make sure it only happens if there's an Error) */
  thread_exit ();
}


