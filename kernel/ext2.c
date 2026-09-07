#include <stdint.h>
#include <stddef.h>
#include "string.h"
#include "printk.h"
#include "memory.h"
#include "ata.h"
#include "vfs.h"
#include "ext2.h"

#define EXT2_SECTOR_SIZE	512

ext2_fs_data_t	ext2_fs;
int		ext2_mounted = 0;

/* Convert a byte offset to a sector offset */
static uint32_t	ext2_off_to_sector(uint32_t byte_off)
{
	return (byte_off / EXT2_SECTOR_SIZE);
}

/* Number of sectors covering a byte range */
static uint32_t	ext2_off_to_sectors(uint32_t byte_off, uint32_t byte_len)
{
	uint32_t	start = byte_off / EXT2_SECTOR_SIZE;
	uint32_t	end = (byte_off + byte_len + EXT2_SECTOR_SIZE - 1) / EXT2_SECTOR_SIZE;

	if (byte_len == 0)
		return (0);
	return (end - start);
}

/* Sector where the block-group-descriptor table begins. It is placed in
 * the first block after the superblock (which is at byte offset 1024). */
static uint32_t	ext2_gd_sector(ext2_fs_data_t *fs)
{
	uint32_t	gd_block = (EXT2_SUPERBLOCK_OFFSET / fs->block_size) + 1;
	return (gd_block * (fs->block_size / EXT2_SECTOR_SIZE));
}

int	ext2_read_superblock(uint8_t drive, ext2_superblock_t *sb)
{
	uint8_t	buf[EXT2_SECTOR_SIZE];
	uint32_t	sector = EXT2_SUPERBLOCK_OFFSET / EXT2_SECTOR_SIZE;
	uint32_t	offset = EXT2_SUPERBLOCK_OFFSET % EXT2_SECTOR_SIZE;

	if (ata_read_sectors(drive, sector, 1, buf) != 0)
		return (-1);
	memcpy(sb, buf + offset, sizeof(ext2_superblock_t));
	return (0);
}

void	ext2_init(void)
{
	ext2_superblock_t	sb;

	bzero(&ext2_fs, sizeof(ext2_fs));
	ext2_mounted = 0;

	if (ext2_read_superblock(EXT2_DRIVE, &sb) != 0)
	{
		printk("ext2: cannot read superblock from drive %d\n", EXT2_DRIVE);
		return;
	}
	if (sb.magic != EXT2_MAGIC)
	{
		printk("ext2: drive %d is not ext2/ext3/ext4 (magic 0x%x)\n",
			EXT2_DRIVE, (unsigned)sb.magic);
		return;
	}

	ext2_fs.superblock = kmalloc(sizeof(ext2_superblock_t));
	if (!ext2_fs.superblock)
		return;
	memcpy(ext2_fs.superblock, &sb, sizeof(ext2_superblock_t));

	ext2_fs.block_size = EXT2_BLOCK_SIZE(&sb);
	ext2_fs.blocks_per_group = sb.blocks_per_group;
	ext2_fs.inodes_per_group = sb.inodes_per_group;
	ext2_fs.group_count = (sb.blocks_count / sb.blocks_per_group) +
		((sb.blocks_count % sb.blocks_per_group) != 0 ? 1 : 0);

	/* Read the group descriptors. They start in the block right after
	 * the superblock (block 1 for 1KiB blocks, block 2 for larger). */
	ext2_fs.group_desc = kmalloc(sizeof(ext2_group_desc_t) * ext2_fs.group_count);
	if (!ext2_fs.group_desc)
	{
		kfree(ext2_fs.superblock);
		return;
	}
	/* The group descriptor table lives in the first block after the
	 * superblock (always at byte offset 1024). */
	uint32_t	gd_sector = ext2_gd_sector(&ext2_fs);
	uint8_t		gd_buf[EXT2_SECTOR_SIZE * 4];
	uint32_t	sectors = ext2_off_to_sectors(gd_sector * EXT2_SECTOR_SIZE,
			sizeof(ext2_group_desc_t) * ext2_fs.group_count);
	if (sectors > sizeof(gd_buf) / EXT2_SECTOR_SIZE)
		sectors = sizeof(gd_buf) / EXT2_SECTOR_SIZE;
	if (ata_read_sectors(EXT2_DRIVE, gd_sector, sectors, gd_buf) != 0)
	{
		kfree(ext2_fs.group_desc);
		kfree(ext2_fs.superblock);
		return;
	}
	memcpy(ext2_fs.group_desc, gd_buf, sizeof(ext2_group_desc_t) * ext2_fs.group_count);

	ext2_mounted = 1;

	/* Register as a VFS filesystem driver and auto-mount at the root. */
	ext2_vfs_register();
	if (vfs_mount("hd0", "/", "ext2") != VFS_OK)
		printk("ext2: auto-mount at / failed\n");

	printk("ext2: mounted drive %d, block size %d,%s%s%s\n",
		EXT2_DRIVE, ext2_fs.block_size,
		(sb.rev_level >= 1 ? " rev1" : ""),
		(sb.feature_compat & (1 << 22) ? " extents" : ""),
		(sb.feature_incompat & (1 << 1) ? " dir_index" : ""));

	ext2_mounted = 1;
}

int	ext2_read_inode(ext2_fs_data_t *fs, uint32_t inode_num, ext2_inode_t *inode)
{
	if (!fs || !fs->superblock || !fs->group_desc)
		return (-1);
	if (inode_num < 1 || inode_num > fs->superblock->inodes_count)
		return (-1);

	uint32_t	group = (inode_num - 1) / fs->superblock->inodes_per_group;
	uint32_t	index = (inode_num - 1) % fs->superblock->inodes_per_group;

	if (group >= fs->group_count)
		return (-1);

	uint16_t	inode_size = fs->superblock->inode_size ? fs->superblock->inode_size : 128;

	uint32_t	inode_table_block = fs->group_desc[group].inode_table;
	uint32_t	byte_off = inode_table_block * fs->block_size + index * inode_size;
	uint32_t	sector = byte_off / EXT2_SECTOR_SIZE;
	uint32_t	off_in_sector = byte_off % EXT2_SECTOR_SIZE;

	uint8_t	buf[EXT2_SECTOR_SIZE];
	if (ata_read_sectors(EXT2_DRIVE, sector, 1, buf) != 0)
		return (-1);
	memcpy(inode, buf + off_in_sector, sizeof(ext2_inode_t));
	return (0);
}

