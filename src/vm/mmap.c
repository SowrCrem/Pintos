#include "../vm/mmap.h"
#include "../lib/kernel/hash.h"


mapid_t 
mmap_create (struct file_entry *file_entry, void *addr)
{
    /* Find number of bytes in file. */

    /* TODO: Add filesys lock when accessing the file. */
    int no_bytes_to_read = file_length (file_entry->file);
    /* Return error if file's length is 0 bytes. */
    if (no_bytes_to_read == 0)
    {
        return ERROR;
    }


    /* Initialise start virtual address as 0. */
    void *start_addr = 0;

    /* Check address is page aligned - if not return error. */

    while (no_bytes_to_read > 0)
    {
        /* Initialise temporary variable (no of bytes to be read from exec file).*/
        int page_read_bytes = (no_bytes_to_read >= PGSIZE) ? PGSIZE : no_bytes_to_read;
        int page_zero_bytes = 0;

        /* Map page_read_bytes to page. */

        /* TODO: Define overlap boolean. */
        bool overlap;

        /* Range of pages mapped overlap any existing set of mapped pages. */
        if (overlap) {
            return ERROR;

        } else {
            /* Add page to spt for thread_current. */
            struct spt_entry *entry;
            entry->file = file_entry->file;
            entry->page_read_bytes = page_read_bytes;
            entry->page_zero_bytes = page_zero_bytes; 
            //hash_insert (thread_current ()->spage_table, )

        }

        
        


        /* Update temporary variables */
        no_bytes_to_read -= PGSIZE;
        start_addr += PGSIZE;

    }
    return -1;
}

void 
mmap_destroy (mapid_t mapid)
{
    /*TODO Implement function */
}