#include <stddef.h>
#include "memory.h"
#include "printk.h"
#include "term.h"

uint32_t	_pm_map[PM_BITMAP_SIZE];
uint32_t	_pm_size;
uint32_t	_pm_blocks;
uint32_t	_pm_used;

void	pm_init(multiboot_info_t *mbd, uint32_t magic)
{
	_pm_size = (ram_max(mbd, magic) + PM_BLOCK_SIZE - 1) & ~(PM_BLOCK_SIZE - 1);
	_pm_used = 1024; //The first 4Mb of RAM are reserved for the kernel
	_pm_blocks = _pm_size / PM_BLOCK_SIZE;
	// We set the first 4Mb as reserved in the bitmap
	for (size_t i = 0; i < 32; i++)
		_pm_map[i] = 0xFFFFFFFF;
	for (size_t i = 32; i < (_pm_blocks / 32); i++)
	{
		_pm_map[i] = 0;
	}
}

static int32_t	mmap_get_first_free(void)
{
	for (size_t i = 0; i < _pm_blocks / 32; i++)
	{
		if (_pm_map[i] != 0xFFFFFFFF)
		{
			for (size_t block = i * 32; block < ((i + 1) * 32); block++)
			{
				if (!mmap_test(block))
					return (block);
			}
		}
	}
	return (-1);
}

static int32_t	mmap_get_free_consecutives(uint32_t n)
{
	uint32_t c = 0;
	uint32_t first_block = 0;

	for (size_t i = 0; i < _pm_blocks / 32; i++)
	{
		if (_pm_map[i] != 0xFFFFFFFF)
		{
			for (size_t block = i * 32; block < (i + 1) * 32; block++)
			{
				if (!mmap_test(block))
				{
					c++;
					if (first_block == 0)
						first_block = block;
					if (c == n)
						return (first_block);
				}
				else
				{
					first_block = 0;
					c = 0;
				}
			}
		}
	}
	return (-1);
}

void	*pm_alloc_blocks(uint32_t n)
{
	void	*physical_addr;
	int		frame;

	if (pm_free_count() < n)
		return (0x0);

	if ((frame = mmap_get_free_consecutives(n)) == -1)
		return (0x0);

	physical_addr = (void*)(frame * PM_BLOCK_SIZE);
	for (size_t i = frame; i < frame + n; i++)
		mmap_set(i);
	_pm_used += n;

	return (physical_addr);
}

void	*pm_alloc(void)
{
	void	*physical_addr;
	int		frame;

	if (pm_free_count() == 0)
		return (0x0);

	if ((frame = mmap_get_first_free()) == -1)
		return (0x0);

	mmap_set(frame);
	physical_addr = (void*)(frame * PM_BLOCK_SIZE);
	_pm_used++;

	return (physical_addr);
}

void	pm_free(void *block_paddr)
{
	uint32_t frame = (uint32_t)block_paddr / PM_BLOCK_SIZE;

	mmap_unset(frame);
	_pm_used--;
}

#define MAX_MAP_HEIGHT	8
#define MAX_MAP_WIDTH	80

static size_t	count_positive_bits(uint32_t m)
{
	size_t	c = 0;

	for (size_t i = 0; i < 32; i++)
	{
		c += (m >> i) & 0x1;
	}
	return (c);
}

void	print_physical_memory(void)
{
	size_t	pp_char = _pm_blocks / (MAX_MAP_HEIGHT * MAX_MAP_WIDTH) + 1;
	size_t	cur_page = 0;

	pp_char += 32 - (pp_char % 32);
	printk("1 char is equivalent to %d physical memory blocks (%d blocks in total)."\
		"\n`.` = 0%% | `*` < 50%% | `#` >= 50%%\n", pp_char, _pm_blocks);
	for (size_t i = 0; i < MAX_MAP_HEIGHT; i++)
	{
		for (size_t j = 0; j < MAX_MAP_WIDTH; j++)
		{
			size_t t = 0;
			for (size_t c = 0; c < pp_char; c += 32)
			{
				t += count_positive_bits(_pm_map[(cur_page + c) / 32]);
				cur_page += 32;
			}
			if (t == 0)
				puts(".");
			else if (t < (pp_char / 2))
				puts("*");
			else
				puts("#");
			if (cur_page >= _pm_blocks)
			{
				puts("\n");
				return ;
			}
		}
	}
}