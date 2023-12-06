#ifndef VM_MMAP_H
#define VM_MMAP_H

#include "../userprog/process.h"
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

typedef int mapid_t;

mapid_t mmap_create (struct file_entry *, void *);
void mmap_destroy (struct file_entry *);

#endif /* VM_MAP_H */