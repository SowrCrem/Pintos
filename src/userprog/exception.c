#include "../lib/inttypes.h"
#include "../lib/stdio.h"
#include "../lib/stdlib.h"
#include "../lib/string.h"
#include "../lib/kernel/bitmap.h"
#include "exception.h"
#include "gdt.h"
#include "syscall.h"
#include "process.h"
#include "../threads/interrupt.h"
#include "../threads/thread.h"
#include "../threads/vaddr.h"
#include "../devices/swap.h"
#include "../vm/frame.h"
#include "memory-access.h"
#include "process.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "vm/frame.h"

/* Number of page faults processed. */
static long long page_fault_cnt;

static void kill (struct intr_frame *);
static void page_fault (struct intr_frame *);

/* Registers handlers for interrupts that can be caused by user
   programs.

   In a real Unix-like OS, most of these interrupts would be
   passed along to the user process in the form of signals, as
   described in [SV-386] 3-24 and 3-25, but we don't implement
   signals.  Instead, we'll make them simply kill the user
   process.

   Page faults are an exception.  Here they are treated the same
   way as other exceptions, but this will need to change to
   implement virtual memory.

   Refer to [IA32-v3a] section 5.15 "Exception and Interrupt
   Reference" for a description of each of these exceptions. */
void
exception_init (void)
{
	/* These exceptions can be raised explicitly by a user program,
		 e.g. via the INT, INT3, INTO, and BOUND instructions.  Thus,
		 we set DPL==3, meaning that user programs are allowed to
		 invoke them via these instructions. */
	intr_register_int (3, 3, INTR_ON, kill, "#BP Breakpoint Exception");
	intr_register_int (4, 3, INTR_ON, kill, "#OF Overflow Exception");
	intr_register_int (5, 3, INTR_ON, kill, "#BR BOUND Range Exceeded Exception");

	/* These exceptions have DPL==0, preventing user processes from
		 invoking them via the INT instruction.  They can still be
		 caused indirectly, e.g. #DE can be caused by dividing by
		 0.  */
	intr_register_int (0, 0, INTR_ON, kill, "#DE Divide Error");
	intr_register_int (1, 0, INTR_ON, kill, "#DB Debug Exception");
	intr_register_int (6, 0, INTR_ON, kill, "#UD Invalid Opcode Exception");
	intr_register_int (7, 0, INTR_ON, kill, "#NM Device Not Available Exception");
	intr_register_int (11, 0, INTR_ON, kill, "#NP Segment Not Present");
	intr_register_int (12, 0, INTR_ON, kill, "#SS Stack Fault Exception");
	intr_register_int (13, 0, INTR_ON, kill, "#GP General Protection Exception");
	intr_register_int (16, 0, INTR_ON, kill, "#MF x87 FPU Floating-Point Error");
	intr_register_int (19, 0, INTR_ON, kill, "#XF SIMD Floating-Point Exception");

	/* Most exceptions can be handled with interrupts turned on.
		 We need to disable interrupts for page faults because the
		 fault address is stored in CR2 and needs to be preserved. */
	intr_register_int (14, 0, INTR_OFF, page_fault, "#PF Page-Fault Exception");
}

/* Prints exception statistics. */
void
exception_print_stats (void)
{
	printf ("Exception: %lld page faults\n", page_fault_cnt);
}

/* Handler for an exception (probably) caused by a user process. */
static void
kill (struct intr_frame *f)
{
	/* This interrupt is one (probably) caused by a user process.
		 For example, the process might have tried to access unmapped
		 virtual memory (a page fault).  For now, we simply kill the
		 user process.  Later, we'll want to handle page faults in
		 the kernel.  Real Unix-like operating systems pass most
		 exceptions back to the process via signals, but we don't
		 implement them. */

	/* The interrupt frame's code segment value tells us where the
		 exception originated. */

	switch (f->cs)
	{
		case SEL_UCSEG:
			/* User's code segment, so it's a user exception, as we
				 expected.  Kill the user process.  */
			printf ("%s: dying due to interrupt %#04x (%s).\n",
			        thread_name (), f->vec_no, intr_name (f->vec_no));
			intr_dump_frame (f);
			thread_exit ();

		case SEL_KCSEG:
			/* Kernel's code segment, which indicates a kernel bug.
				 Kernel code shouldn't throw exceptions.  (Page faults
				 may cause kernel exceptions--but they shouldn't arrive
				 here.)  Panic the kernel to make the point.  */
			intr_dump_frame (f);
			PANIC ("Kernel bug - unexpected interrupt in kernel");

		default:
			/* Some other code segment?
				 Shouldn't happen.  Panic the kernel. */
			printf ("Interrupt %#04x (%s) in unknown segment %04x\n",
			        f->vec_no, intr_name (f->vec_no), f->cs);
			PANIC ("Kernel bug - this shouldn't be possible!");
	}
}

