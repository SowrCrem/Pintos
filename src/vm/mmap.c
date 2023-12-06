#include "../vm/mmap.h"


void uninstall_existing_pages (struct spt_entry *first_page);

mapid_t 
mmap_create (struct file_entry *file_entry, void *start)
{
  // printf ("Reached mmap_create \n");

  if (((int) start % PGSIZE) != 0 || start == 0)
      return ERROR;

  struct hash *table = thread_current ()->spage_table;

  /* Find number of bytes in file. */

  /* Obtain a separate and independent reference to the file for each of its mappings. */
  lock_acquire (&filesys_lock);

      // printf ("About to open file.\n");
      struct file *file = file_reopen (file_entry->file);
      int read_bytes = (int) file_length (file);

  lock_release (&filesys_lock);


  int ofs = 0;

  /* Initialise start virtual address. */
  void *upage = pg_round_down (start);
  struct spte_entry *first_page = NULL;

  /* Check address is page aligned - if not return error. */
  // printf ("About to enter the loop.\n");
  while (read_bytes > 0)
  {
    /* Initialise temporary variable (no of bytes to be read from exec file).*/
    /* Read PAGE_READ_BYTES from FILE and
        zero the final PAGE_ZERO_BYTES bytes. */
    int bytes = (read_bytes >= PGSIZE) ? PGSIZE : read_bytes;

    /* Check if virtual page already allocated for the file */
    struct spt_entry *s_find = spt_entry_lookup (upage);
 

    /* Checks if mapping would overwrite in a space reserved for the stack. */
    bool space_reserved_for_stack = (upage <= PHYS_BASE - MAX_STACK_SIZE); 

    if (s_find == NULL || !space_reserved_for_stack)
    {
      struct spt_entry *new = 
        spt_entry_create (upage, MMAP, file, ofs, bytes, true);

      if (first_page == NULL)
        first_page = new;
      
      /* Insert entry into supplemental page table. */
      hash_insert (table, &new->elem);
    } 
    else
    {
        /* Uninstall any existing pages. */
        uninstall_existing_pages (first_page);
        return ERROR;
    }

    /* Update temporary variables to progress through user virtual memory. */
    read_bytes -= bytes;
    upage += PGSIZE;
    ofs += bytes;
  }

  file_entry->mapping = first_page;
  return (mapid_t) file_entry->fd;
}

void 
mmap_destroy (struct file_entry *f)
{

  uninstall_existing_pages (f->mapping);

  if (f->file == NULL)
  {
    hash_delete (&thread_current ()->rs_manager->file_table, &f->file_elem);
    free (f);
  }
  else
    f->mapping = NULL; 
  
}


/* Helper Functions: uninstall existing pages if error encounters for address start to addr. */
void 
uninstall_existing_pages (struct spt_entry *first_page) 
{
  /* If mapping is null, no need to destroy any pages. */
  if (first_page == NULL)
    return;

  /* Find supplemental page table for the running thread. */
  struct hash *spt = thread_current ()->spage_table;
   
  /* Assign pointer to address and spt_entry to the first page.*/
  void *upage = first_page->upage; 
  struct spt_entry *entry = first_page;
  struct file *file_mapped = entry->file;

  bool filesys_lock_held = lock_held_by_current_thread (&filesys_lock);
  if (!filesys_lock_held)
    lock_acquire (&filesys_lock);

  lock_acquire (&vm_lock);

  /*  Iterate through pages.
      Find supplemental page entry for specific upage.
      Free the supplemental page entry. */
  while (entry != NULL && entry->file == file_mapped && entry->type == MMAP)
  {
    /* Update changes to file system if page is dirty. */
    if (pagedir_is_dirty (thread_current ()->pagedir, upage))
      file_write_at (entry->file, entry->upage, entry->bytes, entry->ofs);

    /* Remove page from the supplemental page table. */
    hash_delete (spt, &entry->elem);
    
    /* Remove page from frame table. */
    spt_entry_delete (entry);
    
    /* Update pointers to progress through user virtual memory. */
    upage += PGSIZE;
    entry = spt_entry_lookup (upage);
  }

  lock_release (&vm_lock);

  file_close (file_mapped);
  if (!filesys_lock_held)
    lock_release (&filesys_lock);
}