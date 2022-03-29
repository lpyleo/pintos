#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <threads/thread.h>
#include <lib/string.h>
#include "lib/stddef.h"
bool mmap_check_mmap_vaddr(struct thread *cur, const void *vaddr, int num_page);
bool mmap_install_page(struct thread *cur, struct mmap_handler *mh);
void mmap_read_file(struct mmap_handler* mh, void *upage, void *kpage);
void mmap_write_file(struct mmap_handler* mh, void *upage, void *kpage);
bool mmap_load_segment(struct file *file, off_t ofs, uint8_t *upage, uint32_t read_bytes, uint32_t zero_bytes, bool writable);

void syscall_file_close(struct file* file);
struct file* syscall_file_open(const char* name);
bool syscall_translate_vaddr(const void *vaddr, bool write);


void syscall_init (void);

#endif /* userprog/syscall.h */