static uint32_t	ext2_read_block(ext2_fs_data_t *fs, uint32_t block_num, uint8_t *buf, size_t len)
{
	if (block_num == 0)
		return (0);
	uint32_t	sector = block_num * (fs->block_size / EXT2_SECTOR_SIZE);
	uint32_t	sectors = (fs->block_size / EXT2_SECTOR_SIZE);
	if (len > fs->block_size || fs->block_size > EXT2_SECTOR_SIZE * 4)
		len = fs->block_size;

	uint32_t	break_sectors = (len + EXT2_SECTOR_SIZE - 1) / EXT2_SECTOR_SIZE;
	if (break_sectors > sectors)
		break_sectors = sectors;

	return (ata_read_sectors(EXT2_DRIVE, sector, break_sectors, buf) == 0 ? len : 0);
}

static uint32_t	ext2_write_block(ext2_fs_data_t *fs, uint32_t block_num, const uint8_t *buf, size_t len)
{
	if (block_num == 0)
		return (0);
	uint32_t	sector = block_num * (fs->block_size / EXT2_SECTOR_SIZE);
	uint32_t	sectors = (fs->block_size / EXT2_SECTOR_SIZE);
	if (len > fs->block_size || fs->block_size > EXT2_SECTOR_SIZE * 4)
		len = fs->block_size;
	uint32_t	break_sectors = (len + EXT2_SECTOR_SIZE - 1) / EXT2_SECTOR_SIZE;
	if (break_sectors > sectors)
		break_sectors = sectors;

	return (ata_write_sectors(EXT2_DRIVE, sector, break_sectors, buf) == 0 ? len : 0);
}

/* Read a block indirectly through a pointer block at a given index. */
static uint32_t	ext2_get_block_from_ptr(ext2_fs_data_t *fs, uint32_t block, uint32_t index_capacity, uint32_t target, int level)
{
	uint32_t	contents[1024];
	uint32_t	ptr_per_block = fs->block_size / 4;

	if (level == 0)
		return (block);

	if (ext2_read_block(fs, block, (uint8_t*)contents, fs->block_size) == 0)
		return (0);

	switch (level)
	{
		case 1:
		{
			if (target >= ptr_per_block)
				return (0);
			return (contents[target]);
		}
		case 2:
		{
			uint32_t	first = target / ptr_per_block;
			uint32_t	second = target % ptr_per_block;
			if (first >= ptr_per_block)
				return (0);
			uint32_t	next = ext2_get_block_from_ptr(fs, contents[first], ptr_per_block, second, 1);
			return (next);
		}
		default:
			return (0);
	}
}

/* Translate a logical block offset (in block units) of an inode's data to a physical block number. */
static uint32_t	ext2_block_lookup(ext2_fs_data_t *fs, ext2_inode_t *inode, uint32_t logical)
{
	uint32_t	ptr_per_block = fs->block_size / 4;
	uint32_t	direct_count = 12;
	uint32_t	indirect_max = ptr_per_block;

	if (logical < direct_count)
		return (inode->block[logical]);

	logical -= direct_count;
	if (logical < indirect_max)
		return (ext2_get_block_from_ptr(fs, inode->block[12], ptr_per_block, logical, 1));

	logical -= indirect_max;
	uint32_t	dbl_max = (uint32_t)ptr_per_block * ptr_per_block;
	if (logical < dbl_max)
		return (ext2_get_block_from_ptr(fs, inode->block[13], ptr_per_block, logical, 2));

	return (0);
}

/* Allocate a data block for a file at a given logical index, growing the file.
 * Supports direct blocks and a single indirect table (12 + ptr_per_block).
 * Updates inode->block[]/blocks in place; the caller persists the inode. */
static uint32_t	ext2_file_alloc_block(ext2_fs_data_t *fs, ext2_inode_t *inode, uint32_t logical);

int	ext2_read_data(ext2_fs_data_t *fs, ext2_inode_t *inode, uint32_t offset, uint32_t size, uint8_t *buf)
{
	if (!fs || !inode || !buf)
		return (-1);

	if (offset >= inode->size)
		return (0);
	if (offset + size > inode->size)
		size = inode->size - offset;

	uint32_t	done = 0;
	uint8_t		block_buf[4096];

	while (done < size)
	{
		uint32_t	byte_off = offset + done;
		uint32_t	block_idx = byte_off / fs->block_size;
		uint32_t	in_block = byte_off % fs->block_size;
		uint32_t	chunk = size - done;
		if (chunk > fs->block_size - in_block)
			chunk = fs->block_size - in_block;

		uint32_t	phys_block = ext2_block_lookup(fs, inode, block_idx);
		if (phys_block == 0)
			return ((int)done);

		if (in_block == 0 && chunk == fs->block_size)
		{
			if (ext2_read_block(fs, phys_block, buf + done, chunk) != chunk)
				return ((int)done);
		}
		else
		{
			if (ext2_read_block(fs, phys_block, block_buf, fs->block_size) != fs->block_size)
				return ((int)done);
			memcpy(buf + done, block_buf + in_block, chunk);
		}
		done += chunk;
	}
	return ((int)done);
}

