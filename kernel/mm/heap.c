#include <stddef.h>
#include "string.h"
#include "memory.h"


size_t	vsize(const void *addr)
{
	return (vm_alloc_size(addr));
}

void	vfree(const void *addr)
{
	vm_alloc_free(addr);
}

void	*vmalloc(uint32_t size)
{
	size += 16;
	size_t	pages_needed = (size / PAGE_SIZE) + ((size % PAGE_SIZE == 0) ? 0: 1);

	// Going crappy mode, just allocating a new page for every call
	if (size != 0)
	{
		virtual_addr vaddr_begin = vm_find_free(pages_needed);
		virtual_addr vaddr = vaddr_begin;
		if (!vaddr_begin)
			return (NULL);
		for (size_t i = 0; i < pages_needed; i++)
		{
			physical_addr paddr = (physical_addr)pm_alloc();
			vm_map_page((void*)paddr, (void*)vaddr);
			vaddr += PAGE_SIZE;
		}
		//0xF offset to store the number of pages
		*(int*)vaddr_begin = pages_needed;
		*(int*)(vaddr_begin + 4) = size - 16;
		return ((void*)(vaddr_begin + 16));
	}
	return (NULL);
}

size_t	ksize(const void *addr)
{
	return (vm_alloc_size(addr));
}

void	kfree(const void *addr)
{
	vm_alloc_free(addr);
}

void	*kmalloc(uint32_t size)
{
	size += 16;
	size_t	pages_needed = (size / PAGE_SIZE) + ((size % PAGE_SIZE == 0) ? 0: 1);

	// Going crappy mode, just allocating a new page for every call
	if (size != 0)
	{
		virtual_addr vaddr_begin = vm_find_free(pages_needed);
		virtual_addr vaddr = vaddr_begin;
		if (!vaddr_begin)
			return (NULL);
		physical_addr paddr = (physical_addr)pm_alloc_blocks(pages_needed);
		for (size_t i = 0; i < pages_needed; i++)
		{
			vm_map_page((void*)paddr, (void*)vaddr);
			vaddr += PAGE_SIZE;
			paddr += PAGE_SIZE;
		}
		//0xF offset to store the number of pages
		*(int*)vaddr_begin = pages_needed;
		*(int*)(vaddr_begin + 4) = size - 16;
		return ((void*)(vaddr_begin + 16));
	}
	return (NULL);
}