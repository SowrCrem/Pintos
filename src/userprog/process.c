#include "../userprog/process.h"
#include "../userprog/gdt.h"
#include "../userprog/pagedir.h"
#include "../userprog/tss.h"
#include "../filesys/directory.h"
#include "../filesys/file.h"
#include "../filesys/filesys.h"
#include "../threads/flags.h"
#include "../threads/init.h"
#include "../threads/interrupt.h"
#include "../threads/palloc.h"
#include "../threads/thread.h"
#include "../threads/vaddr.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../lib/string.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

/* 
   Returns the number of arguments in the COMMAND_LINE string.
   Arguments are separated by spaces. Multiple spaces are treated as a single space.
*/
static int get_no_of_args(const char *command_line) 
{
  int no_chars = 1;
  int no_args = 0;
  char *cur = command_line;

  bool in_space = false;

  /* Iterate through each character in the command line. */
  while (*cur != '\0') 
  {
    // Check if the current character is not a space
    if (*cur != ' ') 
    {
      no_chars++;

      if (!in_space) 
      {
        no_args++;
        in_space = true; 
      }
    } 
    else 
    {
      in_space = false;
    }

    cur = (++command_line);
  }

  return no_args;
}

/* Tokenizes COMMAND_LINE into words. */
static void parse_arguments (const char *command_line, char **args, int argc)
{
	char *token;
	char *save_ptr;
	int i = 0;  /* Initialise argument count. */

	/* Split the command line into words. */
	for (token = strtok_r ((char *) command_line, " ", &save_ptr);
	     token != NULL; token = strtok_r(NULL, " ", &save_ptr))
	{
		int len = strlen (token) + 1;
		args[i] = malloc (len * sizeof (char));
		strlcpy (args[i], token, len);
		i++;
	}

	args[argc] = NULL; /* Set the last argument to NULL. */
}

/* Returns hash value for file FILE, which is just a file's FD as it is unique
   to the file. */
unsigned
file_table_hash (const struct hash_elem *e, void *aux UNUSED)
{
	const struct file_entry *e_f = hash_entry (e, struct file_entry, file_elem);
	return e_f->fd;
}

/* Returns true if entry X precedes entry Y in file table. */
bool
file_table_less (const struct hash_elem *a, const struct hash_elem *b,
                 void *aux UNUSED)
{
	const struct file_entry *a_f = hash_entry (a, struct file_entry, file_elem);
	const struct file_entry *b_f = hash_entry (b, struct file_entry, file_elem);

	return a_f->fd < b_f->fd;
}

/* Closes the file and frees the file_entry corresponding to hash_elem E. */
void
file_table_destroy_func (struct hash_elem *e_, void *aux UNUSED)
{
  ASSERT (lock_held_by_current_thread (&filesys_lock));
  
  struct file_entry *e = hash_entry (e_, struct file_entry, file_elem);

	/* Implicitly ALLOW WRITE*/
	// printf ("(file_table_destroy_func) %s ALLOW WRITE ON CLOSE\n", e->file_name);
  file_close (e->file);
  free (e);
}

/* Returns file_entry pointer for corresponding fd.
   Returns NULL if not found */
struct file_entry *
file_entry_lookup (int fd)
{

	struct hash table = thread_current ()->rs_manager->file_table;
	struct hash_elem *e;

	/* Get file from fd value. */
	struct file_entry entry;
	entry.fd = fd;

	e = hash_find (&table, &entry.file_elem);

	if (e == NULL)
	{
		return NULL;
	}

	return hash_entry (e, struct file_entry, file_elem);
}

/* Returns pointer to child rs_manager, given parent process pointer
   and child process TID.
   Returns NULL if not found. */
struct rs_manager*
get_child (struct thread *parent, tid_t child_tid)
{

	struct list *children = &parent->rs_manager->children;

	if (list_empty (children))
	{
		return NULL;
	}

	/* Iterate through parent rs_manager's list of children. */
	struct rs_manager *child_rs_manager = NULL;
	struct list_elem *e;
	for (e = list_begin (children); e != list_end (children);
	     e = list_next (e))
	{
		child_rs_manager = list_entry (e, struct rs_manager, child_elem);
		/* Match corresponding tid to the child thread. */
		if (child_rs_manager->tid == child_tid)
			break;
		child_rs_manager = NULL;
	}

	return child_rs_manager;
}


/* Initializes thread CHILD's rs_manager as a child of PARENT's
   rs_manager.
   Requires PARENT's rs_manager pointer and CHILD thread pointer. */
