#include "../lib/inttypes.h"
#include "../lib/stdio.h"
#include "../lib/stdlib.h"
#include "userprog/exception.h"
#include "userprog/gdt.h"
#include "userprog/syscall.h"
#include "userprog/process.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
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


	// printf ("(page-fault) fault_addr: %d\n", fault_addr);

	/* Turn interrupts back on (they were only off so that we could
		 be assured of reading CR2 before it changed). */
	intr_enable ();

	/* Count page faults. */
	page_fault_cnt++;

#ifdef USERPROG
	#ifdef VM

	/* Get current thread. */
	struct thread *t = thread_current ();

	/* Get supplemental page table. */
	struct hash *spt = t->spage_table;

	/* Get supplemental page table entry. */
	void *upage = pg_round_down (fault_addr);
	struct spt_entry spte;
	spte.upage = upage;

	/* Look up address in supplemental page table. */
	struct hash_elem *h = hash_find (spt, &spte.elem);

	if (h != NULL) {
		struct spt_entry *spte = hash_entry (h, struct spt_entry, elem);

		/* Load page. */
		if (load_segment (spte->file, spte->ofs, spte->upage, 
											 spte->page_read_bytes, spte->page_zero_bytes, 
											 spte->writable))
		{
			// printf ("(page-fault) successfully loaded %d into memory\n", spte->upage);
			return;
		}
	} 

	/* Dynamic Stack Allocation */

	/* A fault at or above esp while still being underneath PHYS_BASE is fine */

	if ((fault_addr >= f->esp) && (fault_addr < PHYS_BASE) 
	    || fault_addr == f->esp - PUSHA_BYTES_BELOW 
		|| fault_addr == f->esp - PUSH_BYES_BELOW) 
	{
		/* Check that stack will not exceed 8MB */
		if (PHYS_BASE - pg_round_down(fault_addr) <= MAX_STACK_SIZE) 
		{
			// TODO : Grow the stack by allocating a page
			void *added_stack_page = pg_round_down(fault_addr);

			/* Add this stack page to spt */
			//TODO : have a new spt entry for this stack page
			struct spt_entry *spte = malloc(sizeof(struct spt_entry));
			spte->upage = added_stack_page;
			spte->file = NULL;
			spte->ofs = 0;
			spte->page_read_bytes = 0;
			spte->page_zero_bytes = 0;
			spte->writable = true;
			spte->loaded = false;

			/* Allocate a new stack page */
			void *kpage = frame_allocate();
			// use get_user_page
			// hash_insert into spt 
			struct hash_elem *h = hash_insert (thread_current()->spage_table, &spte->elem);
			if (kpage != NULL)
			{
				/* Install the new stack page */
				bool success = install_page (spte, kpage, NULL);		
				if (!success) {
					PANIC("install_page unsuccessful");
				} 
				
			} else 
			{
				palloc_free_page(kpage);
				PANIC("Unable to allocate new stack page");
			}
		} 
		else 
		{
			//Stack too large

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