int	ext2_write_data(ext2_fs_data_t *fs, ext2_inode_t *inode, uint32_t offset, uint32_t size, const uint8_t *buf)
{
	if (!fs || !inode || !buf)
		return (-1);

	uint32_t	done = 0;
	uint8_t		block_buf[4096];

	while (done < size)
	{
		uint32_t	byte_off = offset + done;
		uint32_t	block_idx = byte_off / fs->block_size;
		uint32_t	in_block = byte_off % fs->block_size;
		uint32_t	chunk = size - done;
		if (chunk > fs->block_size - in_block)
			chunk = fs->block_size - in_block;

		uint32_t	phys_block = ext2_block_lookup(fs, inode, block_idx);
		if (phys_block == 0 && byte_off >= inode->size)
			phys_block = ext2_file_alloc_block(fs, inode, block_idx);
		if (phys_block == 0)
			return ((int)done);

		if (byte_off >= inode->size)
			inode->size = byte_off + (size - done);

		if (in_block == 0 && chunk == fs->block_size)
		{
			if (ext2_write_block(fs, phys_block, buf + done, chunk) != chunk)
				return ((int)done);
		}
		else
		{
			if (ext2_read_block(fs, phys_block, block_buf, fs->block_size) != fs->block_size)
				return ((int)done);
			memcpy(block_buf + in_block, buf + done, chunk);
			if (ext2_write_block(fs, phys_block, block_buf, fs->block_size) != fs->block_size)
				return ((int)done);
		}
		done += chunk;
	}
	if (offset + size > inode->size)
	{
		inode->size = offset + size;
		/* size update persisted by the caller using ext2_write_inode */
	}
	return ((int)done);
}

int	ext2_write_inode(ext2_fs_data_t *fs, uint32_t inode_num, ext2_inode_t *inode)
{
	if (!fs || !fs->superblock || !fs->group_desc)
		return (-1);

	uint32_t	group = (inode_num - 1) / fs->superblock->inodes_per_group;
	uint32_t	index = (inode_num - 1) % fs->superblock->inodes_per_group;

	if (group >= fs->group_count)
		return (-1);

	uint16_t	inode_size = fs->superblock->inode_size ? fs->superblock->inode_size : 128;
	uint32_t	inode_table_block = fs->group_desc[group].inode_table;
	uint32_t	byte_off = inode_table_block * fs->block_size + index * inode_size;
	uint32_t	sector = byte_off / EXT2_SECTOR_SIZE;
	uint32_t	off_in_sector = byte_off % EXT2_SECTOR_SIZE;

	uint8_t	buf[EXT2_SECTOR_SIZE];
	if (ata_read_sectors(EXT2_DRIVE, sector, 1, buf) != 0)
		return (-1);
	memcpy(buf + off_in_sector, inode, sizeof(ext2_inode_t));
	return (ata_write_sectors(EXT2_DRIVE, sector, 1, buf) == 0 ? 0 : -1);
}

/* ================================================================
 * Block bitmap helpers
 * ================================================================ */
static void	ext2_read_bbitmap(ext2_fs_data_t *fs, uint32_t group, uint8_t *buf)
{
	uint32_t	sector = fs->group_desc[group].block_bitmap *
		(fs->block_size / EXT2_SECTOR_SIZE);
	ata_read_sectors(EXT2_DRIVE, sector,
		fs->block_size / EXT2_SECTOR_SIZE, buf);
}

static void	ext2_write_bbitmap(ext2_fs_data_t *fs, uint32_t group, const uint8_t *buf)
{
	uint32_t	sector = fs->group_desc[group].block_bitmap *
		(fs->block_size / EXT2_SECTOR_SIZE);
	ata_write_sectors(EXT2_DRIVE, sector,
		fs->block_size / EXT2_SECTOR_SIZE, buf);
}

/* ================================================================
 * Inode bitmap helpers
 * ================================================================ */
static void	ext2_read_ibitmap(ext2_fs_data_t *fs, uint32_t group, uint8_t *buf)
{
	uint32_t	sector = fs->group_desc[group].inode_bitmap *
		(fs->block_size / EXT2_SECTOR_SIZE);
	ata_read_sectors(EXT2_DRIVE, sector,
		fs->block_size / EXT2_SECTOR_SIZE, buf);
}

static void	ext2_write_ibitmap(ext2_fs_data_t *fs, uint32_t group, const uint8_t *buf)
{
	uint32_t	sector = fs->group_desc[group].inode_bitmap *
		(fs->block_size / EXT2_SECTOR_SIZE);
	ata_write_sectors(EXT2_DRIVE, sector,
		fs->block_size / EXT2_SECTOR_SIZE, buf);
}

/* ================================================================
 * Block allocation
 * ================================================================ */
int	ext2_alloc_block(ext2_fs_data_t *fs, uint32_t *out_block)
{
	uint8_t	bitmap[4096];

	if (!fs || !fs->superblock || !fs->group_desc || !out_block)
		return (-1);

	for (uint32_t g = 0; g < fs->group_count; g++)
	{
		if (fs->group_desc[g].free_blocks_count == 0)
			continue;

		ext2_read_bbitmap(fs, g, bitmap);
		uint32_t	blocks_in_group = fs->blocks_per_group;
		for (uint32_t b = 0; b < blocks_in_group; b++)
		{
			uint32_t	word = b / 32;
			uint32_t	bit = b % 32;
			if (!(bitmap[word * 4 + bit / 8] & (1 << (bit % 8))))
			{
				/* Found a free block; set it */
				bitmap[word * 4 + bit / 8] |= (1 << (bit % 8));
				ext2_write_bbitmap(fs, g, bitmap);
				fs->group_desc[g].free_blocks_count--;
				fs->superblock->free_blocks_count--;

				/* Persist the superblock */
				uint32_t	sb_sector = EXT2_SUPERBLOCK_OFFSET / EXT2_SECTOR_SIZE;
				uint8_t		sb_buf[EXT2_SECTOR_SIZE];
				ata_read_sectors(EXT2_DRIVE, sb_sector, 1, sb_buf);
				memcpy(sb_buf + (EXT2_SUPERBLOCK_OFFSET % EXT2_SECTOR_SIZE),
					fs->superblock, sizeof(ext2_superblock_t));
				ata_write_sectors(EXT2_DRIVE, sb_sector, 1, sb_buf);

				/* Persist the group descriptor */
				uint32_t	gd_sector = ext2_gd_sector(fs);
				uint8_t		gd_buf[EXT2_SECTOR_SIZE * 4];
				ata_read_sectors(EXT2_DRIVE, gd_sector,
					fs->block_size / EXT2_SECTOR_SIZE, gd_buf);
				memcpy(gd_buf, fs->group_desc,
					sizeof(ext2_group_desc_t) * fs->group_count);
				ata_write_sectors(EXT2_DRIVE, gd_sector,
					fs->block_size / EXT2_SECTOR_SIZE, gd_buf);

				*out_block = g * fs->blocks_per_group + b +
					fs->superblock->first_data_block;
				return (0);
			}
		}
	}
	return (-1); /* no free blocks */
}

