#include "../vm/mmap.h"
#include "../vm/spt-entry.h"
#include "../lib/kernel/hash.h"
#include "../userprog/process.h"
#include "../threads/thread.h"
#include "../threads/vaddr.h"
#include "../lib/stdio.h"
#include "../threads/synch.h"
#include "../threads/malloc.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "../filesys/filesys.h"

void uninstall_existing_pages (struct spt_entry *first_page);

mapid_t 
mmap_create (struct file_entry *file_entry, void *start)
{
    // printf ("Reached mmap_create \n");

    if (((int) start % PGSIZE) != 0)
    {
        return ERROR;
    }

    struct hash *table = thread_current ()->spage_table;

    /* Find number of bytes in file. */

    /* Obtain a separate and independent reference to the file for each of its mappings. */
    lock_acquire (&filesys_lock);

        // printf ("About to open file.\n");
        struct file *file = file_reopen (file_entry->file);
        int read_bytes = (int) file_length (file);

    lock_release (&filesys_lock);

    if (start == 0)
    {
        return ERROR;
    }

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
        struct spt_entry s_find;
        s_find.upage = upage;
        struct hash_elem *found = hash_find (table, &s_find.elem);

        /* Checks if mapping would overwrite in a space reserved for the stack. */
        bool space_reserved_for_stack = (upage >= PHYS_BASE - MAX_STACK_SIZE); 

        if (found == NULL || !space_reserved_for_stack)
        {
            struct spt_entry *new = 
                spt_entry_create (upage, MMAP, file, ofs, bytes, true);

            if (first_page == NULL)
            {
                first_page = new;
            }
            
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

    /* TODO: Figure out how to return the mapping id - now return upage. */
    file_entry->mapping = first_page;
    return (mapid_t) file_entry->fd;
}

void 
mmap_destroy (mapid_t mapid)
{
  struct file_entry* f = file_entry_lookup ((int) mapid);
  uninstall_existing_pages (f->mapping);

  if (f->file == NULL)
  {
    hash_delete (&thread_current ()->rs_manager->file_table, &f->file_elem);
    free (f);
  }
  else
  {
    f->mapping = NULL;
  }

    // struct hash *table = thread_current ()->spage_table;
   
    // /* Find corresponding entry for address. */
    // struct spt_entry e;
    // e.upage = mapid;

    
    // struct file_entry *fe = file_entry_lookup ((int) mapid);

    // struct hash_elem *found = hash_find (table, &e.elem);
    // struct spt_entry *entry = hash_entry (found, struct spt_entry, elem);

    // // struct spt_entry *entry = spt_entry_lookup (mapid);

    // uninstall_existing_pages (entry);
}


/* Helper Functions: uninstall existing pages if error encounters for address start to addr. */
void 
uninstall_existing_pages (struct spt_entry *first_page) 
{
  /* Find supplemental page table for the running thread. */
  struct hash *table = thread_current ()->spage_table;
   
  /* If mapping is null, no need to destroy any pages. */
  if (first_page == NULL)
    return;

  /* Assign pointer to address and spt_entry to the first page.*/
  void *upage = first_page->upage; 
  struct spt_entry *entry = first_page;
  struct file *file_mapped = entry->file;

  /*  Iterate through pages.
      Find supplemental page entry for specific upage.
      Free the supplemental page entry. */
  while (entry != NULL && entry->file == file_mapped && entry->type == MMAP)
  {
    /* Update changes to file system if page is dirty. */
    if (pagedir_is_dirty (thread_current ()->pagedir, upage))
      file_write_at (entry->file, entry->upage, entry->bytes, entry->ofs);

    /* Remove page from the supplemental page table. */
    hash_delete (table, &entry->elem);
    
    /* Remove page from frame table. */
    spt_entry_destroy_func (&entry->elem, NULL);
    
    /* Update pointers to progress through user virtual memory. */
    upage += PGSIZE;

    struct spt_entry new;
    new.upage = upage;
    entry = hash_entry (hash_find (table, &new.elem), struct spt_entry, elem);
  }
}