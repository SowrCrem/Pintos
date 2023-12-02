#ifndef VM_MMAP_H
#define VM_MMAP_H

#include "../userprog/process.h"

typedef int mapid_t;
#define PGSIZE (4096)


mapid_t mmap_create (struct file_entry *file_entry, void *start);
void mmap_destroy (mapid_t mapid);

#endif /* VM_MAP_H */