void	ext2_free_block(ext2_fs_data_t *fs, uint32_t block)
{
	uint8_t	bitmap[4096];

	if (!fs || block == 0)
		return;
	uint32_t	group = (block - fs->superblock->first_data_block) /
		fs->blocks_per_group;
	uint32_t	index = (block - fs->superblock->first_data_block) %
		fs->blocks_per_group;

	ext2_read_bbitmap(fs, group, bitmap);
	uint32_t	word = index / 32;
	uint32_t	bit = index % 32;
	bitmap[word * 4 + bit / 8] &= ~(1 << (bit % 8));
	ext2_write_bbitmap(fs, group, bitmap);
	fs->group_desc[group].free_blocks_count++;
	fs->superblock->free_blocks_count++;
}

static uint32_t	ext2_file_alloc_block(ext2_fs_data_t *fs, ext2_inode_t *inode, uint32_t logical)
{
	uint32_t	ppb = fs->block_size / 4;
	uint32_t	phys;

	if (!fs || !inode)
		return (0);
	if (ppb > 1024)
		ppb = 1024;

	if (logical < 12)
	{
		if (inode->block[logical])
			return (inode->block[logical]);
		if (ext2_alloc_block(fs, &phys) != 0)
			return (0);
		inode->block[logical] = phys;
	}
	else if (logical < 12 + ppb)
	{
		uint32_t	ind[1024];
		uint32_t	idx = logical - 12;

		if (!inode->block[12])
		{
			if (ext2_alloc_block(fs, &phys) != 0)
				return (0);
			bzero(ind, fs->block_size);
			ext2_write_block(fs, phys, (uint8_t*)ind, fs->block_size);
			inode->block[12] = phys;
		}
		if (ext2_read_block(fs, inode->block[12], (uint8_t*)ind, fs->block_size) != fs->block_size)
			return (0);
		if (ind[idx])
		{
			phys = ind[idx];
			ext2_write_block(fs, inode->block[12], (uint8_t*)ind, fs->block_size);
			return (phys);
		}
		if (ext2_alloc_block(fs, &phys) != 0)
			return (0);
		ind[idx] = phys;
		ext2_write_block(fs, inode->block[12], (uint8_t*)ind, fs->block_size);
	}
	else
		return (0);

	inode->blocks += fs->block_size / EXT2_SECTOR_SIZE;
	return (phys);
}

/* ================================================================
 * Inode allocation
 * ================================================================ */
int	ext2_alloc_inode(ext2_fs_data_t *fs, uint32_t *out_ino)
{
	uint8_t	bitmap[4096];

	if (!fs || !fs->superblock || !fs->group_desc || !out_ino)
		return (-1);

	for (uint32_t g = 0; g < fs->group_count; g++)
	{
		if (fs->group_desc[g].free_inodes_count == 0)
			continue;

		ext2_read_ibitmap(fs, g, bitmap);
		for (uint32_t i = 0; i < fs->superblock->inodes_per_group; i++)
		{
			uint32_t	word = i / 32;
			uint32_t	bit = i % 32;
			if (!(bitmap[word * 4 + bit / 8] & (1 << (bit % 8))))
			{
				bitmap[word * 4 + bit / 8] |= (1 << (bit % 8));
				ext2_write_ibitmap(fs, g, bitmap);
				fs->group_desc[g].free_inodes_count--;
				fs->superblock->free_inodes_count--;

				/* Persist superblock */
				uint32_t	sb_sector = EXT2_SUPERBLOCK_OFFSET / EXT2_SECTOR_SIZE;
				uint8_t		sb_buf[EXT2_SECTOR_SIZE];
				ata_read_sectors(EXT2_DRIVE, sb_sector, 1, sb_buf);
				memcpy(sb_buf + (EXT2_SUPERBLOCK_OFFSET % EXT2_SECTOR_SIZE),
					fs->superblock, sizeof(ext2_superblock_t));
				ata_write_sectors(EXT2_DRIVE, sb_sector, 1, sb_buf);

				/* Persist group descriptors */
				uint32_t	gd_sector = ext2_gd_sector(fs);
				uint8_t		gd_buf[EXT2_SECTOR_SIZE * 4];
				ata_read_sectors(EXT2_DRIVE, gd_sector,
					fs->block_size / EXT2_SECTOR_SIZE, gd_buf);
				memcpy(gd_buf, fs->group_desc,
					sizeof(ext2_group_desc_t) * fs->group_count);
				ata_write_sectors(EXT2_DRIVE, gd_sector,
					fs->block_size / EXT2_SECTOR_SIZE, gd_buf);

				*out_ino = g * fs->superblock->inodes_per_group + i + 1;
				return (0);
			}
		}
	}
	return (-1);
}

void	ext2_free_inode(ext2_fs_data_t *fs, uint32_t ino)
{
	uint8_t	bitmap[4096];

	if (!fs || ino < 1)
		return;
	uint32_t	group = (ino - 1) / fs->superblock->inodes_per_group;
	uint32_t	index = (ino - 1) % fs->superblock->inodes_per_group;

	ext2_read_ibitmap(fs, group, bitmap);
	uint32_t	word = index / 32;
	uint32_t	bit = index % 32;
	bitmap[word * 4 + bit / 8] &= ~(1 << (bit % 8));
	ext2_write_ibitmap(fs, group, bitmap);
	fs->group_desc[group].free_inodes_count++;
	fs->superblock->free_inodes_count++;
}

