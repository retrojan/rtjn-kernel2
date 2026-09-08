#include <stdint.h>
#include <stddef.h>
#include "string.h"
#include "printk.h"
#include "memory.h"
#include "vfs.h"
#include "ext2.h"

extern ext2_fs_data_t	ext2_fs;
extern int		ext2_mounted;

static vfs_fs_ops_t	*vfs_filesystems[VFS_MAX_FS];
static int		vfs_fs_count = 0;
static vfs_mount_point_t	vfs_mounts[VFS_MAX_MOUNTS];
static vfs_fd_t	vfs_files[VFS_MAX_OPEN];

/* The root node of the whole VFS */
static vfs_node_t	*vfs_root = 0;

static const char	*vfs_skip_slashes(const char *path)
{
	while (*path == '/')
		path++;
	return (path);
}

/* Copy the next path component into component and advance path past it. */
static const char	*vfs_next_component(const char *path, char *component, size_t max_len)
{
	size_t	i = 0;

	while (*path && *path != '/' && *path >= 0x20 && i < max_len - 1)
		component[i++] = *path++;
	component[i] = 0;
	while (*path == '/')
		path++;
	return (path);
}

/* Find the deepest mount point that prefixes the given path. */
static vfs_mount_point_t	*vfs_find_mount(const char *path)
{
	int	best = -1;
	size_t	best_len = 0;

	for (int i = 0; i < VFS_MAX_MOUNTS; i++)
	{
		if (!vfs_mounts[i].in_use)
			continue;
		size_t	len = strlen(vfs_mounts[i].path);
		if (len < best_len)
			continue;
		if (strncmp(path, vfs_mounts[i].path, len) == 0 &&
			(path[len] == 0 || path[len] == '/'))
		{
			best = i;
			best_len = len;
			if (best_len == 0)
				best_len = 1;
		}
	}
	if (best >= 0)
		return (&vfs_mounts[best]);
	return (0);
}

static vfs_node_t	*vfs_lookup(vfs_node_t *dir, const char *name)
{
	if (dir == 0 || dir->finddir == 0)
		return (0);
	return (dir->finddir(dir, name));
}

/* Resolve the parent directory of path and copy the leaf name into child_name. */
static vfs_node_t	*vfs_find_parent(const char *path, char *child_name, size_t child_sz)
{
	const char	*p = vfs_skip_slashes(path);
	char		component[64];
	vfs_node_t	*node = vfs_root;

	if (node == 0)
		return (0);
	if (*p == 0)
	{
		if (child_name)
			strncpy(child_name, "", child_sz);
		return (node);
	}

	while (1)
	{
		p = vfs_next_component(p, component, sizeof(component));
		if (component[0] == 0)
			break;
		if (*p == 0)
		{
			if (child_name)
				strncpy(child_name, component, child_sz);
			return (node);
		}
		node = vfs_lookup(node, component);
		if (!node)
			return (0);
	}
	return (node);
}

/* Public wrapper for vfs_find_parent, used by creation commands */
vfs_node_t	*vfs_parent(const char *path, char *child_name, size_t child_sz)
{
	return (vfs_find_parent(path, child_name, child_sz));
}

void	vfs_init(void)
{
	bzero(vfs_filesystems, sizeof(vfs_filesystems));
	bzero(vfs_mounts, sizeof(vfs_mounts));
	bzero(vfs_files, sizeof(vfs_files));
	vfs_fs_count = 0;
	vfs_root = 0;
}

int	vfs_add_fs(vfs_fs_ops_t *ops)
{
	if (!ops || !ops->name || !ops->mount)
		return (VFS_ERR);
	if (vfs_fs_count >= VFS_MAX_FS)
		return (VFS_ERR);
	for (int i = 0; i < vfs_fs_count; i++)
		if (strcmp(vfs_filesystems[i]->name, ops->name) == 0)
			return (VFS_ERR);
	vfs_filesystems[vfs_fs_count++] = ops;
	return (VFS_OK);
}

int	vfs_mount(const char *device, const char *path, const char *fs_type)
{
	int	found = -1;

	if (!path || !fs_type)
		return (VFS_ERR);

	for (int i = 0; i < vfs_fs_count; i++)
		if (strcmp(vfs_filesystems[i]->name, fs_type) == 0)
		{
			found = i;
			break;
		}
	if (found < 0)
		return (VFS_ERR_NOT_FOUND);

	int	slot = -1;
	for (int i = 0; i < VFS_MAX_MOUNTS; i++)
		if (!vfs_mounts[i].in_use)
		{
			slot = i;
			break;
		}
	if (slot < 0)
		return (VFS_ERR_NO_SPACE);

	vfs_node_t	root_node;
	bzero(&root_node, sizeof(root_node));
	strcpy(root_node.name, path);

	if (vfs_filesystems[found]->mount(device, &root_node) != 0)
		return (VFS_ERR);

	vfs_mounts[slot].root = kmalloc(sizeof(vfs_node_t));
	if (!vfs_mounts[slot].root)
		return (VFS_ERR_NO_SPACE);
	memcpy(vfs_mounts[slot].root, &root_node, sizeof(vfs_node_t));

	strncpy(vfs_mounts[slot].path, path, 63);
	vfs_mounts[slot].path[63] = 0;
	vfs_mounts[slot].fs_type = found;
	vfs_mounts[slot].in_use = 1;

	if (path[0] == '/' && path[1] == 0)
		vfs_root = vfs_mounts[slot].root;

	printk("vfs: mounted %s at %s\n", fs_type, path);
	return (VFS_OK);
}

