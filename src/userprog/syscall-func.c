#include <stdlib.h>
#include "../lib/string.h"
#include "../userprog/syscall-func.h"

static void halt (void);
static void exit (int status);
static pid_t exec (const char *file);
static int wait (pid_t pid);
static bool create (const char *file, unsigned initial_size);
static bool remove (const char *file);
static int open (const char *file);
static int filesize (int fd);
static int read (int fd, void *buffer, unsigned size);
static int write (int fd, const void *buffer, unsigned size);
static void seek (int fd, unsigned position);
static unsigned tell (int fd);
static void close (int fd);

/* Terminates by calling shutdown_power_off(). Seldom used because
	 you lose information about possible deadlock situations. */
static void
halt (void)
{
	shutdown_power_off ();
}

/* Terminates the current user program, sending its exit status to
   the kernel. Conventionally, a status of 0 indicates success and
	 nonzero values indicate errors. */
static void
exit (int status)
{
	terminate_userprog (status);
}

/* Runs the executable whose name is given in cmd line, passing any
 	 given arguments, and returns the new process’s program id (pid).
	 Must return pid -1, which otherwise should not be a valid pid, if
	 the program cannot load or run for any reason. */
static pid_t
exec (const char *cmd_line)
{
  if (get_user_safe(cmd_line) == ERROR)
	{
		terminate_userprog (ERROR);
	}

	tid_t tid = process_execute (cmd_line);

	if (tid == TID_ERROR)
	{
		// printf ("(exec) ERROR cannot create child process\n");
		return ERROR;
	}

	/* Find newly created child process and decrement child_load_sema. */
	struct rs_manager *child_rs_manager = get_child (thread_current (), tid);

	if (child_rs_manager == NULL)
	{
		printf ("(exec) ERROR newly created child process not found\n");
		return ERROR;
	}

	/* Continues (and returns) only after child process has loaded successfully
		 (or failed load). */
	sema_down (&child_rs_manager->child_load_sema);

	/* Return TID if child process loaded succesfully, else -1. */
	if (child_rs_manager->load_success)
	{
		return tid;
	}
	else 
	{
		return ERROR;
	}

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
	if (get_user_safe(file) == ERROR)
	{
		terminate_userprog (ERROR);
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
	if (file == NULL || *file == '\0')
	{
		return false;
	}

	lock_acquire (&filesys_lock);
	bool result = filesys_remove (file);
	lock_release (&filesys_lock);

	return result;
}

static int
open (const char *file_name)
{
	if (get_user_safe(file_name) == ERROR)
	{
		return ERROR;
	}

	lock_acquire (&filesys_lock);
	struct file *file = filesys_open (file_name);
	lock_release (&filesys_lock);

	/* Return error if file could not be opened. */
	if (file == NULL)
	{
		return ERROR;
	}

	struct rs_manager *rs = thread_current ()->rs_manager;

	/* Dynamically allocate the file entry. */
	struct file_entry *entry = malloc (sizeof (struct file_entry));

	if (entry == NULL)
	{
		return ERROR;
	}

	/* Set the file entry attributes. */
	entry->file = file;
	strlcpy (entry->file_name, file_name, MAX_CMDLINE_LEN);

	/* Get file descriptor and increment fd_next for next file descriptor. */
	entry->fd = rs->fd_next++;

	/* Add file and corresponding fd to process's hash table. */
	lock_acquire (&rs->file_table_lock);
	hash_insert (&rs->file_table, &entry->file_elem);
	lock_release (&rs->file_table_lock);

	return entry->fd;
}

static int
filesize (int fd)
{
	struct file *file = file_entry_lookup (fd)->file;

	lock_acquire (&filesys_lock);
	int result = file_length (file);
	lock_release (&filesys_lock);

	return result;
}

static int
read (int fd, void *buffer, unsigned size)
{
	if (get_user_safe(buffer) == ERROR)
	{
		terminate_userprog(ERROR);
	}

	if (fd == STDOUT_FILENO)
	{
		/* Cannot read from standard output. */
		return;
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
		struct file *file = file_entry_lookup (fd)->file;

		if (file == NULL)
		{
			return ERROR;
		}

		lock_acquire (&filesys_lock);
		int result = file_read (file, buffer, size);
		lock_release (&filesys_lock);

		return result;
	}
}

static int
write (int fd, const void *buffer, unsigned size)
{
	/* Validate the buffer pointer before trying to write. */
	if (get_user_safe (buffer) == ERROR)
	{
		return ERROR;
	}
	
	if (fd == STDIN_FILENO)
	{
		/* Cannot write to standard input; return 0 (number of bytes read). */
		// printf ("(write) ERROR: cannot write to STDIN\n");
		return SUCCESS;
		// exit (ERROR);

	} 
	else if (fd == STDOUT_FILENO)
	{
		/* Write to standard output. */
		int i = size;
		if (size > MAX_BYTES_PUTBUF)
		{
			/* Write in chunks to avoid stack overflow. */
			while (i > MAX_BYTES_PUTBUF)
			{
				putbuf (buffer, MAX_BYTES_PUTBUF);
				buffer += MAX_BYTES_PUTBUF;
				i -= MAX_BYTES_PUTBUF;
			}
		}

		putbuf (buffer, i);
		// printf ("(write) SUCCESS: %d bytes written \n", size);	
		return size;

	} 
	else
	{
		struct file_entry *entry = file_entry_lookup (fd);

		if (entry == NULL)
		{
			/* Return 0 (for number of bytes read). */
			// printf ("(write) ERROR: file could not be opened\n");
			return SUCCESS;
		}

		char *exe_name = thread_current ()->rs_manager->exe_name;

		lock_acquire (&filesys_lock);

		// printf ("(write) %s and %s\n", entry->file_name, exe_name);
		/* Deny writes if file name is the same as the current process exe_name. */
		if (strcmp (entry->file_name, exe_name) == 0)
		{
			// printf ("(write) cannot write to executable file\n");
			// printf ("(write) %s DENY WRITE\n", entry->file_name);
			file_deny_write (entry->file);
		} else 
		{
			// printf ("(write) %s is not %s's executable file\n", entry->file_name, exe_name);
			// printf ("(write) %s ALLOW WRITE\n", entry->file_name);
			file_allow_write (entry->file);
		}

		int result = (int) file_write (entry->file, buffer, size);
		lock_release (&filesys_lock);

		// printf ("(write) SUCCESS: %d bytes written to %s\n", result, exe_name);	

		return result;
	}
}

static void
seek (int fd, unsigned position)
{
	struct file *file = file_entry_lookup (fd)->file;

	lock_acquire (&filesys_lock);
	file_seek (file, (off_t) position);
	lock_release (&filesys_lock);
}

static unsigned
tell (int fd)
{
	struct file *file = file_entry_lookup (fd)->file;

	lock_acquire (&filesys_lock);
	unsigned result = (unsigned) file_tell (file);
	lock_release (&filesys_lock);

	return result;
}

static void
close (int fd)
{
	
	struct rs_manager *rs = thread_current ()->rs_manager;
	struct file_entry *file_entry = file_entry_lookup (fd);

	if (file_entry == NULL)
	{
		// printf ("(close) reached 3\n");
		// terminate_userprog (ERROR);
		return;
	}

	/* Remove entry from table. */
	lock_acquire (&rs->file_table_lock);
	hash_delete (&rs->file_table, &file_entry->file_elem);
	lock_release (&rs->file_table_lock);

	lock_acquire (&filesys_lock);
	// printf ("(write) %s ALLOW WRITE ON CLOSE\n", file_entry->file_name);
	file_close (file_entry->file);
	lock_release (&filesys_lock);

	free (file_entry);
}



/* Syscall Helper Functions */

void
syscall_halt (struct intr_frame *if_ UNUSED)
{
	halt ();
}

void
syscall_exit (struct intr_frame *if_)
{
	/* Retrieve status from if_ or an argv array */
	int status = syscall_get_arg (if_, 1);
	exit (status);
}

void
syscall_exec (struct intr_frame *if_)
{
	/* Retrieve cmd_line from if_ or an argv array */
	char *cmd_line = syscall_get_arg (if_, 1);

	/* Get result and store it in if_->eax */
	pid_t result = exec (cmd_line);
	if_->eax = result;
}

void
syscall_wait (struct intr_frame *if_)
{
	/* Retrieve pid from if_ or an argv array */
	pid_t pid = syscall_get_arg (if_, 1);

	/* Get result and store it in if_->eax */
	int result = wait (pid);
	if_->eax = result;
}

void
syscall_create (struct intr_frame *if_)
{
	/* Retrieve file, initial_size from if_ or an argv array */
	char *file = syscall_get_arg (if_, 1);
	unsigned initial_size = syscall_get_arg (if_, 2);

	/* Get result and store it in if_->eax */
	bool result = create (file, initial_size);
	if_->eax = result;
}

void
syscall_remove (struct intr_frame *if_)
{
	/* Retrieve file from if_ or an argv array */
	char *file = syscall_get_arg (if_, 1);

	/* Get result and store it in if_->eax */
	int result = remove (file);
	if_->eax = result;
}

void
syscall_open (struct intr_frame *if_)
{
	/* Retrieve file from if_ or an argv array */
	char *file = syscall_get_arg (if_, 1);

	/* Get result and store it in if_->eax */
	int result = open (file);
	if_->eax = result;
}

void
syscall_filesize (struct intr_frame *if_)
{
	/* Retrieve fd from if_ or an argv array */
	int fd = syscall_get_arg (if_, 1);

	/* Get result and store it in if_->eax */
	int result = filesize (fd);
	if_->eax = result;
}

void
syscall_read (struct intr_frame *if_)
{
	/* Retrieve fd, buffer, size from if_ or an argv array */
	int fd = syscall_get_arg (if_, 1);
	void *buffer = syscall_get_arg (if_, 2);
	unsigned size = syscall_get_arg (if_, 3);

	/* Get result and store it in if_->eax */
	int result = read (fd, buffer, size);
	if_->eax = result;
}

void
syscall_write (struct intr_frame *if_)
{
	/* Retrieve fd, buffer, size from if_ or an argv array */
	int fd = syscall_get_arg (if_, 1);
	void *buffer = syscall_get_arg (if_, 2);
	unsigned size = syscall_get_arg (if_, 3);

	/* Get result and store it in if_->eax */
	int result = write (fd, buffer, size);
	if_->eax = result;
}

void
syscall_seek (UNUSED struct intr_frame *if_)
{
	int fd = syscall_get_arg (if_, 1);
	unsigned position = syscall_get_arg (if_, 2);

	/* Execute seek syscall with arguments. */
	seek (fd, position);
}

void
syscall_tell (struct intr_frame *if_)
{
	/* Retrieve fd from if_ or an argv array. */
	int fd = syscall_get_arg (if_, 1);

	/* Get result and store it in if_->eax. */
	unsigned result = tell (fd);
	if_->eax = result;
}

void
syscall_close (UNUSED struct intr_frame *if_)
{
	/* Retrieve fd from if_ or an argv array. */
	int fd = syscall_get_arg (if_, 1);
	
	close (fd);
}
