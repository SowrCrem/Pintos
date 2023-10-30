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

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
	/* Verification of User Provided Pointers, and Dereferences */
	/* TODO: Change to Method 2 */
	if (is_user_vaddr(f))
	{
		if (pagedir_get_page(thread_current ()->pagedir, f->frame_pointer) != NULL)
		{
			f->frame_pointer = * (void *)f->frame_pointer;
		}
	}

	printf ("system call!\n");
  thread_exit ();
}
