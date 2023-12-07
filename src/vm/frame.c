#include "../vm/frame.h"

/* Global frame table. */
static struct hash frame_table;

/* Global frame table iterator. */
static struct hash_iterator iterator;
static struct hash_iterator i2;
struct list to_remove;

/* Global virtual memory lock. */
struct lock vm_lock;

/* Frame table hash function.

   Uses SPTE member in ftable_entry to form the key. */
static unsigned 
frame_hash (const struct hash_elem *e_, void *aux UNUSED) 
{
  const struct ftable_entry *e = 
      hash_entry (e_, struct ftable_entry, elem);
  unsigned spte_hash = hash_bytes (&e->spte, sizeof (e->spte));
  
  return spte_hash;
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
  lock_init (&vm_lock);
}

/* Initialises the frame table iterator. */
static void
init_iterator (void)
{
  ASSERT (lock_held_by_current_thread (&vm_lock));
  // printf ("(init_iterator) Enters into interator again.\n");
  /* Initialise iterator if not already initialised. */
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
  ASSERT (lock_held_by_current_thread (&vm_lock));
  // printf ("(get_frame_to_evict) Enter into get_frame to evict function.\n");
  /* Find frame where accessed bit is 0. */
  init_iterator ();
  // printf ("(get_frame_to_evict) Passes iterator\n");
  struct ftable_entry *evictee;
  while (evictee == NULL)
  {
    /* Iterate circularaly through hash table until frame with accessed
       bit set to 0 is found, then return. */
    while (hash_next (&iterator))
    {
      // printf ("Enters into the loop.\n");
      struct ftable_entry *e = hash_entry (hash_cur (&iterator), 
                                           struct ftable_entry, elem);
      // printf ("The table entry in loop is: %p\n", e->kpage);
      // printf ("The thread that is running this is %p\n", e->owner->pagedir);
      // printf ("(get_frame_to_evict) The entry value is %d, and the accessbit is %s.\n", e->kpage,
      //  (!pagedir_is_accessed(e->owner->pagedir, e->spte->upage)) ? "0" : "1");

      // printf ("(get_frame_to_evict) The thread value is %s\n", (e->owner == NULL) ? "NULL" : "NOT NULL");


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
          // printf ("(get_frame_to_evict) The frame is found.\n") ;     
          evictee = e;
          break;
        }
  
      }
      /* Otherwise, set accessed bit to 0 and continue. */
      else
      {
        // printf ("(get_frame_to_evict) accessing frame %d\n", e->spte->upage);
        pagedir_set_accessed (e->owner->pagedir, e->spte->upage, false);

        // printf ("(get_frame_to_evict) The accessed bit is set to 1\n");
      }
      // printf ("Does it exit out of this loop?\n");
    }
    // printf ("(get_frame_to_evict) Need to loop in a circular fashion.\n");
    /* If no frame with accessed bit set to 0 is found,
       restart search at start of frame table, mimicking
       a circular data structure. */
    hash_first (&iterator, &frame_table);
  }

  ASSERT (evictee->kpage != NULL);
  if (evictee != NULL) {
    // printf ("(get_frame_to_evict) Found evictee %d\n", evictee->kpage);
  }
  return evictee;
}

/* Evicts a frame from the frame table and returns the KPAGE of
   the newly available frame. */
static void *
evict_frame (void)
{
  ASSERT (lock_held_by_current_thread (&vm_lock));

  /* Get frame that is evictable. */
  // printf ("(evict_frame) Going to evict frame function.\n");
  struct ftable_entry *frame_to_evict = get_frame_to_evict ();
  // printf ("(evict_frame) The frame to evict is %d\n", frame_to_evict->kpage);

  /* Frame should have valid supplemental page table entry. */
  // ASSERT (frame_to_evict->spte != NULL);

  /* Get page directory and user virtual address of page to evict. */
  uint32_t *pd = frame_to_evict->owner->pagedir;
  void *upage = frame_to_evict->spte->upage;

  /* Get kernel virtual address of page to evict. */
  void *kpage_to_evict = pagedir_get_page (pd, upage);

  /* Clear page from page directory. */
  pagedir_clear_page (pd, upage);

  /* Swap out victim page, if dirty or stack page. */
  if (pagedir_is_dirty (pd, upage) ||
      frame_to_evict->spte->type == STACK)
  {
    /* Set swapped bit to true. */
    frame_to_evict->spte->swapped = true;
    
    /* Swap out page. */
    size_t swap_slot = swap_out (kpage_to_evict);

    /* Kernel panic if swap partition is full. */ 
    if (swap_slot == BITMAP_ERROR)
      PANIC ("Swap partition is full.");

    /* Update swap slot in supplemental page table. */
    frame_to_evict->spte->swap_slot = swap_slot;
  }
  // printf ("(evict_frame) Removing from the frame table.\n");
  hash_delete (&frame_table, &frame_to_evict->elem);
  /* Remove supplemental page table entry from frame table. */
  frame_to_evict->spte = NULL;
  // printf ("(evict_frame) removed frame from the table itself");

  return kpage_to_evict;  
}

/* Returns the kernel virtual memory pointer to the newly allocated 
   "frame" (kernel page mapping to physical memory).  

   Performs eviction if no pages are available. 
    
   Wrapper for palloc_get_page, called with FLAGS. */
