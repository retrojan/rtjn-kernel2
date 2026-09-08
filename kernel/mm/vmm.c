#include <stddef.h>
#include "string.h"
#include "memory.h"
#include "time.h"
#include "printk.h"

pdirectory page_directory __attribute__((aligned(4096)));
ptable first_page_table __attribute__((aligned(4096)));

extern void load_page_dir(pdirectory *dir);
extern void enable_paging();

pdirectory *get_page_directory(void)
{
	return (&page_directory);
}

void	init_virtual_memory(void)
{
	//Initializing page_directory with ring 0, read/write and present = 0
	for (size_t i = 0; i < 1024; i++)
	{
		ptable	*new_table = (ptable*)pmmngr_alloc_block();
		bzero(new_table, sizeof(ptable));
		page_directory.m_entries[i] = (uint32_t)(new_table) | (I86_PDE_WRITABLE | I86_PDE_PRESENT);
	}

	//Map the first 4Mb of physical memory to the first 4Mb of virtual Memory.
	for (size_t i = 0; i < 1024; i++)
	{
		first_page_table.m_entries[i] = (i * 0x1000) | (I86_PTE_WRITABLE | I86_PTE_PRESENT);
	}

	ptable	*pages =
		(ptable*)((uint32_t)(page_directory.m_entries[1]) & I86_PDE_FRAME);
	for (size_t i = 0; i < PAGES_PER_TABLE; i++)
	{
		pages->m_entries[i] = (uint32_t)((page_directory.m_entries[i]) & I86_PDE_FRAME) | (I86_PTE_WRITABLE | I86_PTE_PRESENT);
	}
	page_directory.m_entries[0] = (uint32_t)(&first_page_table) | (I86_PDE_WRITABLE | I86_PDE_PRESENT);

	load_page_dir(&page_directory);
	enable_paging();
}

uint8_t		vmmngr_alloc_page(pt_entry *e)
{
	void *p = pmmngr_alloc_block();

	if (!p)
		return (1);

	pt_entry_set_frame(e, (physical_addr)p);
	pt_entry_add_attrib(e, I86_PTE_PRESENT);
	return (0);
}

void		vmmngr_free_page(pt_entry *e)
{
	void *p = pt_entry_pfn(*e);

	if (p)
		pmmngr_free_block(p);
	pt_entry_del_attrib(e, I86_PDE_PRESENT);
}

void		vmmngr_map_page(void *phys, void *virt)
{
	pdirectory	*p = get_page_directory();
	pd_entry	*e = &(p->m_entries[PAGE_DIR_INDEX((uint32_t)virt)]);

    if ((*e & I86_PDE_PRESENT) != I86_PDE_PRESENT)
    {
    	ptable *table = (ptable*)pmmngr_alloc_block();
    	if (!table)
    		return ;
    	bzero(table, sizeof(ptable));

    	pd_entry *new_e = &(p->m_entries[PAGE_DIR_INDEX((uint32_t)virt)]);
    	pd_entry_add_attrib(new_e, I86_PDE_PRESENT);
    	pd_entry_add_attrib(new_e, I86_PDE_WRITABLE);
    	pd_entry_set_frame(new_e, (physical_addr)table);
    }
	ptable		*table = (ptable*)PAGE_PHYSICAL_ADDR(e);
	pt_entry	*page = &(table->m_entries[PAGE_TABLE_INDEX((uint32_t)virt)]);

	pt_entry_set_frame(page, (physical_addr)phys);
	pt_entry_add_attrib(page, I86_PTE_PRESENT);
}

static virtual_addr get_available_page_table(void)
{
	pdirectory *p = get_page_directory();

	for (size_t i = 0; i < PAGES_PER_DIR; i++)
	{
		if ((p->m_entries[i] & I86_PDE_PRESENT) != I86_PDE_PRESENT)
		{
			return ((virtual_addr)(i * PAGES_PER_TABLE * PAGE_SIZE));
		}
	}
	return (0);
}

virtual_addr	get_available_virtual_addr(size_t nbr)
{
	virtual_addr	vaddr;
	pdirectory		*p = get_page_directory();
	size_t			count = 0;

	for (size_t i = 0; i < PAGES_PER_DIR; i++)
	{
		if ((p->m_entries[i] & I86_PDE_PRESENT) == I86_PDE_PRESENT)
		{
			ptable *table = (ptable*)((uint32_t)(p->m_entries[i]) & I86_PDE_FRAME);
			for (size_t j = 0; j < PAGES_PER_TABLE; j++)
			{
				if ((table->m_entries[j] & I86_PTE_PRESENT) != I86_PTE_PRESENT)
				{
					if (count == 0)
						vaddr = (virtual_addr)(i * PAGES_PER_TABLE * PAGE_SIZE + j * PAGE_SIZE);
					count++;
					if (count == nbr)
						return (vaddr);
				}
				else
				{
					count = 0;
				}
			}
		}
		else
		{
			count = 0;
		}
	}
	return (get_available_page_table());
}

size_t	vmmngr_alloc_size(const void *addr)
{
	virtual_addr	vaddr = (virtual_addr)addr - 16;
	return (*(size_t*)(vaddr + 4));
}

void	vmmngr_alloc_free(const void *addr)
{
	virtual_addr	vaddr = (virtual_addr)addr - 16;
	pdirectory		*p = get_page_directory();
	size_t			pages_to_free = *(int*)vaddr;

	for (size_t i = 0; i < pages_to_free; i++)
	{
		ptable *t = (ptable*)((uint32_t)p->m_entries[PAGE_DIR_INDEX(vaddr)] & I86_PDE_FRAME);
		pt_entry *pd = &(t->m_entries[PAGE_TABLE_INDEX(vaddr)]);
		vmmngr_free_page(pd);
		__native_flush_tlb_single(vaddr);
		vaddr += 0x1000;
	}
}

void	vmmngr_dump_alloc(const void *addr)
{
	virtual_addr	vaddr = (virtual_addr)addr - 16;
	pdirectory		*p = get_page_directory();
	size_t			pages_to_read = *(int*)vaddr;

	for (size_t i = 0; i < pages_to_read; i++)
	{
		ptable *t = (ptable*)((uint32_t)p->m_entries[PAGE_DIR_INDEX(vaddr)] & I86_PDE_FRAME);
		pt_entry *pd = &(t->m_entries[PAGE_TABLE_INDEX(vaddr)]);
		void *p = pt_entry_pfn(*pd);
		printk("Virtual address 0x%x | Physical address %p\n", vaddr, p);
		vaddr += 0x1000;
	}

}