/* ================================================================
 * Directory entry addition
 * ================================================================ */
int	ext2_add_dir_entry(ext2_fs_data_t *fs, uint32_t dir_ino,
	const char *name, uint32_t child_ino, uint8_t file_type)
{
	ext2_inode_t	dir_inode;
	uint8_t		name_len;
	uint16_t	rec_len;

	if (ext2_read_inode(fs, dir_ino, &dir_inode) != 0)
		return (-1);
	if (!EXT2_S_ISDIR(dir_inode.mode))
		return (-1);

	name_len = (uint8_t)strlen(name);

	/* rec_len = 8 + name_len, rounded up to 4-byte alignment */
	rec_len = (uint16_t)(8 + name_len);
	if (rec_len % 4)
		rec_len += 4 - (rec_len % 4);

	/* Scan existing directory entries to find free space at the end */
	uint32_t	offs = 0;
	uint32_t	dir_size = dir_inode.size;
	uint8_t		block_buf[4096];

	while (offs < dir_size)
	{
		uint32_t	in_blk = offs % fs->block_size;
		uint32_t	block_idx = offs / fs->block_size;

		if (in_blk == 0)
		{
			uint32_t	phys = ext2_block_lookup(fs, &dir_inode, block_idx);
			if (phys == 0)
				return (-1);
			if (ext2_read_block(fs, phys, block_buf, fs->block_size) == 0)
				return (-1);
		}

		ext2_dir_entry_t	*e = (ext2_dir_entry_t*)(block_buf + in_blk);
		if (e->rec_len == 0)
			break;

		/* Check if this entry has enough slack space to insert the new entry
		 * by splitting: the remaining space = rec_len - (8 + e->name_len).
		 * If that's >= our rec_len, we can split in-place. */
		uint16_t	e_name_len = e->name_len;
		uint16_t	e_rec_len = e->rec_len;
		uint16_t	e_total = (uint16_t)(8 + e_name_len);
		if (e_total % 4)
			e_total += 4 - (e_total % 4);
		uint16_t slack = e_rec_len - e_total;
		if (slack >= rec_len)
		{
			/* Shrink the existing entry to its actual size */
			e->rec_len = e_total;

			/* Create a new entry right after */
			ext2_dir_entry_t	*ne = (ext2_dir_entry_t*)(block_buf + in_blk + e_total);
			ne->inode = child_ino;
			ne->rec_len = slack;
			ne->name_len = name_len;
			ne->file_type = file_type;
			memcpy(ne->name, name, name_len);

			/* Write the block back */
			uint32_t	phys = ext2_block_lookup(fs, &dir_inode, block_idx);
			ext2_write_block(fs, phys, block_buf, fs->block_size);
			return (0);
		}
		offs += e_rec_len;
	}

	/* No split possible: append at the end of the directory.
	 * We need to extend the directory by one block. */
	if (offs % fs->block_size != 0)
		offs = ((offs / fs->block_size) + 1) * fs->block_size;

	uint32_t	new_block;
	if (ext2_alloc_block(fs, &new_block) != 0)
		return (-1);

	/* Zero out the new block */
	bzero(block_buf, fs->block_size);

	ext2_dir_entry_t	*ne = (ext2_dir_entry_t*)block_buf;
	ne->inode = child_ino;
	ne->rec_len = (uint16_t)fs->block_size;	/* fills entire block */
	ne->name_len = name_len;
	ne->file_type = file_type;
	memcpy(ne->name, name, name_len);

	ext2_write_block(fs, new_block, block_buf, fs->block_size);

	/* Now we need to point the directory inode to this new block.
	 * If the directory is small, use a direct pointer. */
	uint32_t	existing_blocks = dir_inode.blocks / (fs->block_size / EXT2_SECTOR_SIZE);
	if (existing_blocks < 12)
	{
		dir_inode.block[existing_blocks] = new_block;
		dir_inode.blocks += (fs->block_size / EXT2_SECTOR_SIZE);
		dir_inode.size = offs + fs->block_size;
		ext2_write_inode(fs, dir_ino, &dir_inode);
	}
	else
	{
		/* indirect block support: add to block[12] (single indirect) */
		if (dir_inode.block[12] == 0)
		{
			uint32_t	ind_block;
			if (ext2_alloc_block(fs, &ind_block) != 0)
				return (-1);
			dir_inode.block[12] = ind_block;
			/* zero the indirect block */
			uint8_t	zero_buf[4096];
			bzero(zero_buf, fs->block_size);
			ext2_write_block(fs, ind_block, zero_buf, fs->block_size);
		}
		uint32_t	indirect_idx = existing_blocks - 12;
		/* The indirect block is an array of uint32_t block pointers */
		{
			uint32_t	ind_ptr_buf[1024];
			ext2_read_block(fs, dir_inode.block[12],
				(uint8_t*)ind_ptr_buf, fs->block_size);
			ind_ptr_buf[indirect_idx] = new_block;
			ext2_write_block(fs, dir_inode.block[12],
				(uint8_t*)ind_ptr_buf, fs->block_size);
		}
		dir_inode.blocks += (fs->block_size / EXT2_SECTOR_SIZE);
		dir_inode.size = offs + fs->block_size;
		ext2_write_inode(fs, dir_ino, &dir_inode);
	}

	return (0);
}

/* ================================================================
 * High-level file/directory creation
 * ================================================================ */
int	ext2_create_file(ext2_fs_data_t *fs, uint32_t parent_ino,
	const char *name, uint32_t *out_ino)
{
	uint32_t	ino;
	ext2_inode_t	inode;

	if (!fs || !fs->superblock || !name || !name[0])
		return (-1);

	/* Allocate a new inode */
	if (ext2_alloc_inode(fs, &ino) != 0)
		return (-1);

	/* Initialize the inode as a regular file */
	bzero(&inode, sizeof(inode));
	inode.mode = EXT2_S_IFREG | 0644;	/* rw-r--r-- */
	inode.links_count = 1;
	inode.size = 0;
	inode.blocks = 0;
	ext2_write_inode(fs, ino, &inode);

	/* Add an entry in the parent directory */
	if (ext2_add_dir_entry(fs, parent_ino, name, ino, 1) != 0)
	{
		ext2_free_inode(fs, ino);
		return (-1);
	}

	if (out_ino)
		*out_ino = ino;
	return (0);
}

