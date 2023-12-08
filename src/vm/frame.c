#include "../vm/frame.h"
#include "../threads/synch.h"
#include "../filesys/filesys.h"

/* Global frame table. */
static struct hash frame_table;

/* Global frame table iterator. */
static struct hash_iterator iterator;
static struct hash_iterator exit_iterator;
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
  ASSERT (lock_held_by_current_thread (&filesys_lock));

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
        evictee = e;
        break;
      }
      /* Otherwise, set accessed bit to 0 and continue. */
      else
      {
        pagedir_set_accessed (e->owner->pagedir, e->spte->upage, false);
      }
    }
    /* If no frame with accessed bit set to 0 is found,
       restart search at start of frame table, mimicking
       a circular data structure. */
    hash_first (&iterator, &frame_table);
  }

  ASSERT (evictee->kpage != NULL);

  return evictee;
}

/* Evicts a frame from the frame table and returns the KPAGE of
   the newly available frame. */
static void *
evict_frame (void)
{
  ASSERT (lock_held_by_current_thread (&vm_lock));
  ASSERT (lock_held_by_current_thread (&filesys_lock));
  /* Get frame that is evictable. */
  struct ftable_entry *frame_to_evict = get_frame_to_evict ();

  /* Frame should have valid supplemental page table entry. */

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
  hash_delete (&frame_table, &frame_to_evict->elem);

  /* Remove supplemental page table entry from frame table. */
  frame_to_evict->spte = NULL;

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
	ASSERT (lock_held_by_current_thread (&filesys_lock));

  void *kpage = palloc_get_page (flags);
  if (kpage == NULL)
  {
    /* Perform eviction. */
    bool lock_held = lock_held_by_current_thread (&vm_lock);
    if (!lock_held)
      lock_acquire (&vm_lock);

    kpage = evict_frame ();
    if (!lock_held)
      lock_release (&vm_lock);
  }

  ASSERT (kpage != NULL);
  return kpage;
}

/* Clears the frame FRAME, if owned by current thread, and removes the
   corresponding entry in frame table. 

   Wrapper for palloc_free_page. */
void 
frame_free (void *kpage) 
{
  bool vm_lock_held = lock_held_by_current_thread (&vm_lock);
  if (kpage == NULL)
    return;

  struct ftable_entry e_;
  e_.kpage = kpage;
  e_.owner = thread_current ();
  
  if (!vm_lock_held)
    lock_acquire (&vm_lock);
  
  struct hash_elem *e = hash_find (&frame_table, &e_.elem);
  if (e != NULL)
  {
    struct ftable_entry *entry = hash_entry (e, struct ftable_entry, elem);

    /* Delete frame from hash table and free memory. */
    hash_delete (&frame_table, &entry->elem);
    free (entry);
  }

  if (!vm_lock_held)
    lock_release (&vm_lock);
  
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
  ASSERT (lock_held_by_current_thread (&filesys_lock));
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
    
    /* Add entry to frame table. */
    struct ftable_entry *e = malloc (sizeof (struct ftable_entry));
    if (e == NULL)
      return false;

    e->owner = thread_current ();
    e->kpage = kpage;
    e->spte = spte;

    lock_release (&filesys_lock);
    bool lock_held = lock_held_by_current_thread (&vm_lock);
    if (!lock_held)
      lock_acquire (&vm_lock);

    hash_insert (&frame_table, &e->elem);

    if (!lock_held)
      lock_release (&vm_lock);
    lock_acquire (&filesys_lock);
    
    /* Set accessed bit to 1 (clock page replacement algorithm). */
    pagedir_set_accessed (e->owner->pagedir, upage, true);
  }

  return success;
}


/* Evicts all frames an exiting thread owns. */
void 
frame_remove_all (struct thread *thread)
{
  // ASSERT (!lock_held_by_current_thread (&filesys_lock));
  bool filesys_lock_held = lock_held_by_current_thread (&filesys_lock);
  bool vm_lock_held = lock_held_by_current_thread (&vm_lock);

  if (filesys_lock_held)
  {
    lock_release (&filesys_lock);
  }
  list_init (&to_remove);
  if (!vm_lock_held)
  {
    lock_acquire (&vm_lock);
  }

  /* Initialise iterator to traverse through hash table to evict frames when
     its thread owner exits. */
  hash_first (&exit_iterator, &frame_table);
  while (hash_next (&exit_iterator))
  {
    struct ftable_entry *frame_to_evict = hash_entry (hash_cur (&exit_iterator),
            struct ftable_entry, elem);

    if (frame_to_evict->owner == thread)
    {
      list_push_back (&to_remove, &frame_to_evict->eviction_elem);
    }
  }

  /* Iterate through list of frames to evict, and remove from global frame table. */
  while (!list_empty (&to_remove))
  {
    struct list_elem *elem = list_pop_front (&to_remove);
    struct ftable_entry *f = list_entry (elem, struct ftable_entry, eviction_elem);

    hash_delete (&frame_table, &f->elem);
  }

  if (!vm_lock_held)
  {
    lock_release (&vm_lock);
  }

  if (filesys_lock_held)
  {
    lock_acquire (&filesys_lock);
  }

}
