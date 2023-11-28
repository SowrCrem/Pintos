#include "../vm/frame.h"
#include "../threads/thread.h"
#include "../threads/synch.h"
#include "../threads/malloc.h"
#include "../threads/palloc.h"
#include "../lib/kernel/hash.h"
#include "../lib/debug.h"

/* Global frame table. */
static struct hash frame_table;

/* Global frame table lock. */
static struct lock frame_table_lock;

/* Frame table hash function. */
static unsigned 
frame_hash (const struct hash_elem *elem, void *aux UNUSED) 
{
  const struct frame_table_entry *entry = 
      hash_entry (elem, struct frame_table_entry, elem);
  return hash_bytes (&entry->kpage, sizeof (entry->kpage));
}

/* Frame table comparison function. */
static bool 
frame_less (const struct hash_elem *a, const struct hash_elem *b, 
            void *aux UNUSED) 
{
  return frame_hash (a, NULL) < frame_hash (b, NULL);
}

/* Frame table destroy function. */
void
frame_destroy_func (struct hash_elem *elem, void *aux UNUSED)
{
  struct frame_table_entry *entry = hash_entry (elem, struct frame_table_entry, elem);
  free (entry);
}

/* Initialises the frame table. */
void 
frame_table_init (void) 
{
  hash_init (&frame_table, frame_hash, frame_less, NULL);
  lock_init (&frame_table_lock);
}

/* Allocate a new frame and returns the pointer to the page.
    
   Wrapper for palloc_get_page, called with FLAGS. */
void *
frame_allocate (void)
{
  void *kpage = palloc_get_page (PAL_USER);
  
  if (kpage != NULL)
  {
    struct frame_table_entry *entry = malloc (sizeof (struct frame_table_entry));
    entry->owner = thread_current ();
    entry->kpage = kpage;

    lock_acquire (&frame_table_lock);
      hash_insert (&frame_table, &entry->elem); 
    lock_release (&frame_table_lock);
  }
  else  /* palloc_get_page failed, memory is full. */
  {
      
    /* TODO: Implement eviction. */

    /* If no frame can be evicted without allocating swap slot, 
        panic kernel. */
  }

  return kpage;
}

/* Frees the frame FRAME.

   Wrapper for palloc_free_page. */
void 
frame_free (void *kpage) 
{
  struct frame_table_entry *entry = malloc (sizeof (struct frame_table_entry));
  entry->kpage = kpage;

  lock_acquire (&frame_table_lock);

    struct hash_elem *e = hash_find (&frame_table, &entry->elem);
    if (e != NULL)
    {
      struct frame_table_entry *entry = hash_entry (e, struct frame_table_entry, elem);
      hash_delete (&frame_table, &entry->elem);
      
      /* Free page (implicitly synchronized). */
      palloc_free_page (kpage);
      // palloc_free_page (entry->page);

      free (entry);
    }

  lock_release(&frame_table_lock);
}

/* Remove all frames that are owned by thread THREAD.

   Called on process exit. */
void
frame_remove_all (struct thread *thread)
{
  struct hash_iterator i;
  hash_first (&i, &frame_table);
  while (hash_next (&i))
  {
    struct frame_table_entry *entry = hash_entry (hash_cur (&i), struct frame_table_entry, elem);
    if (entry->owner == thread)
    {
      hash_delete (&frame_table, &entry->elem);
      free (entry);
    }
  }
}
