#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "../lib/stdbool.h"
#include "../lib/stdint.h"
#include "../threads/thread.h"
#include "../threads/palloc.h"
#include "../lib/kernel/hash.h"

/* Frame table entry structure. */
struct ftable_entry
{
  struct thread *owner;     /* Owning thread. */
  void *kpage;               /* Corresponding kernel virtual address pointer. */

  struct hash_elem elem;    /* Hash element for frame table. */
};

/* Frame table functions. */
void frame_table_init (void);
void *frame_allocate (void);
void frame_free (void *);
void frame_remove_all (struct thread *);

#endif /* vm/frame.h */