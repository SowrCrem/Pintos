#include "../vm/spt-entry.h"

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
  frame_free (pagedir_get_page (thread_current ()->pagedir, e->upage));
	free (e);
}

/* Look up function for supplemental page table, given UPAGE.

   Returns NULL if not found, else the SPT_ENTRY. */
struct spt_entry *
spt_entry_lookup (const void *upage)
{
	struct spt_entry spte;
	spte.upage = upage;

	struct hash_elem *e;
	e = hash_find (&thread_current ()->spage_table, &spte.elem);
	return e == NULL ? NULL : hash_entry (e, struct spt_entry, elem);
}

/* Create a new supplemental page table entry. */
struct spt_entry *
spt_entry_create (void *upage, enum page_type type, struct file *file, 
            off_t ofs, size_t bytes, bool writable)
{
  struct spt_entry *spte = malloc (sizeof (struct spt_entry));
  if (spte == NULL)
    return NULL;

  spte->upage = upage;
  spte->type = type;
  spte->file = file;
  spte->ofs = ofs;
  spte->bytes = bytes;
  spte->writable = writable;

  spte->swapped = false;
  spte->swap_index = BITMAP_ERROR;

  return spte;
}