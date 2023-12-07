#include "../vm/spt-entry.h"
#include "../vm/mmap.h"

/* SUPPLEMENTAL PAGE TABLE STRUCT AND FUNCTIONS */

/* Supplemental page table hash function. */
unsigned 
spt_entry_hash (const struct hash_elem *e, void *aux UNUSED)
{
	const struct spt_entry *spte = hash_entry (e, struct spt_entry, elem);
	return hash_bytes (&spte->upage, sizeof spte->upage);
}

/* Supplemental page table comparison function. */
bool 
spt_entry_less (const struct hash_elem *a, const struct hash_elem *b, 
						void *aux UNUSED)
{
	const struct spt_entry *spte_a = hash_entry (a, struct spt_entry, elem);
	const struct spt_entry *spte_b = hash_entry (b, struct spt_entry, elem);
	return spte_a->upage < spte_b->upage;
}

/* Free spt_entry resources. */
void
spt_entry_destroy_func (struct hash_elem *e_, void *aux UNUSED)
{
	struct spt_entry *e = hash_entry (e_, struct spt_entry, elem);
  spt_entry_delete (e);
}

/* Look up function for supplemental page table, given UPAGE.

   Returns NULL if not found, else the SPT_ENTRY. */
struct spt_entry *
spt_entry_lookup (const void *upage)
{

  // printf ("Enters into spt entry lookup\n");
	struct spt_entry spte;
	spte.upage = upage;

	struct hash_elem *e;
	e = hash_find (thread_current ()->spage_table, &spte.elem);

  // printf ("Correctly finds hash elem\n");
  return e == NULL ? NULL : hash_entry (e, struct spt_entry, elem);
}

/* Create a new supplemental page table entry. */
struct spt_entry *
spt_entry_create (void *upage, enum page_type type, struct file *file, 
            off_t ofs, size_t bytes, bool writable)
{
  ASSERT (type == FILESYSTEM || type == STACK || type == MMAP);
  ASSERT (upage != NULL);

  struct spt_entry *spte = malloc (sizeof (struct spt_entry));
  if (spte == NULL)
    return NULL;

  spte->upage = upage;
  spte->type = type;
  // spte->owner = thread_current ();
  spte->file = file;
  spte->ofs = ofs;
  spte->bytes = bytes;
  spte->writable = writable;

  /* Set swapped to false and swap slot to error. */
  spte->swapped = false;
  spte->swap_slot = BITMAP_ERROR;

  return spte;
}

/* Delete supplemental page table entry, and associated resources. */
void
spt_entry_delete (struct spt_entry *spte)
{
  bool vm_lock_held = lock_held_by_current_thread (&vm_lock);

  ASSERT (spte != NULL);
  if (spte->type == MMAP && pagedir_is_dirty (thread_current ()->pagedir, spte->upage)) {
    lock_acquire (&filesys_lock);
    file_write_at (spte->file, spte->upage, spte->bytes, spte->ofs);
    lock_release (&filesys_lock);
  }
  
  if (!vm_lock_held)
    lock_acquire (&vm_lock);

  frame_uninstall_page (spte->upage);

  if (!vm_lock_held)
    lock_release (&vm_lock);
    
  free (spte);



}