/* Loads a page starting at offset OFS in FILE at address
   UPAGE.

   The page initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_page (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, bool writable)
{
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	/* TODO: Add file system synchronisation, add check for 
		 lock held by current thread. */
	file_seek (file, ofs);
	/* Calculate how to fill this page.
			We will read PAGE_READ_BYTES bytes from FILE
			and zero the final PAGE_ZERO_BYTES bytes. */
	size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
	size_t page_zero_bytes = PGSIZE - page_read_bytes;

	/* Check if virtual page already allocated */
	struct thread *t = thread_current ();
	
	/* Get a new page of memory. */
	struct ftable_entry *f = frame_allocate ();
	uint8_t *kpage = f->kpage;

	/* Add the page to the process's address space. */
	if (!install_page (upage, kpage, writable))
	{
		frame_free (kpage);
		return false;
	}

	/* Load data into the page. */
	if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
	{
		return false;
	}
	/* Set remaining bytes to zero. */
	memset (kpage + page_read_bytes, 0, page_zero_bytes);

	return true;
}

static void
spte_print (struct spt_entry *spte)
{
	printf ("(spte_print) upage: %d\n", spte->upage);
	printf ("(spte_print) file: %d\n", spte->file);
	printf ("(spte_print) ofs: %d\n", spte->ofs);
	printf ("(spte_print) page_read_bytes: %d\n", spte->page_read_bytes);
	printf ("(spte_print) writable: %d\n", spte->writable);
	printf ("(spte_print) disk: %d\n", spte->disk);
	printf ("(spte_print) loaded: %d\n", spte->loaded);
	printf ("(spte_print) swap_index: %d\n", spte->swap_index);
	printf ("(spte_print) swapped: %d\n", spte->swapped);
}

/* Page fault handler.  This is a skeleton that must be filled in
   to implement virtual memory.  Some solutions to task 2 may
   also require modifying this code.

   At entry, the address that faulted is in CR2 (Control Register
   2) and information about the fault, formatted as described in
   the PF_* macros in exception.h, is in F's error_code member.  The
   example code here shows how to parse that information.  You
   can find more information about both of these in the
   description of "Interrupt 14--Page Fault Exception (#PF)" in
   [IA32-v3a] section 5.15 "Exception and Interrupt Reference". */
