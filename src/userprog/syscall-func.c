#include "../userprog/syscall-func.h"
#include "../userprog/syscall.h"
#include "../threads/synch.h"
#include "../threads/malloc.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "process.h"
#include "../vm/mmap.h"
#include "../vm/spt-entry.h"

static void store_result (struct intr_frame *if_, uintptr_t result);
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
static mapid_t mmap (int fd, void *addr);
static void munmap (mapid_t mapping);

static void
store_result (struct intr_frame *if_, uintptr_t result)
{
	if_->eax = result;
}

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
  if (get_user_safe ((uint8_t *) cmd_line) == ERROR)
	{
		terminate_userprog (ERROR);
	}

	tid_t tid = process_execute (cmd_line);

	if (tid == TID_ERROR)
	{
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
	if (get_user_safe ((uint8_t *) file) == ERROR)
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

/* Opens the file called file. 
	
	 Returns a nonnegative integer handle called a “file descriptor” (fd), 
	 or -1 if the file could not be opened. */
static int
open (const char *file_name)
{
	if (get_user_safe ((uint8_t *) file_name) == ERROR)
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

	/* Add file and corresponding fd to process's hash table. */
	lock_acquire (&rs->file_table_lock);
		/* Get file descriptor and increment fd_next for next file descriptor. */
		entry->fd = rs->fd_next++;
		hash_insert (&rs->file_table, &entry->file_elem);
	lock_release (&rs->file_table_lock);

	return entry->fd;
}

/* Returns the size, in bytes, of the file open as fd. */
static int
filesize (int fd)
{
	struct file *file = file_entry_lookup (fd)->file;

	if (file == NULL)
	{
		return ERROR;
	}

	lock_acquire (&filesys_lock);
	int result = file_length (file);
	lock_release (&filesys_lock);

	return result;
}

/* Reads size bytes from the file open as fd into buffer. 

	 Returns the number of bytes actually read (0 at end of file), or -1 if the
	 file could not be read (due to a condition other than end of file). */
static int
read (int fd, void *buffer, unsigned size)
{
	unsigned i = 0;
	while (i < size)
	{
		unsigned remaining = size - i;
		unsigned chunk_size = remaining < PGSIZE ? remaining : PGSIZE;

		if (get_user_safe ((uint8_t *) (buffer + i)) == ERROR)
		{
			terminate_userprog (ERROR);
		}
		i += chunk_size;
	}

	/* Cannot read from standard output. */
	if (fd == STDOUT_FILENO)
	{
		return ERROR;
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
	else	/* Can read from file. */
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

/* Writes size bytes from buffer to the open file fd. 
	
	 Returns the number of bytes actually written, which may be less 
	 than size if some bytes could not be written. */
static int
write (int fd, const void *buffer, unsigned size)
{
	/* Terminate process if buffer pointer is invalid. */
	unsigned i = 0;
	while (i < size)
	{
		unsigned remaining = size - i;
		unsigned chunk_size = remaining < PGSIZE ? remaining : PGSIZE;

		if (get_user_safe ((uint8_t *) (buffer + i)) == ERROR)
		{
			terminate_userprog (ERROR);
		}

		/* Check page is writable if FD is not STDIN or STDOUT. */
		if (!fd == STDIN_FILENO && !fd == STDOUT_FILENO)
		{
			void *upage = pg_round_down (buffer + i);
			struct spt_entry *spte = spt_entry_lookup (upage);

			if (spte != NULL && !spte->writable)
			{
				terminate_userprog (ERROR);
			}
		}

		i += chunk_size;
	}
	
	if (fd == STDIN_FILENO)
	{
		/* Cannot write to standard input; return 0 (number of bytes read). */
		return ERROR;

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
		return size;
	} 
	else
	{
		/* Check FD in file descriptor table. */
		struct file_entry *entry = file_entry_lookup (fd);

		if (entry == NULL)
		{
			return ERROR;
		}

		char *exe_name = thread_current ()->rs_manager->exe_name;

		lock_acquire (&filesys_lock);

			/* Deny writes if file name is the same as the current process exe_name. */
			if (strcmp (entry->file_name, exe_name) == 0)
			{
				file_deny_write (entry->file);
			} else 
			{
				file_allow_write (entry->file);
			}
			int result = (int) file_write (entry->file, buffer, size);

		lock_release (&filesys_lock);

		return result;
	}
}

/* Changes the next byte to be read or written in open file fd to position, 
   expressed in bytes from the beginning of the file.

	 (Thus, a position of 0 is the file’s start.) */
static void
seek (int fd, unsigned position)
{
	struct file *file = file_entry_lookup (fd)->file;

	if (file == NULL)
	{
		return;
	}

	lock_acquire (&filesys_lock);
	file_seek (file, (off_t) position);
	lock_release (&filesys_lock);
}

/* Returns the position of the next byte to be read or written in open file 
	 fd, expressed in bytes from the beginning of the file. */
static unsigned
tell (int fd)
{
	struct file *file = file_entry_lookup (fd)->file;

	if (file == NULL)
	{
		return SUCCESS;
	}

	lock_acquire (&filesys_lock);
	unsigned result = (unsigned) file_tell (file);
	lock_release (&filesys_lock);

	return result;
}

/* Closes file descriptor fd. */
static void
close (int fd)
{
	struct rs_manager *rs = thread_current ()->rs_manager;
	struct file_entry *file_entry = file_entry_lookup (fd);

	/* Check validity of file_entry pointer. */
	if (file_entry == NULL)
	{
		return;
	}

	/* Remove entry from table. */
	lock_acquire (&rs->file_table_lock);
		hash_delete (&rs->file_table, &file_entry->file_elem);
	lock_release (&rs->file_table_lock);

	lock_acquire (&filesys_lock);
		file_close (file_entry->file);
	lock_release (&filesys_lock);

	free (file_entry);
}

/* Maps the file open as fd into the process's virtual address space. 
	 The entire file is mapped into consecutive virtual pages starting at addr.

	 Returns a mapping id that uniquely identifies the mapping within the process.
	 On failure, returns -1. */
static mapid_t
mmap (int fd, void *addr)
{
	struct file_entry *file_entry = file_entry_lookup (fd);

	if (file_entry == NULL || addr == NULL || pg_ofs (addr) != 0)
	{
		return ERROR;
	}

	mapid_t mapping = mmap_create (file_entry, addr);

	return mapping;
}

/* Unmaps the mapping designated by mapping, which must be a mapping ID 
	 returned by a previous call to mmap by the same process that has not 
	 yet been unmapped.*/
static void 
munmap (mapid_t mapping)
{
	struct file_entry *file = file_entry_lookup ((int) mapping);

	if (file == NULL)
	{
		return;
	}

	mmap_destroy (file);
}

/* Syscall Helper Functions */

void
syscall_halt (struct intr_frame *if_ UNUSED)
{
	/* Execute halt syscall. */
	halt ();
}

void
syscall_exit (struct intr_frame *if_)
{
	/* Retrieve status from if_. */
	int status = (int) syscall_get_arg (if_, 1);

	/* Execute exit syscall. */
	exit (status);
}

void
syscall_exec (struct intr_frame *if_)
{
	/* Retrieve cmd_line from if_. */
	char *cmd_line = (char *) syscall_get_arg (if_, 1);

	/* Execute exec syscall, get result and store it in if_->eax */
	store_result (if_, (uintptr_t) exec (cmd_line));
}

void
syscall_wait (struct intr_frame *if_)
{
	/* Retrieve pid from if_. */
	pid_t pid = (pid_t) syscall_get_arg (if_, 1);

	/* Execute wait syscall, get result and store it in if_->eax */
	store_result (if_, (uintptr_t) wait (pid));
}

void
syscall_create (struct intr_frame *if_)
{
	/* Retrieve file, initial_size from if_. */
	char *file = (char *) syscall_get_arg (if_, 1);
	unsigned initial_size = (unsigned) syscall_get_arg (if_, 2);

	/* Execute create syscall, get result and store it in if_->eax */
	store_result (if_, (uintptr_t) create (file, initial_size));
}

void
syscall_remove (struct intr_frame *if_)
{
	/* Retrieve file from if_. */
	char *file = (char *) syscall_get_arg (if_, 1);

	/* Execute remove syscall, get result and store it in if_->eax */
	store_result (if_, (uintptr_t) remove (file));
}

void
syscall_open (struct intr_frame *if_)
{
	/* Retrieve file from if_. */
	char *file = (char *) syscall_get_arg (if_, 1);

	/* Execute open syscall, get result and store it in if_->eax */
	store_result (if_, (uintptr_t) open (file));
}

void
syscall_filesize (struct intr_frame *if_)
{
	/* Retrieve fd from if_. */
	int fd = (int) syscall_get_arg (if_, 1);

	/* Execute filesize syscall, get result and store it in if_->eax */
	store_result (if_, (uintptr_t) filesize (fd));
}

void
syscall_read (struct intr_frame *if_)
{
	/* Retrieve fd, buffer, size from if_. */
	int fd = (int) syscall_get_arg (if_, 1);
	void *buffer = (void *) syscall_get_arg (if_, 2);
	unsigned size = (unsigned) syscall_get_arg (if_, 3);

	/* Execute read syscall, get result and store it in if_->eax */
	store_result (if_, (uintptr_t) read (fd, buffer, size));
}

void
syscall_write (struct intr_frame *if_)
{
	/* Retrieve fd, buffer, size from if_. */
	int fd = (int) syscall_get_arg (if_, 1);
	void *buffer = (void *) syscall_get_arg (if_, 2);
	unsigned size = (unsigned) syscall_get_arg (if_, 3);

	/* Execute write syscall, get result and store it in if_->eax */
	store_result (if_, (uintptr_t) write(fd, buffer, size));
}

void
syscall_seek (struct intr_frame *if_ UNUSED)
{
	/* Retrieve fd, position from if_. */
	int fd = (int) syscall_get_arg (if_, 1);
	unsigned position = (unsigned) syscall_get_arg (if_, 2);

	/* Execute seek syscall. */
	seek (fd, position);
}

void
syscall_tell (struct intr_frame *if_)
{
	/* Retrieve fd from if_. */
	int fd = (int) syscall_get_arg (if_, 1);

	/* Execute tell syscall, get result and store it in if_->eax. */
	store_result (if_, (uintptr_t) tell (fd));
}

void
syscall_close (struct intr_frame *if_ UNUSED)
{
	/* Retrieve fd from if_. */
	int fd = (int) syscall_get_arg (if_, 1);
	
	/* Execute close syscall . */
	close (fd);
}

void
syscall_mmap (struct intr_frame *if_)
{
	/* Retrieve fd, addr from if_. */
	int fd = (int) syscall_get_arg (if_, 1);
	void *addr = (void *) syscall_get_arg (if_, 2);

	/* Execute mmap syscall, get result and store it in if_->eax. */
	store_result (if_, (uintptr_t) mmap (fd, addr));
}

void
syscall_munmap (struct intr_frame *if_)
{
	/* Retrieve mapping from if_. */
	mapid_t mapping = (mapid_t) syscall_get_arg (if_, 1);

	/* Execute munmap syscall . */
	munmap (mapping);
}