void
rs_manager_init (struct rs_manager *parent, struct thread *child)
{
	/* Allocate space on the heap for the parent. */
	struct rs_manager *rs = malloc (sizeof (struct rs_manager));

	rs->parent_rs_manager = parent;
	/* Push child onto parent's children list, if parent present. */
	if (parent != NULL)
		list_push_back (&parent->children, &rs->child_elem);

	list_init (&rs->children);

	rs->thread = child;        
	rs->tid = child->tid;

	hash_init (&rs->file_table, file_table_hash, file_table_less, NULL);
	lock_init (&rs->file_table_lock);
	rs->fd_next = FD_START;

	sema_init (&rs->child_load_sema, 0);
	sema_init (&rs->child_exit_sema, 0);

	rs->exit_status = NOT_EXITED;
	rs->running = true;
	rs->load_success = true;

	/* Update child rs_manager pointer. */
	child->rs_manager = rs;
}

/* Runs on process exit. Frees rs_manager RS's associated memory if no
   other references to RS are found.  */
void
rs_manager_free (struct rs_manager *rs)
{
	/* Increment semaphore to allow parent to return from wait. */
	sema_up (&rs->child_exit_sema);

	if (rs->parent_rs_manager->thread != NULL)
	{
		/* Free parent rs_manager if RS parent process has exited. */
		if (!rs->parent_rs_manager->running)
		{
			rs_manager_free (rs->parent_rs_manager);
		} 
		else /* Don't free any memory if parent process is alive. */
		{
			return;
		}
	}

	/* Reaches this point if parent_rs_manager points to exited thread. */

	/* Empty children list and free their respective rs_manager if child
		 processes are not running. */
	while (!list_empty (&rs->children))
	{
		struct list_elem *e = list_pop_front (&rs->children);
		struct rs_manager *child = list_entry (e, struct rs_manager, child_elem);

		if (!child->running)
		{
			/* If child process is not running, free its rs_manager. */
			free (child);

		} else
		{
			/* Set child's parent_rs_manager to NULL. */
			child->parent_rs_manager = NULL;
		}
	}

  /* Free the file descriptor table and close executable file. */
  lock_acquire (&filesys_lock);
	// printf ("(rs_manager_free) %s ALLOW WRITE ON CLOSE\n", rs->exe_name);
  file_close (rs->executable);
  hash_destroy (&rs->file_table, &file_table_destroy_func);
  lock_release (&filesys_lock);

	/* Finally, deallocate original rs_manager memory. */
	free (rs);
}


/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *cmd_line)
{

	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
		 Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, cmd_line, PGSIZE);

	/* Copy cmd_line string so strtok_r can function as intended. */
	char string_to_tokenize[MAX_CMDLINE_LEN];
	strlcpy (string_to_tokenize, cmd_line, MAX_CMDLINE_LEN);
	char* save_ptr;
	char* program_name = strtok_r (string_to_tokenize, " ", &save_ptr);

	/* Create a new thread to execute FILE_NAME (first item in argv). */
	tid = thread_create (program_name, PRI_DEFAULT, start_process, fn_copy);

	if (tid == TID_ERROR)
	{
		palloc_free_page (fn_copy);
	}

	return tid;
}

/* Waits for thread TID to die and returns its exit status.
 *
 * If it was terminated by the kernel (i.e. killed due to an exception),
 * returns -1.
 *
 * If TID is invalid or if it was not a child of the calling process, or if
 * process_wait() has already been successfully called for the given TID,
 * returns -1 immediately, without waiting. */
int
process_wait (tid_t child_tid)
{
	struct thread *parent = thread_current ();

	/* Search through the rs_manager struct with the same child_tid. */
	struct rs_manager *child_rs_manager = get_child (parent, child_tid);

	/* Return error if child process does not exist. */
	if (child_rs_manager == NULL)
	{
		return ERROR;
	}

	/* Continues only if child process has exited. */
	sema_down (&child_rs_manager->child_exit_sema);

	/* Remove child process from parent's children list. Therefore,
		 subsequent wait's to the same child will return ERROR. */
	list_remove (&child_rs_manager->child_elem);

	return child_rs_manager->exit_status;
}