int	ext2_create_dir(ext2_fs_data_t *fs, uint32_t parent_ino,
	const char *name, uint32_t *out_ino)
{
	uint32_t	ino;
	ext2_inode_t	inode;
	uint32_t	block;
	uint8_t		block_buf[4096];
	ext2_dir_entry_t	*dot;
	ext2_dir_entry_t	*dotdot;

	if (!fs || !fs->superblock || !name || !name[0])
		return (-1);

	if (ext2_alloc_inode(fs, &ino) != 0)
		return (-1);

	/* Allocate a data block for the directory */
	if (ext2_alloc_block(fs, &block) != 0)
	{
		ext2_free_inode(fs, ino);
		return (-1);
	}

	/* Initialize directory with "." and ".." entries */
	bzero(block_buf, fs->block_size);

	dot = (ext2_dir_entry_t*)block_buf;
	dot->inode = ino;
	dot->rec_len = 12;
	dot->name_len = 1;
	dot->file_type = 2;	/* directory */
	dot->name[0] = '.';

	dotdot = (ext2_dir_entry_t*)(block_buf + 12);
	dotdot->inode = parent_ino;
	dotdot->rec_len = (uint16_t)(fs->block_size - 12);
	dotdot->name_len = 2;
	dotdot->file_type = 2;
	dotdot->name[0] = '.';
	dotdot->name[1] = '.';

	ext2_write_block(fs, block, block_buf, fs->block_size);

	/* Initialize the inode */
	bzero(&inode, sizeof(inode));
	inode.mode = EXT2_S_IFDIR | 0755;
	inode.links_count = 2;	/* . and parent's entry */
	inode.size = fs->block_size;
	inode.blocks = fs->block_size / EXT2_SECTOR_SIZE;
	inode.block[0] = block;
	ext2_write_inode(fs, ino, &inode);

	/* Update parent's link count */
	ext2_inode_t	parent_inode;
	if (ext2_read_inode(fs, parent_ino, &parent_inode) == 0)
	{
		parent_inode.links_count++;
		ext2_write_inode(fs, parent_ino, &parent_inode);
	}

	/* Add entry in parent */
	if (ext2_add_dir_entry(fs, parent_ino, name, ino, 2) != 0)
	{
		ext2_free_inode(fs, ino);
		ext2_free_block(fs, block);
		return (-1);
	}

	if (out_ino)
		*out_ino = ino;
	return (0);
}

/* ================================================================
 * Lookup / removal / rename
 * ================================================================ */

/* Find a directory entry by name.
 * Returns 0 and optionally the child inode and type. */
int	ext2_dir_lookup(ext2_fs_data_t *fs, uint32_t dir_ino, const char *name,
	uint32_t *out_ino, uint8_t *out_type)
{
	ext2_inode_t	dir;
	uint8_t		buf[4096];
	uint32_t	read_count = 0;
	uint32_t	pos;
	uint32_t	len;
	int		n;

	if (!fs || !name)
		return (-1);
	if (ext2_read_inode(fs, dir_ino, &dir) != 0)
		return (-1);
	if (!EXT2_S_ISDIR(dir.mode))
		return (-1);
	len = (uint32_t)strlen(name);

	while (read_count < dir.size)
	{
		uint32_t	chunk = dir.size - read_count;
		if (chunk > sizeof(buf))
			chunk = sizeof(buf);
		n = ext2_read_data(fs, &dir, read_count, chunk, buf);
		if (n <= 0)
			break;
		pos = 0;
		while (pos + 8 <= (uint32_t)n)
		{
			ext2_dir_entry_t	*e = (ext2_dir_entry_t*)(buf + pos);
			if (e->rec_len == 0)
				return (-1);
			if (e->inode != 0 && len == e->name_len &&
				memcmp(e->name, name, len) == 0)
			{
				if (out_ino)
					*out_ino = e->inode;
				if (out_type)
					*out_type = e->file_type;
				return (0);
			}
			pos += e->rec_len;
		}
		read_count += (uint32_t)n;
	}
	return (-1);
}

/* Remove a directory entry.
 * The space is reclaimed by extending the rec_len of the previous entry;
 * entries that are the first of their block are tombstoned (inode=0). */
