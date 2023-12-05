#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "../lib/stdbool.h"
#include "../lib/stdint.h"
#include "../lib/kernel/hash.h"
#include "../threads/thread.h"
#include "../threads/palloc.h"
#include "../userprog/process.h"

/* Frame table entry structure. */
struct ftable_entry
{
  struct thread *owner;     /* Owning thread. */
  void *kpage;              /* Corresponding kernel virtual address pointer. */

  struct spt_entry *spte;   /* Page stored in the frame. */
  bool pinned;              /* Boolean for if frame is pinned. */

  struct hash_elem elem;    /* Hash element for frame table. */
};

/* Frame table functions. */
void frame_init (void);
struct ftable_entry *frame_allocate (enum palloc_flags);
void frame_free (void *);

#endif /* vm/frame.h */