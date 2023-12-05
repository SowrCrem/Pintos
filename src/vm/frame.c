#include "../vm/frame.h"
#include "../vm/spt-entry.h"
#include "../devices/swap.h"
#include "../userprog/pagedir.h"
#include "../threads/vaddr.h"
#include "../threads/thread.h"
#include "../threads/synch.h"
#include "../threads/malloc.h"
#include "../threads/palloc.h"
#include "../lib/kernel/hash.h"
#include "../lib/debug.h"

/* Global frame table. */
static struct hash frame_table;

/* Global frame table iterator. */
static struct hash_iterator iterator;

/* Global frame table lock. */
static struct lock frame_table_lock;

/* Frame table hash function.

   Uses OWNER and KPAGE members in ftable_entry to form the key. */
static unsigned 
frame_hash (const struct hash_elem *e_, void *aux UNUSED) 
{
  const struct ftable_entry *e = 
      hash_entry (e_, struct ftable_entry, elem);
  unsigned kpage_hash = hash_bytes (&e->kpage, sizeof (e->kpage));
  unsigned owner_hash = hash_bytes (&e->owner, sizeof (e->owner));
  
  return kpage_hash + owner_hash;
}

/* Frame table comparison function. */
static bool 
frame_less (const struct hash_elem *a, const struct hash_elem *b, 
            void *aux UNUSED) 
{
  return frame_hash (a, NULL) < frame_hash (b, NULL);
}

/* Frame table destroy function. 

   Not necessary as frame table is global. */
static void
frame_destroy_func (struct hash_elem *e, void *aux UNUSED)
{
  struct ftable_entry *entry = hash_entry (e, struct ftable_entry, elem);
  free (entry);
}

/* Initialises the frame table. */
void 
frame_init (void) 
{
  hash_init (&frame_table, frame_hash, frame_less, NULL);
  lock_init (&frame_table_lock);
}

/* Initialises the frame table iterator. */
static void
init_iterator (void)
{
  static bool is_initialised = false;
  if (!is_initialised)
  {
    hash_first (&iterator, &frame_table);
    is_initialised = true;
  }
}

/* Return frame table entry for the frame which holds the result of 
   the chosen eviction algorithm's frame of choice, to be evicted. */
static struct ftable_entry *
get_frame_to_evict (void)
{
  /* Find frame where accessed bit is 0. */
  init_iterator ();
  struct ftable_entry *evictee = NULL;
  
  while (evictee == NULL)
  {
    /* Iterate circularaly through hash table until frame with accessed
       bit set to 0 is found, then return. */
    while (hash_next (&iterator))
    {
      struct ftable_entry *e = hash_entry (hash_cur (&iterator), 
                                           struct ftable_entry, elem);

      /* If accessed bit is 0, evict frame. */
      if (!pagedir_is_accessed (e->owner->pagedir, e->spte->upage))
      {
        /* Cannot evict pinned frames. */
        if (e->pinned)
        {
          // printf ("(get_frame_to_evict) frame %d is pinned\n", e->spte->upage);
          continue;
        } else
        {          
          evictee = e;
          break;
        }
      }
      /* Otherwise, set accessed bit to 0 and continue. */
      else
      {
        // printf ("(get_frame_to_evict) accessing frame %d\n", e->spte->upage);
        pagedir_set_accessed (e->owner->pagedir, e->spte->upage, false);
      }
    }
    /* If no frame with accessed bit set to 0 is found,
       restart search at start of frame table, mimicking
       a circular data structure. */
    hash_first (&iterator, &frame_table);
  }
  return evictee;
}

/* Allocate a new frame and returns the pointer to the page.
    
   Wrapper for palloc_get_page, called with FLAGS. 
   
   MUST ensure to set SPTE after page installion successful. */
struct ftable_entry *
frame_allocate (enum palloc_flags flags)
{
  void *kpage = palloc_get_page (flags);

  /* Page offset should be 0. */
  ASSERT (pg_ofs (kpage) == 0);

  struct ftable_entry *entry = NULL;
  
  if (kpage != NULL)
  {
    entry = malloc (sizeof (struct ftable_entry));
    entry->owner = thread_current ();
    entry->kpage = kpage;
    entry->spte = NULL;
    entry->pinned = false;

    lock_acquire (&frame_table_lock);
      struct hash_elem *h = hash_insert (&frame_table, &entry->elem); 
    lock_release (&frame_table_lock);

    // printf ("Is hash_elem added to frame table? %s \n", (h != &entry->elem) ? "Yes" : "No");
  }
  else  /* Memory is full: eviction must occur. */
  {
    lock_acquire (&frame_table_lock);

      /* Get victim page to be evicted. */
      entry = get_frame_to_evict ();
      void *page_to_evict = entry->kpage;

      /* Swap out victim page, if dirty or stack page. */
      size_t swap_slot = swap_out (page_to_evict);

      /* Store swap slot in supplemental page table entry,
        and update flags. */
      entry->spte->swapped = true;
      entry->spte->swap_index = swap_slot;
      
      // /* Reinsert entry into frame table. */
      // hash_replace (&frame_table, &entry->elem);
    
    lock_release (&frame_table_lock);


    /* Free corresponding frame. */
    frame_free (page_to_evict);

    /* Allocate new frame. */
    kpage =  palloc_get_page (PAL_USER); /* Currently returning null. */

    entry->kpage = kpage;
    // /* Page offset should be 0. */
    ASSERT (pg_ofs (kpage) == 0);

    /* Page allocation should return freed page. */
    ASSERT (kpage == page_to_evict)

    /* KPAGE should not be null if eviction is successful. */
    ASSERT (kpage != NULL);
  }

  ASSERT (entry != NULL);
  return entry;
}

/* Frees the frame FRAME, if owned by current thread, and removes the
   corresponding entry in frame table. 

   Wrapper for palloc_free_page. */
void 
frame_free (void *kpage) 
{
  ASSERT (pg_ofs (kpage) == 0);
  // ASSERT (kpage != NULL);

  struct ftable_entry e_;
  e_.kpage = kpage;
  // e_.owner = thread_current ();
  // printf ("Is kpage null? %s", (kpage == NULL) ? "Yes" : "No");

  lock_acquire (&frame_table_lock);
  
    struct hash_elem *e = hash_find (&frame_table, &e_.elem);
    // printf ("IS e null? %s\n", (e == NULL) ? "Yes" : "No");
    if (e != NULL)
    {
      struct ftable_entry *entry = hash_entry (e, struct ftable_entry, elem);

      /* Delete frame from hash table and free memory. */
      hash_delete (&frame_table, &entry->elem);
      free (entry);

      /* Free page at kernel virtual address KPAGE. */
      palloc_free_page (kpage);
    }

  lock_release (&frame_table_lock);
}

/* TODO: Remove all frames that are owned by thread THREAD.

   Called on process exit. */
// static void
// frame_remove_all (void)
// {
//   /* Cannot iteratively delete multiple elements as any modification
//      to the hash table invalidates the hash iterator.
     
//      Could map elements to a list and then delete matching hash
//      elements by use of the list elem key, or similar. */


//   init_iterator ();
//   hash_first (&iterator, &frame_table);

//   while (hash_next (&iterator))
//   {
//     struct ftable_entry *e = hash_entry (hash_cur (&iterator), 
//                                          struct ftable_entry, elem);
//     if (e->owner == thread)
//     {
//       hash_delete (&frame_table, &e->elem);
//       free (e);
//     }
//   }
// }
