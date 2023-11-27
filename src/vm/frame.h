#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdbool.h>
#include <stdint.h>
#include "threads/palloc.h"
#include "lib/kernel/hash.h"

/* Frame table entry structure. */
struct frame
{
  struct thread *owner;    /* Owning thread. */
  void *page;               /* Corresponding kernel virtual adress pointer. */

  struct hash_elem elem;    /* Hash element for frame table. */
};

/* Frame table functions. */
void frame_init (void);
void *frame_allocate (void);
void frame_free (void *);

#endif /* vm/frame.h */
