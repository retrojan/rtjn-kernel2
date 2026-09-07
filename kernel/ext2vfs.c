#include <stdint.h>
#include <stddef.h>
#include "string.h"
#include "printk.h"
#include "memory.h"
#include "vfs.h"
#include "ext2.h"

/* The global ext2 open filesystem state, populated by ext2_init().
 * Exposed so the VFS driver can reach it. */
extern ext2_fs_data_t	ext2_fs;
extern int		ext2_mounted;

static vfs_node_t	*ext2_node_finddir(vfs_node_t *node, const char *name);

static int	ext2_node_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buf)
{
	ext2_inode_t	inode;

	if (!ext2_mounted)
		return (VFS_ERR);
	if (ext2_read_inode(&ext2_fs, node->inode, &inode) != 0)
		return (VFS_ERR);
	return (ext2_read_data(&ext2_fs, &inode, offset, size, buf));
}

static int	ext2_node_write(vfs_node_t *node, uint32_t offset, uint32_t size, const uint8_t *buf)
{
	ext2_inode_t	inode;

	if (!ext2_mounted)
		return (VFS_ERR);
	if (ext2_read_inode(&ext2_fs, node->inode, &inode) != 0)
		return (VFS_ERR);
	uint32_t	orig = inode.size;
	int	n = ext2_write_data(&ext2_fs, &inode, offset, size, buf);
	if (n > 0 && offset + (uint32_t)n > orig)
	{
		inode.size = offset + (uint32_t)n;
		ext2_write_inode(&ext2_fs, node->inode, &inode);
	}
	node->length = inode.size;
	return (n);
}

static int	ext2_node_readdir(vfs_node_t *node, uint32_t index, vfs_node_t *out)
{
	ext2_inode_t	inode;
	uint8_t		buf[4096];
	uint32_t	read_count = 0;
	uint32_t	pos = 0;
	uint32_t	entry_index = 0;

	if (!ext2_mounted)
		return (VFS_ERR);
	if (ext2_read_inode(&ext2_fs, node->inode, &inode) != 0)
		return (VFS_ERR);
	if (!EXT2_S_ISDIR(inode.mode))
		return (VFS_ERR_IS_FILE);

	while (read_count < inode.size)
	{
		uint32_t	chunk = inode.size - read_count;
		if (chunk > sizeof(buf))
			chunk = sizeof(buf);
		int	n = ext2_read_data(&ext2_fs, &inode, read_count, chunk, buf);
		if (n <= 0)
			break;
		pos = 0;
		while (pos + 8 <= (uint32_t)n)
		{
			ext2_dir_entry_t	*e = (ext2_dir_entry_t*)(buf + pos);
			if (e->rec_len == 0)
				break;
			if (e->inode == 0)
			{
				/* Tombstoned (deleted) entry: skip it. */
				pos += e->rec_len;
				continue;
			}
			if (entry_index == index)
			{
				bzero(out, sizeof(*out));
				size_t	nl = e->name_len;
				if (nl > 63)
					nl = 63;
				memcpy(out->name, e->name, nl);
				out->name[nl] = 0;
				out->inode = e->inode;
				out->length = 0;
				out->flags = (e->file_type == 2) ? VFS_FT_DIR : VFS_FT_FILE;
				out->read = (e->file_type == 2) ? 0 : ext2_node_read;
				out->write = (e->file_type == 2) ? 0 : ext2_node_write;
				out->readdir = (e->file_type == 2) ? ext2_node_readdir : 0;
				out->finddir = (e->file_type == 2) ? ext2_node_finddir : 0;
				if (!out->read && !(e->file_type == 2))
				{
					out->read = ext2_node_read;
					out->write = ext2_node_write;
				}
				return (VFS_OK);
			}
			entry_index++;
			pos += e->rec_len;
		}
		read_count += (uint32_t)n;
	}
	return (VFS_ERR_NOT_FOUND);
}

static vfs_node_t	*ext2_node_finddir(vfs_node_t *node, const char *name)
{
	static vfs_node_t	nodes[8];
	static uint32_t	next = 0;
	vfs_node_t	child;
	uint32_t	index = 0;

	while (ext2_node_readdir(node, index++, &child) == VFS_OK)
	{
		if (strcmp(child.name, name) == 0)
		{
			vfs_node_t	*result = &nodes[next++];
			next %= 8;
			memcpy(result, &child, sizeof(vfs_node_t));
			return (result);
		}
	}
	return (0);
}

int	ext2_vfs_mount(const char *device, vfs_node_t *root)
{
	ext2_inode_t	root_inode;

	(void)device;
	if (!ext2_mounted)
		return (VFS_ERR);
	if (ext2_read_inode(&ext2_fs, EXT2_ROOT_INO, &root_inode) != 0)
		return (VFS_ERR);
	if (!EXT2_S_ISDIR(root_inode.mode))
		return (VFS_ERR);

	root->inode = EXT2_ROOT_INO;
	root->length = root_inode.size;
	root->flags = VFS_FT_DIR;
	root->readdir = ext2_node_readdir;
	root->finddir = ext2_node_finddir;
	return (VFS_OK);
}

void	ext2_vfs_umount(vfs_node_t *root)
{
	(void)root;
}

static vfs_fs_ops_t	ext2_fs_ops = {
	.name = "ext2",
	.mount = ext2_vfs_mount,
	.umount = ext2_vfs_umount,
};

int	ext2_vfs_register(void)
{
	return (vfs_register_fs(&ext2_fs_ops));
}

/* Create a new file in the given VFS parent directory.
 * parent_dir must be a valid directory node (with inode set).
 * Returns 0 on success and fills child_ino. */
int	ext2_vfs_create_file(vfs_node_t *parent_dir, const char *name, uint32_t *child_ino)
{
	if (!ext2_mounted || !parent_dir || !name || !name[0])
		return (VFS_ERR);

	/* Strip leading slashes from the name */
	while (*name == '/')
		name++;
	if (*name == 0)
		return (VFS_ERR);

	uint32_t	ino;
	if (ext2_create_file(&ext2_fs, parent_dir->inode, name, &ino) != 0)
		return (VFS_ERR);

	if (child_ino)
		*child_ino = ino;
	return (VFS_OK);
}

/* Create a new directory in the given VFS parent directory. */
int	ext2_vfs_create_dir(vfs_node_t *parent_dir, const char *name, uint32_t *child_ino)
{
	if (!ext2_mounted || !parent_dir || !name || !name[0])
		return (VFS_ERR);

	while (*name == '/')
		name++;
	if (*name == 0)
		return (VFS_ERR);

	uint32_t	ino;
	if (ext2_create_dir(&ext2_fs, parent_dir->inode, name, &ino) != 0)
		return (VFS_ERR);

	if (child_ino)
		*child_ino = ino;
	return (VFS_OK);
}