int	ext2_remove_dir_entry(ext2_fs_data_t *fs, uint32_t dir_ino, const char *name)
{
	ext2_inode_t	dir;
	uint8_t		block_buf[4096];
	uint32_t	offs;
	uint32_t	toff;
	uint32_t	prev_abs;
	uint32_t	del_block_idx;
	uint32_t	del_phys;
	ext2_dir_entry_t	*del = 0;

	if (!fs || !name)
		return (-1);
	if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
		return (-1);
	if (ext2_dir_lookup(fs, dir_ino, name, 0, 0) != 0)
		return (-1);
	if (ext2_read_inode(fs, dir_ino, &dir) != 0)
		return (-1);

	/* Find the target entry; walk blocks like ext2_add_dir_entry does. */
	prev_abs = 0xFFFFFFFF;
	offs = 0;
	while (offs < dir.size)
	{
		uint32_t	in_blk = offs % fs->block_size;
		uint32_t	block_idx = offs / fs->block_size;

		if (in_blk == 0)
		{
			uint32_t	phys = ext2_block_lookup(fs, &dir, block_idx);
			if (phys == 0)
				return (-1);
			if (ext2_read_block(fs, phys, block_buf, fs->block_size) == 0)
				return (-1);
		}
		ext2_dir_entry_t	*e = (ext2_dir_entry_t*)(block_buf + in_blk);
		if (e->rec_len == 0)
			return (-1);
		if ((uint32_t)strlen(name) == e->name_len &&
			memcmp(e->name, name, strlen(name)) == 0)
		{
			del = e;
			toff = offs;
			break;
		}
		prev_abs = offs;
		offs += e->rec_len;
	}
	if (!del)
		return (-1);

	del_block_idx = toff / fs->block_size;
	del_phys = ext2_block_lookup(fs, &dir, del_block_idx);
	if (del_phys == 0)
		return (-1);

	/* Shrink the directory if we are removing the last entry. */
	int	shrink = (offs + del->rec_len >= dir.size);

	/* Merge into the previous entry when it lives in the same block. */
	if (prev_abs != 0xFFFFFFFF && prev_abs / fs->block_size == del_block_idx)
	{
		ext2_dir_entry_t	*prev =
			(ext2_dir_entry_t*)(block_buf + (prev_abs % fs->block_size));
		prev->rec_len = (uint16_t)((uint32_t)prev->rec_len + del->rec_len);
		if (ext2_write_block(fs, del_phys, block_buf, fs->block_size) == 0)
			return (-1);
	}
	else
	{
		/* First entry of its block: tombstone it (skipped by the reader). */
		del->inode = 0;
		if (ext2_write_block(fs, del_phys, block_buf, fs->block_size) == 0)
			return (-1);
	}

	if (shrink)
	{
		dir.size -= del->rec_len;
		ext2_write_inode(fs, dir_ino, &dir);
	}
	return (0);
}

/* Free every indirect block chain (single, double, triple) referenced by
 * an inode. Zeroes the corresponding block pointers. */
static void	ext2_free_indirect_chains(ext2_fs_data_t *fs, ext2_inode_t *inode)
{
	uint32_t	ptrs[1024];
	uint32_t	ppb = fs->block_size / 4;
	uint32_t	i;

	if (inode->block[12])
	{
		uint32_t	ind = inode->block[12];
		uint32_t	n = ppb;

		if (n > 1024)
			n = 1024;
		if (ext2_read_block(fs, ind, (uint8_t*)ptrs, fs->block_size) == 0)
			n = 0;
		for (i = 0; i < n; i++)
			if (ptrs[i])
				ext2_free_block(fs, ptrs[i]);
		ext2_free_block(fs, ind);
		inode->block[12] = 0;
	}
	if (inode->block[13])
	{
		uint32_t	dbl = inode->block[13];
		uint32_t	n = ppb;

		if (n > 1024)
			n = 1024;
		if (ext2_read_block(fs, dbl, (uint8_t*)ptrs, fs->block_size) == 0)
			n = 0;
		for (i = 0; i < n; i++)
		{
			uint32_t	inner[1024];
			uint32_t	m;
			uint32_t	j;

			if (!ptrs[i])
				continue;
			m = ppb;
			if (m > 1024)
				m = 1024;
			if (ext2_read_block(fs, ptrs[i], (uint8_t*)inner, fs->block_size) == 0)
				m = 0;
			for (j = 0; j < m; j++)
				if (inner[j])
					ext2_free_block(fs, inner[j]);
			ext2_free_block(fs, ptrs[i]);
		}
		ext2_free_block(fs, dbl);
		inode->block[13] = 0;
	}
	if (inode->block[14])
	{
		ext2_free_block(fs, inode->block[14]);
		inode->block[14] = 0;
	}
}

/* Free every block referenced by an inode (direct, single and double
 * indirect tables). */
void	ext2_free_inode_blocks(ext2_fs_data_t *fs, ext2_inode_t *inode)
{
	uint32_t	i;

	for (i = 0; i < 12; i++)
	{
		if (inode->block[i])
		{
			ext2_free_block(fs, inode->block[i]);
			inode->block[i] = 0;
		}
	}
	ext2_free_indirect_chains(fs, inode);
	inode->blocks = 0;
}

/* Remove a regular file: unlink the directory entry and free the inode. */
int	ext2_remove_file(ext2_fs_data_t *fs, uint32_t parent_ino, const char *name)
{
	uint32_t		ino;
	uint8_t		type;
	ext2_inode_t	inode;

	if (ext2_dir_lookup(fs, parent_ino, name, &ino, &type) != 0)
		return (-1);
	if (type == 2)
		return (-1);
	if (ext2_remove_dir_entry(fs, parent_ino, name) != 0)
		return (-1);
	if (ext2_read_inode(fs, ino, &inode) != 0)
		return (-1);
	if (inode.links_count > 0)
		inode.links_count--;
	inode.dtime = 0;
	ext2_free_inode_blocks(fs, &inode);
	if (inode.links_count == 0)
	{
		ext2_write_inode(fs, ino, &inode);
		ext2_free_inode(fs, ino);
	}
	else
		ext2_write_inode(fs, ino, &inode);
	return (0);
}

/* Remove an (empty) directory. */
int	ext2_remove_dir(ext2_fs_data_t *fs, uint32_t parent_ino, const char *name)
{
	uint32_t		ino;
	uint8_t		type;
	ext2_inode_t	inode;
	ext2_inode_t	parent;
	uint8_t		buf[4096];
	uint32_t		read_count = 0;
	int			n;

	if (ext2_dir_lookup(fs, parent_ino, name, &ino, &type) != 0)
		return (-1);
	if (type != 2)
		return (-1);
	if (ext2_read_inode(fs, ino, &inode) != 0)
		return (-1);
	if (!EXT2_S_ISDIR(inode.mode))
		return (-1);

	/* Must be empty: only '.' and '..' allowed. */
	while (read_count < inode.size)
	{
		uint32_t	chunk = inode.size - read_count;
		uint32_t	pos;

		if (chunk > sizeof(buf))
			chunk = sizeof(buf);
		n = ext2_read_data(fs, &inode, read_count, chunk, buf);
		if (n <= 0)
			break;
		pos = 0;
		while (pos + 12 <= (uint32_t)n)
		{
			ext2_dir_entry_t	*e = (ext2_dir_entry_t*)(buf + pos);
			if (e->rec_len == 0)
				return (-1);
			if (e->inode != 0 && !(e->name_len == 1 && e->name[0] == '.') &&
				!(e->name_len == 2 && e->name[0] == '.' && e->name[1] == '.'))
				return (-1);	/* not empty */
			pos += e->rec_len;
		}
		read_count += (uint32_t)n;
	}

	if (ext2_remove_dir_entry(fs, parent_ino, name) != 0)
		return (-1);

	/* Decrement the parent's subdirectory count. */
	if (ext2_read_inode(fs, parent_ino, &parent) == 0)
	{
		if (parent.links_count > 0)
			parent.links_count--;
		ext2_write_inode(fs, parent_ino, &parent);
	}

	inode.links_count = 0;
	inode.dtime = 0;
	ext2_free_inode_blocks(fs, &inode);
	ext2_write_inode(fs, ino, &inode);
	ext2_free_inode(fs, ino);
	return (0);
}

