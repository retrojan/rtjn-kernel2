#ifndef VFS_H
# define VFS_H

# include <stdint.h>
# include <stddef.h>

# define VFS_MAX_PATH	256
# define VFS_MAX_FS		8
# define VFS_MAX_MOUNTS	8
# define VFS_MAX_OPEN	32

# define VFS_OK			0
# define VFS_ERR		(-1)
# define VFS_ERR_NOT_FOUND	(-2)
# define VFS_ERR_IS_DIR	(-3)
# define VFS_ERR_IS_FILE	(-4)
# define VFS_ERR_NO_SPACE	(-5)
# define VFS_ERR_PERM		(-6)

/* File types */
# define VFS_FT_NONE	0
# define VFS_FT_FILE	1
# define VFS_FT_DIR	2
# define VFS_FT_CHAR	3
# define VFS_FT_BLOCK	4

/* File open flags */
# define VFS_O_READ		0x01
# define VFS_O_WRITE	0x02
# define VFS_O_CREATE	0x04
# define VFS_O_APPEND	0x08

typedef struct vfs_node
{
	char		name[64];
	uint32_t	length;
	uint32_t	flags;
	uint32_t	inode;
	uint32_t	unused;
	void		*impl_data;
	/* operations */
	int		(*read)(struct vfs_node*, uint32_t, uint32_t, uint8_t*);
	int		(*write)(struct vfs_node*, uint32_t, uint32_t, const uint8_t*);
	int		(*readdir)(struct vfs_node*, uint32_t, struct vfs_node*);
	struct vfs_node *(*finddir)(struct vfs_node*, const char*);
} vfs_node_t;

typedef struct vfs_fs_ops
{
	const char	*name;
	int		(*mount)(const char *device, vfs_node_t *root);
	void		(*umount)(vfs_node_t *root);
} vfs_fs_ops_t;

typedef struct vfs_mount_point
{
	char		path[64];
	vfs_node_t	*root;
	int			fs_type;
	int			in_use;
} vfs_mount_point_t;

typedef struct vfs_fd
{
	vfs_node_t	*node;
	uint32_t	pos;
	int			flags;
	int			in_use;
} vfs_fd_t;

/* init */
void		vfs_init(void);

/* registration */
int			vfs_add_fs(vfs_fs_ops_t *ops);
int			vfs_mount(const char *device, const char *path, const char *fs_type);
int			vfs_unmount(const char *path);

/* operations */
vfs_node_t	*vfs_open_node(const char *path);
int			vfs_read(int fd, uint8_t *buf, uint32_t size);
int			vfs_write(int fd, const uint8_t *buf, uint32_t size);
int			vfs_close(int fd);
int			vfs_open(const char *path, int flags);
int			vfs_stat(const char *path, uint32_t *size, uint32_t *type);
int		vfs_readdir(const char *path, uint32_t index, vfs_node_t *out);
int		vfs_mkdir(const char *path);
int		vfs_create(const char *path, uint32_t flags);
int		vfs_unlink(const char *path);
int		vfs_rmdir(const char *path);
int		vfs_rmtree(const char *path);
int		vfs_rename(const char *oldpath, const char *newpath);
int		vfs_truncate(const char *path, uint32_t new_size);

/* Resolve a path to its parent directory, returning the leaf name in child_name.
 * Used by file/directory creation commands. */
vfs_node_t	*vfs_parent(const char *path, char *child_name, size_t child_sz);

#endif