/* Free the current process's resources. */
void
process_exit (void)
{
	struct thread *cur = thread_current ();
	uint32_t *pd;

	/* Increments child_exit_sema so parent process can return from
		 wait. Also frees memory for rs_manager if certain conditions met. */
	rs_manager_free (cur->rs_manager);

	/* Destroy the current process's page directory and switch back
		 to the kernel-only page directory. */
	pd = cur->pagedir;
	if (pd != NULL)
	{
		/* Correct ordering here is crucial.  We must set
			 cur->pagedir to NULL before switching page directories,
			 so that a timer interrupt can't switch back to the
			 process page directory.  We must activate the base page
			 directory before destroying the process's page
			 directory, or our active page directory will be one
			 that's been freed (and cleared). */
		cur->pagedir = NULL;
		pagedir_activate (NULL);
		pagedir_destroy (pd);
	}
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
	struct thread *t = thread_current ();

	/* Activate thread's page tables. */
	pagedir_activate (t->pagedir);

	/* Set thread's kernel stack for use in processing
		 interrupts. */
	tss_update ();
}

/* Push a string s onto the stack at esp */
static void
push_string_to_stack (void **esp, char *s)
{
	int len = strlen(s) + 1;
	*esp -= len;
	strlcpy(*esp, s, len);
}

/* Push an int x onto the stack at esp */
static void
push_int_to_stack (void **esp, int x)
{
	*esp -= sizeof (int);
	*((int *) *esp) = x;
}

/* Push a pointer ptr onto the stack at esp */
static void
push_pointer_to_stack (void **esp, void *ptr)
{
	*esp -= sizeof (void *);
	*((int **) *esp) = (int *) ptr;
}

/* Function to calculate total of data pushed to stack */
static int
calc_total_stack_size(int argc, char** argv)
{
    size_t total_size = 0;

    /* Size for each string argument */
    for (int i = 0; i < argc; i++)
    {
        if (argv[i] != NULL)
        {
            total_size += strlen(argv[i]) + 1; /* +1 for null terminator */
        }
    }

    /* Size of word alignment bytes */

    /* Size for each argv element */
    total_size += (argc + 1) * sizeof(char *);

    /* Size of null pointer sentinel */
    total_size += sizeof(char *);

    /* Argv */
    total_size += sizeof(char **);

    /* Argc */
    total_size += sizeof(int);

    /* Fake return address */
    total_size += sizeof(void *);

    return total_size;

}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
	// printf("(start_process) entered \n");
	char *file_name = file_name_;
	struct intr_frame if_;
	bool success;

	/* Initialize interrupt frame and load executable. */
	memset (&if_, 0, sizeof if_);
	if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
	if_.cs = SEL_UCSEG;
	if_.eflags = FLAG_IF | FLAG_MBS;

	/* Set up argc and args, argv arrays, update interrupt frame. */
	int argc = get_no_of_args(file_name_); /* Number of arguments. */

	/* List of string arguments. */
	char **args = (char**) malloc(sizeof(char*) * argc);
	/* List of addresses of each string. */
	char **argv = (char**) malloc(sizeof(char*) * argc);
	parse_arguments(file_name, args, argc);

	success = load (args[0], &if_.eip, &if_.esp);

	struct thread *cur = thread_current ();
	cur->rs_manager->load_success = success;
	

	palloc_free_page (file_name);
	if (!success)
	{
		/* Update rs_manager states and quit if load failed. */
		sema_up (&cur->rs_manager->child_load_sema);
		thread_exit ();
	}

	/* Increment child_load_sema on success, to allow parent process
		 to return from exec. */
	sema_up (&cur->rs_manager->child_load_sema);

	/* Calculate total size of data pushed onto the stack */
    int total_size = calc_total_stack_size(argc, argv);
    
    /* Check that user's stack page is not being overflowed. If it is then exit thread */
    if (total_size > PGSIZE)
    {
        printf("Error: Stack size exceeds PGSIZE. \n");
        thread_exit();
    }

	/* Push words to stack. */
	for (int i = argc - 1; i >= 0; i--)
	{
		push_string_to_stack(&if_.esp, args[i]);
		free(args[i]);

		/* Update argv to contain address of string being pushed to stack. */
		argv[i] = (char *) if_.esp;
	}
	free (args);

	/* Word alignment. */
	if_.esp -= ((int) if_.esp % sizeof (void *));

	/* Push null pointer sentinel to stack. */
	push_pointer_to_stack (&if_.esp, NULL);

	/* Push address of each string to stack. */
	for (int i = argc - 1; i >= 0; i--)
	{
		push_pointer_to_stack (&if_.esp, argv[i]);
	}
	free (argv);

	/* Push argv to stack. */
	push_pointer_to_stack (&if_.esp, if_.esp);

	/* Push argc to stack. */
	push_int_to_stack (&if_.esp, argc);

	/* Push a fake return address. */
	push_pointer_to_stack (&if_.esp, NULL);


	/* Start the user process by simulating a return from an
		 interrupt, implemented by intr_exit (in
		 threads/intr-stubs.S).  Because intr_exit takes all of its
		 arguments on the stack in the form of a `struct intr_frame',
		 we just point the stack pointer (%esp) to our stack frame
		 and jump to it. */
	asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
	NOT_REACHED ();
}


