#include "../userprog/memory-access.h"

#define SYS_MIN SYS_HALT  	/* Minimum system call number. */
#define SYS_MAX SYS_CLOSE 	/* Maximum system call number. */

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
int
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
bool
put_user (uint8_t *udst, uint8_t byte)
{
	int error_code;
	asm ("movl $1f, %0; movb %b2, %1; 1:"
			: "=&a" (error_code), "=m" (*udst) : "q" (byte));
	return error_code != -1;
}

/* Checks if UADDR is valid (Points below PHYS_BASE) and retrieves if it is.
   Returns -1 otherwise. */
int
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
bool
put_user_safe (uint8_t *udst, uint8_t byte)
{
	if (is_user_vaddr (udst))
		return put_user (udst, byte);
	return false;
}

/* Retrieves a word from UADDR and returns -1 otherwise. */
int32_t
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
int32_t
get_syscall_no (struct intr_frame *if_)
{
	int32_t syscall_no = get_user_word_safe ((uint8_t *) if_->esp);
	if (syscall_no < SYS_MIN || syscall_no > SYS_MAX)
		syscall_no = ERROR;
	return syscall_no;
}

/* Returns argc for a given interrupt frame if_, -1 if not successful */
int
get_argc (struct intr_frame *if_)
{
	int32_t argc = get_user_word_safe ((uint8_t *) if_->esp + WORD_SIZE);
	if (argc < 0)
		argc = ERROR;
	return argc;
}

/* Retrieves argument arg_num from argv array. */
int32_t
syscall_get_arg (struct intr_frame *if_, int arg_num)
{
	int32_t arg =
			get_user_word_safe ((uint8_t *) if_->esp + (WORD_SIZE * (arg_num)));
	// printf("(syscall-get-arg) integer argument %d: %d for %s\n", arg_num,
	// 			 arg, thread_current ()->name);
	return arg;
}

/* Checks if if_ contains an argument at arg_num */
bool
syscall_invalid_arg (struct intr_frame *if_, int arg_num)
{
	return syscall_get_arg (if_, arg_num) == ERROR;
}

/* Populates argv array. If at any point an argument is invalid
	 (i.e. not enough arguments provided), returns false */
bool
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