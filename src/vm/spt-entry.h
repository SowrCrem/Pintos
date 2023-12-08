#ifndef VM_SPT_ENTRY_H
#define VM_SPT_ENTRY_H

#include "../lib/kernel/bitmap.h"
#include "../lib/kernel/hash.h"
#include "../lib/debug.h"
#include "../threads/malloc.h"
#include "../threads/thread.h"
#include "../threads/vaddr.h"
#include "../filesys/filesys.h"
#include "../filesys/file.h"
#include "../filesys/off_t.h"
#include "../userprog/pagedir.h"
#include "../vm/mmap.h"
#include "../vm/frame.h"

/* Maximum stack size (8 MB). */
#define MAX_STACK_SIZE (1 << 23)

/* Status of page initialisation. */
enum page_type
{
  FILESYSTEM,
  STACK,
  MMAP
};

/* Represents an entry in the hash table for supplemental page table. */
struct spt_entry
{
  // struct thread *owner;       /* Owning thread. */

	void *upage;                /* User virtual page. */
  enum page_type type;        /* Status of page initialisation type. */

  bool swapped;               /* Boolean for swapped pages. */
  size_t swap_slot;          /* Swap index for swapped pages. */

	struct file *file;          /* File pointer. */
	off_t ofs;                  /* Offset of page in file. */
	size_t bytes;               /* Number of bytes to read from file. */
	bool writable;              /* Boolean if page is read-only or not. */

	struct hash_elem elem; 			/* Hash table element for supplemental page table. */
};

unsigned spt_entry_hash (const struct hash_elem *, void * UNUSED);
bool spt_entry_less (const struct hash_elem *, const struct hash_elem *, 
						         void * UNUSED);
void spt_entry_destroy_func (struct hash_elem *, void * UNUSED);
struct spt_entry *spt_entry_lookup (const void *);
struct spt_entry *spt_entry_create (void *upage, enum page_type type, 
                                    struct file *file, off_t ofs, 
                                    size_t bytes, bool writable);
void spt_entry_delete (struct spt_entry *spte);

#endif /* vm/spt-entry.h */