static void
page_fault (struct intr_frame *f)
{
	void *fault_addr;  /* Fault address. */

	/* Obtain faulting address, the virtual address that was
		 accessed to cause the fault.  It may point to code or to
		 data.  It is not necessarily the address of the instruction
		 that caused the fault (that's f->eip).
		 See [IA32-v2a] "MOV--Move to/from Control Registers" and
		 [IA32-v3a] 5.15 "Interrupt 14--Page Fault Exception
		 (#PF)". */
	asm ("movl %%cr2, %0" : "=r" (fault_addr));

	/* Turn interrupts back on (they were only off so that we could
		 be assured of reading CR2 before it changed). */
	intr_enable ();

	/* Count page faults. */
	page_fault_cnt++;


	// printf ("\n(page-fault) fault_addr: %d\n", fault_addr);
#ifdef USERPROG
	#ifdef VM

	/* Terminate if address is above PHYS_BASE. */
	if (is_user_vaddr (fault_addr))
	{
		/* Look up address in supplemental page table. */
		void *upage = pg_round_down (fault_addr);
		struct spt_entry spte;
		spte.upage = upage;

		struct hash_elem *h = hash_find (thread_current ()->spage_table, &spte.elem);

		/* Lazy load page if entry found in supplemental page table. */
		if (h != NULL) 
		{
			struct spt_entry *spte = hash_entry (h, struct spt_entry, elem);

			/* DEBUG SPTE. */
			// spte_print (spte);

			if (spte->swapped)
			{
				/* Swap in page if swapped. */
				swap_in (spte->upage, spte->swap_index);
				spte->swapped = false;
				spte->loaded = true;
				spte->swap_index = BITMAP_ERROR;
				// printf ("(page-fault) successfully swapped %d into memory\n", spte->upage);
				return;

			} else if (spte->mmaped)
			{
				// printf ("(page-fault) ERROR: mmap'd page not yet implemented\n");
			} else if (spte->disk)
			{
				if (!spte->loaded)
				{
					/* Load page if not already loaded. */
					if (load_page (spte->file, spte->ofs, spte->upage, 
														spte->page_read_bytes, spte->writable))
					{
						spte->loaded = true;
						// printf ("(page-fault) successfully loaded %d into memory\n", spte->upage);
						return;
					}
				}
				else
				{
					printf ("(page-fault) ERROR: %d already in memory\n", upage);
				}
			}
		}
	}
	

		/* Load page. */
		if (!spte->loaded)
		{
			/* If stack page. */
			if (spte->stack_access)
			{
				void *kpage = frame_allocate();
				install_page (spte->upage, kpage, spte->writable);
				spte->loaded = true;
			} else
			{
				/* Lazy load page from file system. */
				// printf ("(page-fault) lazy loading %d into memory\n", spte->upage);
				if (load_segment (spte->file, spte->ofs, spte->upage, 
												 spte->page_read_bytes, spte->page_zero_bytes, 
												 spte->writable))
				{
					// printf ("(page-fault) successfully loaded %d into memory\n", spte->upage);
					return;
				}
			}
		}
		else
		{
			printf ("(page-fault) %d already loaded\n", spte->upage);
			return;
		}
	} 

	/* Dynamic Stack Allocation */

	/* A fault at or above esp while still being underneath PHYS_BASE is fine */

	if ((fault_addr >= f->esp) && (fault_addr < PHYS_BASE) 
	    || fault_addr == f->esp - PUSHA_BYTES_BELOW 
		|| fault_addr == f->esp - PUSH_BYES_BELOW) 
	{
		//printf("inside valid stack growth condition\n");
		/* Check that stack will not exceed 8MB */
		if ((PHYS_BASE - pg_round_down(fault_addr) <= MAX_STACK_SIZE) 
		     && (is_user_vaddr(fault_addr))) 
		{
			//printf("Passed max stack size check and is_user_vaddr check\n");
			//Grow the stack by allocating a page
			void *added_stack_page = pg_round_down (fault_addr);

			/* Add this stack page to spt */
			//have a new spt entry for this stack page
			struct spt_entry *spte = malloc(sizeof(struct spt_entry));
			spte->upage = added_stack_page;
			spte->file = NULL;
			spte->ofs = 0;
			spte->page_read_bytes = 0;
			spte->page_zero_bytes = 0;
			spte->writable = true;
			spte->loaded = false;
			spte->stack_access = true;

			/* Allocate a new stack page */
			void *kpage = frame_allocate();
			// use get_user_page
			// hash_insert into spt 
			struct hash_elem *h = hash_insert (thread_current()->spage_table, &spte->elem);
			if (kpage != NULL)
			{
				/* Install the new stack page */
				bool success = install_page (added_stack_page, kpage, spte->writable);		
				if (!success) {
					PANIC("install_page unsuccessful");
				} 
				else 
				{
					// printf("Installed page successfully\n");
					return;
				}
				
			} else 
			{
				// palloc_free_page(kpage);
				PANIC("Unable to allocate new stack page");
			}
		} 
		else 
		{
			//Stack too large
			terminate_userprog(ERROR);

		}
	} 
	else 
	{
		// Invalid fault address or stack too large
		terminate_userprog(ERROR);
	}

	#endif
	
	/* Copy eax value to eip. */
	f->eip = (void *) f->eax;
	/* Set eax value to -1. */
	f->eax = ERROR;
	terminate_userprog (ERROR);
#else
	bool not_present;  /* True: not-present page, false: writing r/o page. */
	bool write;        /* True: access was write, false: access was read. */
	bool user;         /* True: access by user, false: access by kernel. */
	
	/* Determine cause. */
	not_present = (f->error_code & PF_P) == 0;
	write = (f->error_code & PF_W) != 0;
	user = (f->error_code & PF_U) != 0;

	/* To implement virtual memory, delete the rest of the function
		 body, and replace it with code that brings in the page to
		 which fault_addr refers. */
	printf ("Page fault at %p: %s error %s page in %s context.\n",
	        fault_addr,
	        not_present ? "not present" : "rights violation",
	        write ? "writing" : "reading",
	        user ? "user" : "kernel");
	kill (f);
#endif
}

