#include "../userprog/syscall.h"
#include "../userprog/syscall-func.h"
#include "../userprog/memory-access.h"

static void syscall_handler (struct intr_frame *);
static void syscall_execute_function (int32_t syscall_no, struct intr_frame *if_);

/* Look-up table for syscall functions */
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
		[SYS_MMAP]     = syscall_mmap,
		[SYS_MUNMAP]   = syscall_munmap,
};

void
syscall_init (void)
{
	intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_execute_function (int32_t syscall_no, struct intr_frame *if_)
{
	void* (*func_pointer) (struct intr_frame *if_) = system_call_function[syscall_no];
	func_pointer (if_);
}

static void
syscall_handler (struct intr_frame *if_)
{
	/* Verification of user provided pointer happens within get_user_safe(), called by get_syscall_no(). */
	int32_t syscall_no = get_syscall_no(if_);
	/* Remove page-dir check and modify page_fault() in exception.c to catch invalid user pointers. */
	void *page = pagedir_get_page (thread_current ()->pagedir, if_->frame_pointer);

	if (syscall_no != ERROR && page != NULL)
	{
		/* De-reference frame pointer. */
		if_->frame_pointer = (void *) (*(uint32_t *) if_->frame_pointer);

		/* Save stack pointer onto thread, in case of stack growth. */
		thread_current ()->saved_esp = if_->esp;

		/* Execute corresponding syscall function. */
		syscall_execute_function (syscall_no, if_);
	}
	else
	{
		/* Terminate user process with ERROR code. */
		terminate_userprog (ERROR);
	}
}

/* Terminates a user process with given status. */
void
terminate_userprog (int status)
{
	struct thread *cur = thread_current();

	/* Send exit status to kernel. */
	cur->rs_manager->exit_status = status;
	cur->rs_manager->running = false;

	/* Print termination message. */
	printf ("%s: exit(%d)\n", cur->name, status);

	/* Terminate current process. */
	thread_exit ();
}
