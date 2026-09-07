#include <stdint.h>
#include <stddef.h>
#include "string.h"
#include "memory.h"
#include "sched.h"

/* The ring-3 user program lives in virtual page 0x40000000 (PDE 256).
 * Code/rodata is shared read-only by every instance; each spawned task gets
 * its own stack page further up the same 4 MiB region. */
#define USER_ENTRY		0x40000000
#define USER_STACK_BASE		0x40010000
#define USER_STACK_SIZE		0x8000
#define USER_MAX_TASKS		6

extern char	_binary_user_bin_start[];
extern char	_binary_user_bin_end[];

static int	g_code_mapped = 0;
static int	g_user_slots[USER_MAX_TASKS];

static int	map_user_page(uint32_t virt, void *phys)
{
	pdirectory	*p = get_page_directory();
	pd_entry	*e = &p->m_entries[PAGE_DIR_INDEX(virt)];

	if (!(*e & I86_PDE_PRESENT))
	{
		ptable	*table = (ptable*)pmmngr_alloc_block();

		if (!table)
			return (-1);
		bzero(table, sizeof(ptable));
		pd_entry_set_frame(e, (physical_addr)table);
	}
	pd_entry_add_attrib(e, I86_PDE_PRESENT | I86_PDE_WRITABLE | I86_PDE_USER);
	{
		ptable	*table = (ptable*)PAGE_PHYSICAL_ADDR(e);
		pt_entry	*pg = &table->m_entries[PAGE_TABLE_INDEX(virt)];

		pt_entry_set_frame(pg, (physical_addr)phys);
		pt_entry_add_attrib(pg, I86_PTE_PRESENT | I86_PTE_WRITABLE | I86_PTE_USER);
	}
	__native_flush_tlb_single((virtual_addr)virt);
	return (0);
}

static void	unmap_user_page(uint32_t virt)
{
	pdirectory	*p = get_page_directory();
	pd_entry	*e = &p->m_entries[PAGE_DIR_INDEX(virt)];

	if (!(*e & I86_PDE_PRESENT))
		return;
	{
		ptable	*table = (ptable*)PAGE_PHYSICAL_ADDR(e);
		pt_entry	*pg = &table->m_entries[PAGE_TABLE_INDEX(virt)];

		if (*pg & I86_PTE_PRESENT)
		{
			pmmngr_free_block(pt_entry_pfn(*pg));
			*pg = 0;
			__native_flush_tlb_single((virtual_addr)virt);
		}
	}
}

static int	usertask_ensure_code(void)
{
	uint32_t	len;
	void		*phys;

	if (g_code_mapped)
		return (0);
	len = (uint32_t)(_binary_user_bin_end - _binary_user_bin_start);
	if (len == 0 || len > PAGE_SIZE)
		return (-1);
	phys = pmmngr_alloc_block();
	if (!phys)
		return (-1);
	if (map_user_page(USER_ENTRY, phys) < 0)
	{
		pmmngr_free_block(phys);
		return (-1);
	}
	memcpy((void*)USER_ENTRY, _binary_user_bin_start, len);
	g_code_mapped = 1;
	return (0);
}

int	usertask_spawn(const char *name)
{
	int		slot = -1;
	void	*phys;
	int		pid;
	uint32_t	stack_virt;

	for (int i = 0; i < USER_MAX_TASKS; i++)
	{
		if (g_user_slots[i] == 0 ||
			!sched_pid_alive((uint32_t)g_user_slots[i]))
		{
			slot = i;
			break;
		}
	}
	if (slot < 0)
		return (-1);
	if (usertask_ensure_code() < 0)
		return (-1);

	stack_virt = USER_STACK_BASE + (uint32_t)slot * USER_STACK_SIZE;
	phys = pmmngr_alloc_block();
	if (!phys)
		return (-1);
	if (g_user_slots[slot] != 0)
		unmap_user_page(stack_virt);
	if (map_user_page(stack_virt, phys) < 0)
	{
		pmmngr_free_block(phys);
		return (-1);
	}
	pid = sched_add_user_task(name, USER_ENTRY,
		stack_virt + USER_STACK_SIZE - 16);
	if (pid < 0)
	{
		unmap_user_page(stack_virt);
		return (-1);
	}
	g_user_slots[slot] = pid;
	return (pid);
}