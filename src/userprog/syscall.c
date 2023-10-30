#include "../userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "../threads/interrupt.h"
#include "../threads/thread.h"
#include "../lib/user/syscall.h"

// Array of function pointers for system calls for efficient Function Lookup
void (*system_call_function[])(void) = {
		[SYS_HALT] = halt,
		[SYS_EXIT] = exit,
		/* Casting to appropriate function pointer type */
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

/* Sets up Arguments */
static void setup_args(void *esp, int* syscall_no, int* argc, char** argv) {
	syscall_no = (int *)esp;
	argc = *((int *) (esp + sizeof(int)) );
	/* TODO: Check Syntax */

	for (int i = 0; i < argc; i++) {
		// Increment esp, Retrieve Current Argument and Add to argv Array
		esp += sizeof(char *);
		argv[i] = *(char **)esp;  // Store the pointer to the current argument in argv[i]
	}
}


static void
syscall_handler (struct intr_frame *f)
{
	/* Verification of User Provided Pointers, and Dereferences */
	/* TODO: Change to Method 2 */
	if (is_user_vaddr(f))
	{
		if (pagedir_get_page(thread_current ()->pagedir, f->frame_pointer) != NULL)
		{
			/* De-Referencing Pointer */
			f->frame_pointer = (void *) (*(uint32_t *)f->frame_pointer);

			/* Retrieve Arguments */
			char** argv = (char **)(f->esp + sizeof(int) * 2); /* Increments by 4 bytes */
			int syscall_no;
			int argc;
			setup_args(f->esp, &syscall_no, &argc, argv);
			(void *) result;
			/* Basic Syscall Handler - Each case calls the specific function for the specified syscall */
			switch (argc) {
				case 0:
					result = system_call_function[syscall_no]();
				case 1:
					result = system_call_function[syscall_no](argv[0]);
				case 2:
					result = system_call_function[syscall_no](argv[0], argv[1]);
				case 3:
					result = system_call_function[syscall_no](argv[0], argv[1], argv[2]);
				default:
					/* TODO: Error for invalid Number of Arguments */
			}

			/* TODO: Store the Result in f->eax */
			f->eax = result;
		}
	}
	/* TODO: Remove (Or make sure it only happens if there's an Error) */
  thread_exit ();
}


