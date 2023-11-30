#include "../vm/mmap.h"
#include "../lib/kernel/hash.h"


mapid_t 
mmap_create (struct file_entry *file_entry, void *addr)
{

    struct thread *t = thread_current ();

    /* Find number of bytes in file. */

    /* TODO: Add filesys lock when accessing the file. */
    int read_bytes = file_length (file_entry->file);
    /* Return error if file's length is 0 bytes. */
    if (read_bytes == 0)
    {
        return ERROR;
    }


    /* Initialise start virtual address as 0. */
    void *start_addr = 0;

    /* Check address is page aligned - if not return error. */

    while (read_bytes > 0)
    {
        /* Initialise temporary variable (no of bytes to be read from exec file).*/
        /* Read PAGE_READ_BYTES from FILE and
            zero the final PAGE_ZERO_BYTES bytes. */

        int page_read_bytes = (read_bytes >= PGSIZE) ? PGSIZE : read_bytes;
        int page_zero_bytes = PGSIZE - page_read_bytes;

        /* Map page_read_bytes to page. */

        /* Check if virtual page already allocated for the file */
        struct spt_entry s_find;
        s_find.file = file_entry->file;
        struct hash_elem *found = hash_find (t->spage_table, &s_find.elem);

        if (found == NULL)
        {
            struct spt_entry *spte = malloc (sizeof (struct spt_entry));

            /* TODO: Recheck assignment of spte is correct or not? */
            spte->file = file;
            spte->upage = addr;
            spte->ofs = 0;
            spte->page_read_bytes = page_read_bytes;
            spte->page_zero_bytes = page_zero_bytes;
            spte->writable = writable;
            spte->loaded = false;

            struct hash_elem *h = hash_insert (t->spage_table, &spte->elem);
        } 
        else
        {
            struct spt_entry *spte = hash_entry (found, struct spt_entry, elem);

            if (writable && !spte->writable)
            {
                spte->writable = writable;
            }
            spte->page_read_bytes = page_read_bytes;
            spte->page_zero_bytes = page_zero_bytes;

            struct hash_elem *h = hash_replace (t->spage_table, &spte->elem);

        }
        /* Need to allocate frames somewhere? */

        /* Update temporary variables */
        read_bytes -= PGSIZE;
        start_addr += PGSIZE;

    }
    return -1
    +;
}

void 
mmap_destroy (mapid_t mapid)
{
    /*TODO Implement function */
}