#include "vm/frame.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/palloc.h"
#include "lib/kernel/hash.h"
#include "lib/debug.h"

/* Frame table. */
static struct hash frame_table;

/* Frame table lock. */
static struct lock frame_table_lock;

/* Frame table hash function. */
unsigned 
frame_hash (const struct hash_elem *elem, void *aux UNUSED) {
  const struct frame *entry = hash_entry (elem, struct frame, elem);
  return hash_bytes (&entry->page, sizeof (entry->page));
}

/* Frame table comparison function. */
bool 
frame_less (const struct hash_elem *a, const struct hash_elem *b, 
            void *aux UNUSED) {
  return frame_hash (a, NULL) < frame_hash (b, NULL);
}

/* Frame table initialization. */
void 
frame_init (void) 
{
  hash_init (&frame_table, frame_hash, frame_less, NULL);
  lock_init (&frame_table_lock);
}


/* Allocate a new frame and returns the pointer to the page.
    
   Wrapper for palloc_get_page, called with FLAGS. */
void *
frame_allocate (void)
{
    void *page = palloc_get_page (PAL_USER);
    if (page != NULL)
    {
        struct frame *entry = malloc (sizeof (struct frame));
        entry->owner = thread_current ();
        entry->page = page;

        /* TODO: Set page present bit (0) to not present (0),
           so that when program tries to access page, page fault
           occurs and page is lazy loaded. */
        uint32_t *pd = entry->owner->pagedir;
        pagedir_clear_page (pd, page);

        lock_acquire (&frame_table_lock);
        hash_insert (&frame_table, &entry->elem);
        lock_release (&frame_table_lock);
    }
    else
    {
        /* palloc_get_page failed, memory is full. */
        
        /* TODO: Implement eviction. */

        /* If no frame can be evicted without allocating swap slot, 
           panic kernel. */
    }

    return page;
}

/* Frees the frame FRAME.

   Wrapper for palloc_free_page. */
void 
frame_free (void *frame) 
{
  struct frame *entry;

  lock_acquire (&frame_table_lock);
  struct hash_elem *e = hash_find (&frame_table, &entry->elem);
  if (e != NULL)
  {
    struct frame *entry = hash_entry (e, struct frame, elem);
    hash_delete (&frame_table, &entry->elem);
    palloc_free_page (entry->page);
    free (entry);
  }

  lock_release(&frame_table_lock);
}
