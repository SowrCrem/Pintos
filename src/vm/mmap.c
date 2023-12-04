#include "../vm/mmap.h"
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

    struct spage_table *table = thread_current ()->spage_table;
    /* Find number of bytes in file. */

    /* TODO: Add filesys lock when accessing the file. */

    //lock_acquire (&filesys_lock);
    /* Obtain a separate and independent reference to the file for each of its mappings. */
    // printf ("About to open file.\n");
    struct file *file = file_reopen(file_entry->file);
    int read_bytes = file_length (file);

    if (start == 0)
    {
        return ERROR;
    }

    //lock_release (&filesys_lock);
    int ofs = 0;


    /* Initialise start virtual address. */
    void *upage = start;
    struct spte *first_page = NULL;

    /* Check address is page aligned - if not return error. */
    // printf ("About to enter the loop.\n");
    while (read_bytes > 0)
    {
        /* Initialise temporary variable (no of bytes to be read from exec file).*/
        /* Read PAGE_READ_BYTES from FILE and
            zero the final PAGE_ZERO_BYTES bytes. */
        int page_read_bytes = (read_bytes >= PGSIZE) ? PGSIZE : read_bytes;
        int page_zero_bytes = PGSIZE - page_read_bytes;


        /* Check if virtual page already allocated for the file */
        struct spt_entry s_find;
        s_find.upage = upage;
        struct hash_elem *found = hash_find (table, &s_find.elem);

        /* Checks if mapping would overwrite in a space reserved for the stack. */
        bool space_reserved_for_stack = (upage >= PHYS_BASE - (1 << 23)); 

        if (found == NULL && !space_reserved_for_stack)
        {
            struct spt_entry *new = malloc (sizeof (struct spt_entry));

            new->file = file;
            new->upage = (uint8_t *) pg_round_down (upage);
            new->ofs = ofs; /* To ensure that page is page aligned. */
            new->page_read_bytes = page_read_bytes;
            new->writable = true; 
            new->loaded = true; /* TODO: Check if this value is true. */

            if (first_page == NULL)
            {
                first_page = new;
            }
            hash_insert (table, &new->elem);
        } 
        else
        {
            /* Uninstall any existing pages. */
            uninstall_existing_pages (first_page);
            return ERROR;
        }

        /* Update temporary variables to progress through user virtual memory. */
        read_bytes -= page_read_bytes;
        upage += PGSIZE;
        ofs += page_read_bytes;
    }

    /* TODO: Figure out how to return the mapping id - now return upage. */
    return (mapid_t) start;
}

void 
mmap_destroy (mapid_t mapid)
{
    struct spage_table *table = thread_current ()->spage_table;
   
    /* Find corresponding entry for address. */
    struct spt_entry e;
    e.upage = mapid;

    struct hash_elem *found = hash_find (table, &e.elem);
    struct spt_entry *entry = hash_entry (found, struct spt_entry, elem);

    uninstall_existing_pages (entry);
}


/* Helper Functions: uninstall existing pages if error encounters for address start to addr. */
void 
uninstall_existing_pages (struct spt_entry *first_page) 
{
    /* Find supplemental page table for the running thread. */
    struct spage_table *table = thread_current ()->spage_table;
   
    /* If mapping is null, no need to destroy any pages. */
    if (first_page == NULL)
        return;

    /* Assign pointer to address and spt_entry to the firs tpage.*/
    void *upage = first_page->upage; 
    struct spt_entry *entry = first_page;
    struct file *file_mapped = entry->file;

    /*  Iterate through pages.
        Find supplemental page entry for specific upage.
        Free the supplemental page entry. */
    while (entry != NULL && entry->file == file_mapped)
    {
        /* Remove spt_entry from the table. */
        hash_delete (table, entry);
        frame_free (upage);

        /* Update pointers to progress through user virtual memory. */
        upage += PGSIZE;

        struct spt_entry new;
        new.upage = upage;
        entry = hash_entry (hash_find (table, &new.elem), struct spt_entry, elem);

    }
}