#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Sets up Arguments */
static void setup_args(void *esp) {
	int syscall_no = *(int *)esp;
	int argc = *((int *)esp + sizeof(int));   // 4 byte intervals

	/* Need to check syntax for this*/
	char **argv = *(char **)(&argc + sizeof(char**));
	for (int i = 0; i < argc; i++)
	{
		/* need to increment esp here and get arguments */

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

			setup_args(f->esp);

		}
	}

	printf ("system call!\n");
  thread_exit ();
}