int	vfs_unmount(const char *path)
{
	for (int i = 0; i < VFS_MAX_MOUNTS; i++)
	{
		if (vfs_mounts[i].in_use && strcmp(vfs_mounts[i].path, path) == 0)
		{
			if (vfs_filesystems[vfs_mounts[i].fs_type]->umount)
				vfs_filesystems[vfs_mounts[i].fs_type]->umount(vfs_mounts[i].root);
			kfree(vfs_mounts[i].root);
			vfs_mounts[i].in_use = 0;
			if (vfs_root == vfs_mounts[i].root)
				vfs_root = 0;
			return (VFS_OK);
		}
	}
	return (VFS_ERR_NOT_FOUND);
}

vfs_node_t	*vfs_open_node(const char *path)
{
	char		comp[64];
	vfs_node_t	*parent;

	if (!path || !*path)
		return (0);

	if (path[0] == '/' && path[1] == 0)
		return (vfs_root);

	parent = vfs_find_parent(path, comp, sizeof(comp));
	if (!parent)
		return (0);
	return (vfs_lookup(parent, comp));
}

int	vfs_open(const char *path, int flags)
{
	vfs_node_t	*node = vfs_open_node(path);

	if (!node && (flags & VFS_O_CREATE))
	{
		/* Path doesn't exist yet — try to create it.
		 * We need to find the parent directory and the leaf name. */
		char	leaf[64];
		vfs_node_t	*parent = vfs_find_parent(path, leaf, sizeof(leaf));

		if (!parent || leaf[0] == 0)
			return (VFS_ERR_NOT_FOUND);

		uint32_t	child_ino;
		if (ext2_vfs_create_file(parent, leaf, &child_ino) != VFS_OK)
			return (VFS_ERR);

		/* Re-resolve now that the file exists */
		node = vfs_open_node(path);
		if (!node)
			return (VFS_ERR);
	}

	if (!node)
		return (VFS_ERR_NOT_FOUND);

	for (int i = 0; i < VFS_MAX_OPEN; i++)
	{
		if (vfs_files[i].in_use)
			continue;
		vfs_files[i].node = node;
		vfs_files[i].pos = 0;
		vfs_files[i].flags = flags;
		vfs_files[i].in_use = 1;
		return (i);
	}
	return (VFS_ERR_NO_SPACE);
}

int	vfs_close(int fd)
{
	if (fd < 0 || fd >= VFS_MAX_OPEN || !vfs_files[fd].in_use)
		return (VFS_ERR);
	vfs_files[fd].in_use = 0;
	vfs_files[fd].node = 0;
	return (VFS_OK);
}

int	vfs_read(int fd, uint8_t *buf, uint32_t size)
{
	if (fd < 0 || fd >= VFS_MAX_OPEN || !vfs_files[fd].in_use)
		return (VFS_ERR);
	vfs_node_t	*node = vfs_files[fd].node;
	if (!node || !node->read)
		return (VFS_ERR);
	int	n = node->read(node, vfs_files[fd].pos, size, buf);
	if (n > 0)
		vfs_files[fd].pos += (uint32_t)n;
	return (n);
}

int	vfs_write(int fd, const uint8_t *buf, uint32_t size)
{
	if (fd < 0 || fd >= VFS_MAX_OPEN || !vfs_files[fd].in_use)
		return (VFS_ERR);
	vfs_node_t	*node = vfs_files[fd].node;
	if (!node || !node->write)
		return (VFS_ERR);
	int	n = node->write(node, vfs_files[fd].pos, size, buf);
	if (n > 0)
		vfs_files[fd].pos += (uint32_t)n;
	return (n);
}

int	vfs_stat(const char *path, uint32_t *size, uint32_t *type)
{
	vfs_node_t	*node = vfs_open_node(path);

	if (!node)
		return (VFS_ERR_NOT_FOUND);
	if (size)
		*size = node->length;
	if (type)
		*type = node->flags;
	return (VFS_OK);
}

int	vfs_readdir(const char *path, uint32_t index, vfs_node_t *out)
{
	vfs_node_t	*node = vfs_open_node(path);

	if (!node || !node->readdir)
		return (VFS_ERR);
	if (!out)
		return (VFS_ERR);
	return (node->readdir(node, index, out));
}

