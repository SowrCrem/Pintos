#include "../userprog/syscall.h"
#include "../userprog/syscall-func.h"
#include "../userprog/memory-access.h"

static void syscall_handler (struct intr_frame *);
static intptr_t syscall_execute_function (int32_t syscall_no, struct intr_frame *if_);

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

/* Initialises system call handler */
void
syscall_init (void)
{
	intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Executes system calls given state of registers */
static intptr_t
syscall_execute_function (int32_t syscall_no, struct intr_frame *if_)
{
	intptr_t* (*func_pointer) (struct intr_frame *if_) = system_call_function[syscall_no];
	return func_pointer (if_);
}


/* Handles system calls. */
static void
syscall_handler (struct intr_frame *if_)
{
	/* Get syscall number, verification of if_ pointer happens behind the scenes. */
	int32_t syscall_no = get_syscall_no(if_);

	/* Terminate if syscall number is invalid */
	if (syscall_no == ERROR)
		terminate_userprog(ERROR);

	/* De-reference frame pointer. */
	if_->frame_pointer = (void *) (intptr_t) (*(uint32_t *) if_->frame_pointer);

	/* Retrieve result by executing corresponding syscall function. */
	intptr_t result = syscall_execute_function (syscall_no, if_);

	/* Store result if not void. */
	if ((syscall_no != SYS_HALT) && (syscall_no != SYS_EXIT) && (result != VOID_SYSCALL_ERROR))
		if_->eax = result;
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
