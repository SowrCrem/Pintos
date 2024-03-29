#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "../lib/stdbool.h"
#include "../lib/stdint.h"
#include "../lib/debug.h"
#include "../lib/kernel/hash.h"
#include "../lib/kernel/list.h"
#include "../devices/swap.h"
#include "../userprog/pagedir.h"
#include "../userprog/process.h"
#include "../threads/thread.h"
#include "../threads/vaddr.h"
#include "../threads/thread.h"
#include "../threads/synch.h"
#include "../threads/palloc.h"
#include "../threads/malloc.h"
#include "../vm/spt-entry.h"

/* Global virtual memory lock. */
extern struct lock vm_lock;

/* Frame table entry structure. */
struct ftable_entry
{
  struct thread *owner;     /* Owning thread. */
  void *kpage;              /* Corresponding kernel virtual address pointer. */

  struct spt_entry *spte;   /* Page stored in the frame. */

  struct hash_elem elem;    /* Hash element for frame table. */
  struct list_elem eviction_elem; /*Element to evict from to_remove list. */
};

/* Frame table functions. */
void frame_init (void);
void *frame_allocate (enum palloc_flags);
void frame_free (void *);
bool frame_install_page (struct spt_entry *, void *);
void frame_uninstall_page (void *upage);
void frame_remove_all (struct thread*);

#endif /* vm/frame.h */