/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
{
		unsigned char e_ident[16];
		Elf32_Half    e_type;
		Elf32_Half    e_machine;
		Elf32_Word    e_version;
		Elf32_Addr    e_entry;
		Elf32_Off     e_phoff;
		Elf32_Off     e_shoff;
		Elf32_Word    e_flags;
		Elf32_Half    e_ehsize;
		Elf32_Half    e_phentsize;
		Elf32_Half    e_phnum;
		Elf32_Half    e_shentsize;
		Elf32_Half    e_shnum;
		Elf32_Half    e_shstrndx;
};

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
{
		Elf32_Word p_type;
		Elf32_Off  p_offset;
		Elf32_Addr p_vaddr;
		Elf32_Addr p_paddr;
		Elf32_Word p_filesz;
		Elf32_Word p_memsz;
		Elf32_Word p_flags;
		Elf32_Word p_align;
};

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp)
{
	struct thread *t = thread_current ();
	struct Elf32_Ehdr ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* Allocate and activate page directory. */
	t->pagedir = pagedir_create ();
	if (t->pagedir == NULL)
		goto done;
	process_activate ();

	/* Open executable file. */
	lock_acquire (&filesys_lock);
	file = filesys_open (file_name);
	if (file == NULL)
	{
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

  // /* Deny writes to executable. */
	// printf ("(load) %s DENY WRITE\n", file_name);
  // file_deny_write (file);
  // printf ("(load) file: %s\n", file_name);

  /* Store executable file pointer and name of process. */
  t->rs_manager->executable = file;
  strlcpy (t->rs_manager->exe_name, file_name, MAX_CMDLINE_LEN);

	/* Read and verify executable header. */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
	    || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
	    || ehdr.e_type != 2
	    || ehdr.e_machine != 3
	    || ehdr.e_version != 1
	    || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
	    || ehdr.e_phnum > 1024)
	{
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++)
	{
		struct Elf32_Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type)
		{
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file))
				{
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint32_t file_page = phdr.p_offset & ~PGMASK;
					uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint32_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0)
					{
						/* Normal segment.
							 Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
						              - read_bytes);
					}
					else
					{
						/* Entirely zero.
							 Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page,
					                   read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	if (!setup_stack (esp))
		goto done;

	/* Start address. */
	*eip = (void (*) (void)) ehdr.e_entry;

	success = true;

		done:
	/* We arrive here whether the load is successful or not. */
	lock_release (&filesys_lock);
	return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file)
{
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (Elf32_Off) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
		 user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
		 address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
		 Not only is it a bad idea to map page 0, but if we allowed
		 it then user code that passed a null pointer to system calls
		 could quite likely panic the kernel by way of null pointer
		 assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0)
	{
		/* Calculate how to fill this page.
			 We will read PAGE_READ_BYTES bytes from FILE
			 and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Check if virtual page already allocated */
		struct thread *t = thread_current ();
		uint8_t *kpage = pagedir_get_page (t->pagedir, upage);

		if (kpage == NULL){

			/* Get a new page of memory. */
			kpage = palloc_get_page (PAL_USER);
			if (kpage == NULL){
				return false;
			}

			/* Add the page to the process's address space. */
			if (!install_page (upage, kpage, writable))
			{
				palloc_free_page (kpage);
				return false;
			}

		} else {

			/* Check if writable flag for the page should be updated */
			if(writable && !pagedir_is_writable(t->pagedir, upage)){
				pagedir_set_writable(t->pagedir, upage, writable);
			}

		}

		/* Load data into the page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes){
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp)
{
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL)
	{
		success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
		if (success)
		{
			/* Temporarily set up stack to avoid immediate page fault */
			*esp = PHYS_BASE - 12;
			/* TODO: Remove */
		}
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
		 address, then map our page there. */
	return (pagedir_get_page (t->pagedir, upage) == NULL
	        && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
