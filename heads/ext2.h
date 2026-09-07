#ifndef EXT2_H
# define EXT2_H

# include <stdint.h>
# include <stddef.h>
# include "vfs.h"

/* ext2 superblock offsets */
# define EXT2_SUPERBLOCK_OFFSET	1024
# define EXT2_MAGIC				0xEF53
# define EXT2_ROOT_INO			2

/* ATA device index the ext2 filesystem lives on.
 * QEMU index: 0 = ide0-master (boot), 1 = ide0-slave,
 *             2 = ide1-master,       3 = ide1-slave. */
# define EXT2_DRIVE				2

/* ext2 inode modes */
# define EXT2_S_IFSOCK	0xC000
# define EXT2_S_IFLNK	0xA000
# define EXT2_S_IFREG	0x8000
# define EXT2_S_IFBLK	0x6000
# define EXT2_S_IFDIR	0x4000
# define EXT2_S_IFCHR	0x2000
# define EXT2_S_IFIFO	0x1000

# define EXT2_S_ISDIR(m)	(((m) & 0xF000) == EXT2_S_IFDIR)
# define EXT2_S_ISREG(m)	(((m) & 0xF000) == EXT2_S_IFREG)

# define EXT2_BLOCK_SIZE(sb)	(1024 << (sb)->log_block_size)

typedef struct ext2_superblock
{
	uint32_t	inodes_count;
	uint32_t	blocks_count;
	uint32_t	r_blocks_count;
	uint32_t	free_blocks_count;
	uint32_t	free_inodes_count;
	uint32_t	first_data_block;
	uint32_t	log_block_size;
	uint32_t	log_frag_size;
	uint32_t	blocks_per_group;
	uint32_t	frags_per_group;
	uint32_t	inodes_per_group;
	uint32_t	mtime;
	uint32_t	wtime;
	uint16_t	mnt_count;
	uint16_t	max_mnt_count;
	uint16_t	magic;
	uint16_t	state;
	uint16_t	errors;
	uint16_t	minor_rev_level;
	uint32_t	lastcheck;
	uint32_t	checkinterval;
	uint32_t	creator_os;
	uint32_t	rev_level;
	uint16_t	def_resuid;
	uint16_t	def_resgid;
	/* ext2 specific */
	uint32_t	first_ino;
	uint16_t	inode_size;
	uint16_t	block_group_nr;
	uint32_t	feature_compat;
	uint32_t	feature_incompat;
	uint32_t	feature_ro_compat;
	uint8_t		uuid[16];
	char		volume_name[16];
	char		last_mounted[64];
	uint32_t	algorithm_usage_bitmap;
} ext2_superblock_t;

typedef struct ext2_group_desc
{
	uint32_t	block_bitmap;
	uint32_t	inode_bitmap;
	uint32_t	inode_table;
	uint16_t	free_blocks_count;
	uint16_t	free_inodes_count;
	uint16_t	used_dirs_count;
	uint16_t	pad;
	uint8_t		reserved[12];
} ext2_group_desc_t;

typedef struct ext2_inode
{
	uint16_t	mode;
	uint16_t	uid;
	uint32_t	size;
	uint32_t	atime;
	uint32_t	ctime;
	uint32_t	mtime;
	uint32_t	dtime;
	uint16_t	gid;
	uint16_t	links_count;
	uint32_t	blocks;
	uint32_t	flags;
	uint32_t	osd1;
	uint32_t	block[15];
	uint32_t	version;
	uint32_t	file_acl;
	uint32_t	dir_acl;
	uint32_t	frag_addr;
	uint8_t		osd2[12];
} ext2_inode_t;

typedef struct ext2_dir_entry
{
	uint32_t	inode;
	uint16_t	rec_len;
	uint8_t		name_len;
	uint8_t		file_type;
	char		name[];
} ext2_dir_entry_t;

typedef struct ext2_fs_data
{
	ext2_superblock_t	*superblock;
	uint8_t				*block_buffer;
	uint32_t			block_size;
	uint32_t			blocks_per_group;
	uint32_t			inodes_per_group;
	uint32_t			group_count;
	ext2_group_desc_t	*group_desc;
} ext2_fs_data_t;

void	ext2_init(void);

/* low-level ext2 operations */
int		ext2_read_superblock(uint8_t drive, ext2_superblock_t *sb);
int		ext2_read_inode(ext2_fs_data_t *fs, uint32_t inode_num, ext2_inode_t *inode);
int		ext2_write_inode(ext2_fs_data_t *fs, uint32_t inode_num, ext2_inode_t *inode);
int		ext2_read_data(ext2_fs_data_t *fs, ext2_inode_t *inode, uint32_t offset, uint32_t size, uint8_t *buf);
int		ext2_write_data(ext2_fs_data_t *fs, ext2_inode_t *inode, uint32_t offset, uint32_t size, const uint8_t *buf);

/* block and inode allocation */
int		ext2_alloc_block(ext2_fs_data_t *fs, uint32_t *out_block);
void	ext2_free_block(ext2_fs_data_t *fs, uint32_t block);
int		ext2_alloc_inode(ext2_fs_data_t *fs, uint32_t *out_ino);
void	ext2_free_inode(ext2_fs_data_t *fs, uint32_t ino);

/* directory operations */
int		ext2_add_dir_entry(ext2_fs_data_t *fs, uint32_t dir_ino, const char *name, uint32_t child_ino, uint8_t file_type);
int		ext2_remove_dir_entry(ext2_fs_data_t *fs, uint32_t dir_ino, const char *name);
int		ext2_dir_lookup(ext2_fs_data_t *fs, uint32_t dir_ino, const char *name, uint32_t *out_ino, uint8_t *out_type);
void	ext2_free_inode_blocks(ext2_fs_data_t *fs, ext2_inode_t *inode);

/* high-level creation */
int		ext2_create_file(ext2_fs_data_t *fs, uint32_t parent_ino, const char *name, uint32_t *out_ino);
int		ext2_create_dir(ext2_fs_data_t *fs, uint32_t parent_ino, const char *name, uint32_t *out_ino);

/* high-level removal / rename / truncate */
int		ext2_remove_file(ext2_fs_data_t *fs, uint32_t parent_ino, const char *name);
int		ext2_remove_dir(ext2_fs_data_t *fs, uint32_t parent_ino, const char *name);
int		ext2_rename(ext2_fs_data_t *fs, uint32_t old_parent_ino, const char *old_name,
		uint32_t new_parent_ino, const char *new_name);
int		ext2_truncate_file(ext2_fs_data_t *fs, uint32_t ino, uint32_t new_size);

int		ext2_vfs_mount(const char *device, vfs_node_t *root);
void	ext2_vfs_umount(vfs_node_t *root);
int		ext2_vfs_register(void);
int		ext2_vfs_create_file(vfs_node_t *parent_dir, const char *name, uint32_t *child_ino);
int		ext2_vfs_create_dir(vfs_node_t *parent_dir, const char *name, uint32_t *child_ino);

#endif