/* Rename a file or directory, possibly moving it to another parent. */
int	ext2_rename(ext2_fs_data_t *fs, uint32_t old_parent_ino, const char *old_name,
	uint32_t new_parent_ino, const char *new_name)
{
	uint32_t	ino;
	uint8_t		type;

	if (!fs || !old_name || !new_name || !old_name[0] || !new_name[0])
		return (-1);
	if (strcmp(new_name, ".") == 0 || strcmp(new_name, "..") == 0)
		return (-1);
	if (old_parent_ino == new_parent_ino && strcmp(old_name, new_name) == 0)
		return (0);
	if (ext2_dir_lookup(fs, old_parent_ino, old_name, &ino, &type) != 0)
		return (-1);

	/* Refuse to overwrite an existing target. */
	{
		uint32_t	tino;
		uint8_t		ttype;

		if (ext2_dir_lookup(fs, new_parent_ino, new_name, &tino, &ttype) == 0)
			return (-1);
	}

	/* Link the new name first, then unlink the old one. */
	if (ext2_add_dir_entry(fs, new_parent_ino, new_name, ino, type) != 0)
		return (-1);
	if (ext2_remove_dir_entry(fs, old_parent_ino, old_name) != 0)
		return (-1);

	/* Moving a directory between parents: update link counts and '..'. */
	if (type == 2 && old_parent_ino != new_parent_ino)
	{
		ext2_inode_t	p;

		if (ext2_read_inode(fs, old_parent_ino, &p) == 0)
		{
			if (p.links_count > 0)
				p.links_count--;
			ext2_write_inode(fs, old_parent_ino, &p);
		}
		if (ext2_read_inode(fs, new_parent_ino, &p) == 0)
		{
			p.links_count++;
			ext2_write_inode(fs, new_parent_ino, &p);
		}
		{
			ext2_inode_t	d;
			uint8_t		bbuf[4096];

			if (ext2_read_inode(fs, ino, &d) == 0 && d.size >= 12)
			{
				uint32_t	phys = ext2_block_lookup(fs, &d, 0);
				ext2_dir_entry_t	*dd;

				if (phys && ext2_read_block(fs, phys, bbuf, fs->block_size) > 0)
				{
					dd = (ext2_dir_entry_t*)(bbuf + 12);	/* '..' */
					dd->inode = new_parent_ino;
					ext2_write_block(fs, phys, bbuf, fs->block_size);
				}
			}
		}
	}
	return (0);
}

/* Truncate (or extend) a file to new_size, freeing the discarded blocks.
 * Supports files up to the single-indirect range. */
int	ext2_truncate_file(ext2_fs_data_t *fs, uint32_t ino, uint32_t new_size)
{
	ext2_inode_t	inode;
	uint32_t		new_blocks;
	uint32_t		ppb;
	uint32_t		i;

	if (!fs || !fs->superblock)
		return (-1);
	if (ext2_read_inode(fs, ino, &inode) != 0)
		return (-1);
	ppb = fs->block_size / 4;
	new_blocks = (new_size + fs->block_size - 1) / fs->block_size;

	if (new_size >= inode.size)
	{
		inode.size = new_size;
		ext2_write_inode(fs, ino, &inode);
		return (0);
	}

	if (new_blocks <= 12)
	{
		/* Free the blocks beyond the new size. */
		for (i = new_blocks; i < 12; i++)
			if (inode.block[i])
			{
				ext2_free_block(fs, inode.block[i]);
				inode.block[i] = 0;
			}
		ext2_free_indirect_chains(fs, &inode);
	}
	else if (new_blocks <= 12 + ppb)
	{
		uint32_t	kept = new_blocks - 12;
		uint32_t	ind[1024];
		uint32_t	n = ppb;
		uint32_t	ind_phys = inode.block[12];

		/* Drop double/triple indirect tables. */
		if (inode.block[13])
		{
			ext2_free_block(fs, inode.block[13]);
			inode.block[13] = 0;
		}
		if (inode.block[14])
		{
			ext2_free_block(fs, inode.block[14]);
			inode.block[14] = 0;
		}
		if (n > 1024)
			n = 1024;
		if (!ind_phys || ext2_read_block(fs, ind_phys, (uint8_t*)ind, fs->block_size) == 0)
			return (-1);
		for (i = kept; i < n; i++)
			if (ind[i])
			{
				ext2_free_block(fs, ind[i]);
				ind[i] = 0;
			}
		if (ext2_write_block(fs, ind_phys, (uint8_t*)ind, fs->block_size) == 0)
			return (-1);
	}
	else
		return (-1);	/* beyond single indirect: too large to truncate here */

	inode.size = new_size;
	inode.blocks = 0;
	for (i = 0; i < 12 && i < new_blocks; i++)
		if (inode.block[i])
			inode.blocks += fs->block_size / EXT2_SECTOR_SIZE;
	if (new_blocks > 12 && inode.block[12])
		inode.blocks += fs->block_size / EXT2_SECTOR_SIZE;
	ext2_write_inode(fs, ino, &inode);
	return (0);
}