int	vfs_mkdir(const char *path)
{
	(void)path;
	return (VFS_ERR);
}

int	vfs_create(const char *path, uint32_t flags)
{
	return (vfs_open(path, VFS_O_CREATE | flags));
}

int	vfs_unlink(const char *path)
{
	char		leaf[64];
	vfs_node_t	*parent;
	vfs_node_t	*node;

	if (!path || !*path)
		return (VFS_ERR);
	node = vfs_open_node(path);
	if (!node)
		return (VFS_ERR_NOT_FOUND);
	if (node->flags == VFS_FT_DIR)
		return (VFS_ERR_IS_DIR);

	parent = vfs_find_parent(path, leaf, sizeof(leaf));
	if (!parent || leaf[0] == 0)
		return (VFS_ERR_NOT_FOUND);
	if (ext2_remove_file(&ext2_fs, parent->inode, leaf) != 0)
		return (VFS_ERR);
	return (VFS_OK);
}

int	vfs_rmdir(const char *path)
{
	char		leaf[64];
	vfs_node_t	*parent;
	vfs_node_t	*node;

	if (!path || !*path)
		return (VFS_ERR);
	node = vfs_open_node(path);
	if (!node)
		return (VFS_ERR_NOT_FOUND);
	if (node->flags != VFS_FT_DIR)
		return (VFS_ERR_IS_FILE);

	parent = vfs_find_parent(path, leaf, sizeof(leaf));
	if (!parent || leaf[0] == 0)
		return (VFS_ERR_NOT_FOUND);
	if (ext2_remove_dir(&ext2_fs, parent->inode, leaf) != 0)
		return (VFS_ERR);
	return (VFS_OK);
}

/* Recursively remove a file or a directory tree. */
int	vfs_rmtree(const char *path)
{
	vfs_node_t	*node;
	vfs_node_t	child;
	uint32_t	index = 0;
	char		cpath[VFS_MAX_PATH];

	node = vfs_open_node(path);
	if (!node)
		return (VFS_ERR_NOT_FOUND);

	if (node->flags != VFS_FT_DIR)
		return (vfs_unlink(path));

	if (vfs_readdir(path, 0, &child) != VFS_OK)
		return (VFS_ERR);
	while (vfs_readdir(path, index, &child) == VFS_OK)
	{
		if (strcmp(child.name, ".") == 0 || strcmp(child.name, "..") == 0)
		{
			index++;
			continue;
		}
		if (strlen(path) + 1 + strlen(child.name) + 1 < sizeof(cpath))
		{
			snprintf(cpath, sizeof(cpath), "%s/%s", path, child.name);
			if (child.flags == VFS_FT_DIR)
				vfs_rmtree(cpath);
			else
				vfs_unlink(cpath);
		}
		index++;
	}
	return (vfs_rmdir(path));
}

/* Rename / move a file or directory.
 * If newpath is an existing directory, src is moved into it. */
int	vfs_rename(const char *oldpath, const char *newpath)
{
	char		old_leaf[64];
	char		new_leaf[64];
	char		buf[VFS_MAX_PATH];
	vfs_node_t	*old_parent;
	vfs_node_t	*new_parent;
	vfs_node_t	*dst;

	if (!oldpath || !newpath || !*oldpath || !*newpath)
		return (VFS_ERR);

	if (vfs_open_node(oldpath) == 0)
		return (VFS_ERR_NOT_FOUND);

	old_parent = vfs_find_parent(oldpath, old_leaf, sizeof(old_leaf));
	if (!old_parent || old_leaf[0] == 0)
		return (VFS_ERR_NOT_FOUND);

	dst = vfs_open_node(newpath);
	if (dst && dst->flags == VFS_FT_DIR && strcmp(newpath, oldpath) != 0)
	{
		/* Move into the directory, keeping the source name. */
		if (strlen(newpath) + 1 + strlen(old_leaf) + 1 < sizeof(buf))
			snprintf(buf, sizeof(buf), "%s/%s", newpath, old_leaf);
		else
			return (VFS_ERR);
		newpath = buf;
	}

	new_parent = vfs_find_parent(newpath, new_leaf, sizeof(new_leaf));
	if (!new_parent || new_leaf[0] == 0)
		return (VFS_ERR_NOT_FOUND);

	if (ext2_rename(&ext2_fs, old_parent->inode, old_leaf,
		new_parent->inode, new_leaf) != 0)
		return (VFS_ERR);
	return (VFS_OK);
}

int	vfs_truncate(const char *path, uint32_t new_size)
{
	vfs_node_t	*node = vfs_open_node(path);

	if (!node)
		return (VFS_ERR_NOT_FOUND);
	if (node->flags != VFS_FT_FILE)
		return (VFS_ERR_IS_DIR);
	if (ext2_truncate_file(&ext2_fs, node->inode, new_size) != 0)
		return (VFS_ERR);
	return (VFS_OK);
}