void *
frame_allocate (enum palloc_flags flags)
{ 
  /* Assert flags are valid. */
  ASSERT (flags & PAL_USER);

  void *kpage = palloc_get_page (flags);
  if (kpage == NULL)
  {
    // printf ("The stack is now full.\n");
    /* Perform eviction. */
    bool lock_held = lock_held_by_current_thread (&vm_lock);
    if (!lock_held)
      lock_acquire (&vm_lock);

    kpage = evict_frame ();
    if (!lock_held)
      lock_release (&vm_lock);
  }
  // if (kpage != NULL) 
  // {
  //   printf ("(frame_allocate) The kpage allocated is: %d\n", kpage);
  // }
  ASSERT (kpage != NULL);
  return kpage;
}

/* Clears the frame FRAME, if owned by current thread, and removes the
   corresponding entry in frame table. 

   Wrapper for palloc_free_page. */
void 
frame_free (void *kpage) 
{
  ASSERT (lock_held_by_current_thread (&vm_lock));

  if (kpage == NULL)
    return;

  struct ftable_entry e_;
  e_.kpage = kpage;
  e_.owner = thread_current ();
  
  struct hash_elem *e = hash_find (&frame_table, &e_.elem);
  // printf ("(frame_free) the frame to be evicted is found\n.");
  if (e != NULL)
  {
    struct ftable_entry *entry = hash_entry (e, struct ftable_entry, elem);

    /* Delete frame from hash table and free memory. */
    hash_delete (&frame_table, &entry->elem);
    free (entry);
  }
  
  /* Free page at kernel virtual address KPAGE. */
  palloc_free_page (kpage);
}

/* Uninstall user page UPAGE located in supplemental page table and 
   remove entry from frame table. */
void
frame_uninstall_page (void *upage)
{
  ASSERT (lock_held_by_current_thread (&vm_lock));

  /* Get kernel virtual address mapping to user virtual address UPAGE. */
  uint32_t *pd = thread_current ()->pagedir;
  void *kpage = pagedir_get_page (pd, upage);

  /* Clear page from page directory. */
  pagedir_clear_page (pd, upage);

  /* Remove entry from frame table. */
  frame_free (kpage);
}

/* Install UPAGE located in SPTE and add entry to frame table. */
bool
frame_install_page (struct spt_entry *spte, void *kpage)
{
  void *upage = spte->upage;
  bool writable = spte->writable;

  ASSERT (is_user_vaddr (upage));
  ASSERT (kpage != NULL);
 
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (pg_ofs (kpage) == 0);
  ASSERT (upage != NULL);
  ASSERT (kpage != NULL);

  /* Install page. */
  bool success = install_page (upage, kpage, writable);

  if (success)
  {
    bool lock_held = lock_held_by_current_thread (&vm_lock);
    if (!lock_held)
      lock_acquire (&vm_lock);

    /* Add entry to frame table. */
    struct ftable_entry *e = malloc (sizeof (struct ftable_entry));
    if (e == NULL)
      return false;

    e->owner = thread_current ();
    e->kpage = kpage;
    e->spte = spte;
    e->pinned = false;

      // printf ("(frame_install_page) The e->kpage value is %d\n", e->kpage);

    hash_insert (&frame_table, &e->elem);

    if (!lock_held)
      lock_release (&vm_lock);
    
    /* Set accessed bit to 1 (clock page replacement algorithm). */
    pagedir_set_accessed (e->owner->pagedir, upage, true);
    // printf ("(frame_install_page) The frame is added to the frame table and access bit set to 1.\n");
  }

  return success;
}


void 
frame_remove_all (struct thread *thread)
{
  list_init (&to_remove);
  if (!lock_held_by_current_thread(&vm_lock))
  {
    lock_acquire (&vm_lock);
  }
  // init_iterator ();
  hash_first (&i2, &frame_table);
  // printf ("(frame_remove_all) entering into for loop to remove all frames.\n");
  while (hash_next (&i2))
  {
    struct ftable_entry *frame_to_evict = hash_entry (hash_cur (&i2),
            struct ftable_entry, elem);
    // printf ("The thread tid value is %d\n", thread->tid);
    // printf ("the frame to evict value is %d\n", frame_to_evict->owner->tid);
    // printf ("The frame to evict has spte which is %s\n", (frame_to_evict->spte == NULL) ?
      // "NULL" : "NOT NULL");
    if (frame_to_evict->owner->tid == thread->tid)
    {
      // printf ("About to delete from hash table.\n");
      // hash_delete (&frame_table, &e->elem);
      // printf ("Removed from entry successfully.\n");
      // // spt_entry_delete (e->spte);
      // // free (e);
      list_push_back (&to_remove, &frame_to_evict->eviction_elem);
      // printf ("Added list to the to remove list.\n");

    }
  }

  while (!list_empty (&to_remove))
  {
    struct list_elem *elem = list_pop_front (&to_remove);
    struct ftable_entry *f = list_entry (elem, struct ftable_entry, eviction_elem);

    hash_delete (&frame_table, &f->elem);
  }

  if (lock_held_by_current_thread(&vm_lock))
  {
    lock_release (&vm_lock);
  }

}
