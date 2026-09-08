#ifndef MEMORY_H
# define MEMORY_H

# include <stdint.h>
# include "multiboot.h"

# define	KDEBUG				0
# define	PM_BLOCK_SIZE		4096
# define	PM_BITMAP_SIZE		32768

uint32_t	ram_max(multiboot_info_t *mbd, uint32_t magic);
void		pm_init(multiboot_info_t *mbd, uint32_t magic);
void		vm_init(void);
void		*pm_alloc_blocks(uint32_t n);
void		*pm_alloc(void);
void		pm_free(void *block_paddr);

extern uint32_t	_pm_map[PM_BITMAP_SIZE];
extern uint32_t	_pm_size;
extern uint32_t	_pm_blocks;
extern uint32_t	_pm_used;

typedef	uint32_t	physical_addr;

static inline void		mmap_set(uint32_t bit)
{
	_pm_map[bit / 32] |= (1 << (bit & 31));
}

static inline void		mmap_unset(uint32_t bit)
{
	_pm_map[bit / 32] &= ~ (1 << (bit % 32));
}

static inline uint32_t	mmap_test(uint32_t bit)
{
	return (_pm_map[bit / 32] & (1 << (bit & 31)));
}

static inline uint32_t	pm_free_count(void)
{
	return (_pm_blocks - _pm_used);
}


/* ================
**	VIRTUAL MEMORY
*/

typedef	uint32_t	pt_entry;
typedef	uint32_t	pd_entry;
typedef	uint32_t	virtual_addr;

# define PAGES_PER_TABLE		1024
# define PAGES_PER_DIR			1024

typedef struct	s_pdirectory
{
	pd_entry	m_entries[PAGES_PER_DIR];
}				pdirectory;

typedef struct	s_ptable
{
	pt_entry	m_entries[PAGES_PER_TABLE];
}				ptable;

# define PAGE_DIR_INDEX(x)		((x >> 22) & 0x3ff)
# define PAGE_TABLE_INDEX(x)	((x >> 12) & 0x3ff)
# define PAGE_PHYSICAL_ADDR(x)	(*x & ~0xfff)

# define PTABLE_ADDR_SPACE_SIZE	0x400000
# define DTABLE_ADDR_SPACE_SIZE	0x100000000

# define PAGE_SIZE				4096

enum PAGE_PTE_FLAGS {

	I86_PTE_PRESENT			= 1,			//0000000000000000000000000000001
	I86_PTE_WRITABLE		= 2,			//0000000000000000000000000000010
	I86_PTE_USER			= 4,			//0000000000000000000000000000100
	I86_PTE_WRITETHOUGH		= 8,			//0000000000000000000000000001000
	I86_PTE_NOT_CACHEABLE	= 0x10,			//0000000000000000000000000010000
	I86_PTE_ACCESSED		= 0x20,			//0000000000000000000000000100000
	I86_PTE_DIRTY			= 0x40,			//0000000000000000000000001000000
	I86_PTE_PAT				= 0x80,			//0000000000000000000000010000000
	I86_PTE_CPU_GLOBAL		= 0x100,		//0000000000000000000000100000000
	I86_PTE_LV4_GLOBAL		= 0x200,		//0000000000000000000001000000000
   	I86_PTE_FRAME			= 0x7FFFF000 	//1111111111111111111000000000000
};

static inline void	pt_entry_add_attrib(pt_entry *e, uint32_t attribute)
{
	*e |= attribute;
}

static inline void	pt_entry_del_attrib(pt_entry *e, uint32_t attribute)
{
	*e ^= attribute;
}

static inline void	*pt_entry_pfn(pt_entry e)
{
	return ((void*)(e & I86_PTE_FRAME));
}

static inline void	pt_entry_set_frame(pt_entry *e, physical_addr addr)
{
	*e = (*e & ~I86_PTE_FRAME) | addr;
}

enum PAGE_PDE_FLAGS {
 
	I86_PDE_PRESENT			= 1,			//0000000000000000000000000000001
	I86_PDE_WRITABLE		= 2,			//0000000000000000000000000000010
	I86_PDE_USER			= 4,			//0000000000000000000000000000100
	I86_PDE_PWT				= 8,			//0000000000000000000000000001000
	I86_PDE_PCD				= 0x10,			//0000000000000000000000000010000
	I86_PDE_ACCESSED		= 0x20,			//0000000000000000000000000100000
	I86_PDE_DIRTY			= 0x40,			//0000000000000000000000001000000
	I86_PDE_4MB				= 0x80,			//0000000000000000000000010000000
	I86_PDE_CPU_GLOBAL		= 0x100,		//0000000000000000000000100000000
	I86_PDE_LV4_GLOBAL		= 0x200,		//0000000000000000000001000000000
   	I86_PDE_FRAME			= 0x7FFFF000	//1111111111111111111000000000000
};

virtual_addr	vm_find_free(size_t nbr);
pdirectory		*vm_page_dir(void);
size_t			vm_alloc_size(const void *addr);
void			vm_map_page(void *phys, void *virt);
void			vm_alloc_free(const void *addr);
void			vm_dump_alloc(const void *addr);

static inline void	pd_entry_add_attrib(pd_entry *e, uint32_t attribute)
{
	*e |= attribute;
}

static inline void	pd_entry_del_attrib(pd_entry *e, uint32_t attribute)
{
	*e ^= attribute;
}

static inline void	pd_entry_set_frame(pd_entry *e, physical_addr addr)
{
	*e = (*e & ~I86_PTE_FRAME) | addr;
}

static inline void	*pd_entry_pfn(pd_entry e)
{
	return ((void*)(e & I86_PDE_FRAME));
}

static inline void __native_flush_tlb_single(virtual_addr addr) {
	asm volatile("invlpg (%0)" ::"r" (addr) : "memory");
}

/* ============================================================================
**                      HIGH LEVEL MEMORY FUNCTIONS
** ============================================================================
*/

void	*vmalloc(uint32_t size);
void	vfree(const void *addr);
size_t	vsize(const void *addr);
size_t	ksize(const void *addr);
void	kfree(const void *addr);
void	*kmalloc(uint32_t size);

void	map_ident(uintptr_t base, uint32_t len);

#endif
