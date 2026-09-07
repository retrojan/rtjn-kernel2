#include <stddef.h>
#include <stdint.h>
#include "string.h"
#include "printk.h"
#include "vfs.h"
#include "ata.h"
#include "ext2.h"
#include "commands.h"

char	g_cwd[VFS_MAX_PATH] = "/";

static void	strtrim_right(char *s, char c)
{
	size_t	len = strlen(s);

	while (len > 0 && s[len - 1] == c)
		s[--len] = 0;
}

static void	canonicalize(char *path)
{
	char	parts[32][48];
	int	nparts = 0;
	char	*p = path;
	char	comp[64];
	char	out[VFS_MAX_PATH];

	if (path[0] != '/')
		return;
	p++;
	while (*p)
	{
		int	k = 0;

		while (*p && *p != '/')
		{
			if (k < 47)
			{
				comp[k++] = *p;
				comp[k] = 0;
			}
			p++;
		}
		if (*p == '/')
			p++;
		if (k == 0)
			continue;
		if (strcmp(comp, ".") == 0)
			continue;
		if (strcmp(comp, "..") == 0)
		{
			if (nparts > 0)
				nparts--;
			continue;
		}
		if (nparts < 32)
			strcpy(parts[nparts++], comp);
	}
	if (nparts == 0)
	{
		strcpy(path, "/");
		return;
	}
	out[0] = '/';
	out[1] = 0;
	for (int i = 0; i < nparts; i++)
	{
		size_t	off = strlen(out);
		size_t	plen = strlen(parts[i]);

		memcpy(out + off, parts[i], plen);
		out[off + plen] = '/';
		out[off + plen + 1] = 0;
	}
	strtrim_right(out, '/');
	if (out[0] == 0)
		strcpy(out, "/");
	strcpy(path, out);
}

void	vfs_cmd_resolve(const char *path, char *out, size_t out_sz)
{
	char	joined[VFS_MAX_PATH];

	if (!path || out_sz == 0)
		return;
	if (path[0] == '/')
	{
		strncpy(joined, path, sizeof(joined) - 1);
		joined[sizeof(joined) - 1] = 0;
	}
	else if (strcmp(path, ".") == 0 || strcmp(path, "") == 0)
	{
		strncpy(joined, g_cwd, sizeof(joined) - 1);
		joined[sizeof(joined) - 1] = 0;
	}
	else
	{
		if (g_cwd[strlen(g_cwd) - 1] == '/')
			snprintf(joined, sizeof(joined), "%s%s", g_cwd, path);
		else
			snprintf(joined, sizeof(joined), "%s/%s", g_cwd, path);
	}
	canonicalize(joined);
	strncpy(out, joined, out_sz - 1);
	out[out_sz - 1] = 0;
}

static void	vfs_cwd_parent(char *out, size_t out_sz)
{
	size_t	l = strlen(g_cwd);

	if (l <= 1)
	{
		snprintf(out, out_sz, "/");
		return;
	}
	while (l > 1 && g_cwd[l - 1] == '/')
		l--;
	while (l > 1 && g_cwd[l - 1] != '/')
		l--;
	if (l <= 1)
		snprintf(out, out_sz, "/");
	else
	{
		memcpy(out, g_cwd, l - 1);
		out[l - 1] = 0;
	}
}

static int	vfs_is_dir(const char *path)
{
	vfs_node_t	*node = vfs_open_node(path);

	if (!node)
		return (0);
	return (node->flags == VFS_FT_DIR);
}

int	cmd_cd(int argc, char **argv)
{
	char	full[VFS_MAX_PATH];
	const char	*target;

	if (argc <= 1)
	{
		printk("cd: usage: cd <dir>\n");
		return (0);
	}
	target = argv[1];

	if (strcmp(target, "~") == 0)
		target = "/";

	if (strcmp(target, "..") == 0)
		vfs_cwd_parent(full, sizeof(full));
	else
		vfs_cmd_resolve(target, full, sizeof(full));

	if (!vfs_is_dir(full) && strcmp(full, "/") != 0)
	{
		printk("cd: %s: No such directory\n", target);
		return (0);
	}
	strncpy(g_cwd, full, sizeof(g_cwd) - 1);
	g_cwd[sizeof(g_cwd) - 1] = 0;
	return (0);
}

int	cmd_ls(int argc, char **argv)
{
	char	full[VFS_MAX_PATH];
	const char	*path = ".";
	vfs_node_t	node;
	uint32_t	index = 0;

	if (argc > 1)
		path = argv[1];
	vfs_cmd_resolve(path, full, sizeof(full));

	while (vfs_read_dir(full, index, &node) == VFS_OK)
	{
		if (node.flags == VFS_FT_DIR)
			printk("%s/\n", node.name);
		else
			printk("%s\n", node.name);
		index++;
	}
	if (index == 0)
		printk("(empty)\n");
	return (0);
}

int	cmd_cat(int argc, char **argv)
{
	uint8_t	buf[1024];
	int	fd;
	int	n;
	char	full[VFS_MAX_PATH];

	if (argc < 2)
	{
		printk("Usage: cat <path>\n");
		return (0);
	}
	vfs_cmd_resolve(argv[1], full, sizeof(full));
	fd = vfs_open(full, VFS_O_READ);
	if (fd < 0)
	{
		printk("cat: cannot open %s\n", argv[1]);
		return (0);
	}
	while ((n = vfs_read(fd, buf, sizeof(buf))) > 0)
	{
		for (int i = 0; i < n; i++)
		{
			if (buf[i] == '\n')
				printk("\n");
			else if (buf[i] >= 0x20 && buf[i] < 0x7F)
				printk("%c", buf[i]);
		}
	}
	vfs_close(fd);
	return (0);
}

int	cmd_mount(int argc, char **argv)
{
	int	ret;

	if (argc < 2)
	{
		printk("Usage: mount <path>   (mounts ext2 on device 0 at path)\n");
		return (0);
	}
	ret = vfs_mount("hd0", argv[1], "ext2");
	if (ret != VFS_OK)
	{
		printk("mount: failed (%d)\n", ret);
		return (0);
	}
	return (0);
}

int	cmd_pwd(int argc, char **argv)
{
	(void)argc; (void)argv;
	printk("%s\n", g_cwd);
	return (0);
}

int	cmd_df(int argc, char **argv)
{
	ata_device_t	*dev = ata_get_device(EXT2_DRIVE);

	(void)argc; (void)argv;
	if (!dev || !dev->present)
	{
		printk("df: no disk\n");
		return (0);
	}
	printk("device %d: %s\n", EXT2_DRIVE, dev->model);
	printk("  size   : %u MB\n", (uint32_t)(dev->capacity_sectors / 2048));
	printk("  sectors: %u\n", dev->capacity_sectors);
	return (0);
}

/* ================================================================
 * Touch: create an empty file
 * ================================================================ */
int	cmd_touch(int argc, char **argv)
{
	char	leaf[64];
	char	full[VFS_MAX_PATH];
	vfs_node_t	*parent;
	uint32_t	child_ino;

	if (argc < 2)
	{
		printk("Usage: touch <path>\n");
		return (0);
	}

	vfs_cmd_resolve(argv[1], full, sizeof(full));
	parent = vfs_resolve_parent_exported(full, leaf, sizeof(leaf));
	if (!parent || leaf[0] == 0)
	{
		printk("touch: cannot create '%s'\n", argv[1]);
		return (0);
	}

	if (ext2_vfs_create_file(parent, leaf, &child_ino) != VFS_OK)
	{
		printk("touch: failed to create '%s'\n", argv[1]);
		return (0);
	}
	printk("created: %s (inode %d)\n", full, child_ino);
	return (0);
}

/* ================================================================
 * Mkdir: create a new directory
 * ================================================================ */
int	cmd_mkdir(int argc, char **argv)
{
	char	leaf[64];
	char	full[VFS_MAX_PATH];
	vfs_node_t	*parent;
	uint32_t	child_ino;

	if (argc < 2)
	{
		printk("Usage: mkdir <path>\n");
		return (0);
	}

	vfs_cmd_resolve(argv[1], full, sizeof(full));
	parent = vfs_resolve_parent_exported(full, leaf, sizeof(leaf));
	if (!parent || leaf[0] == 0)
	{
		printk("mkdir: cannot create '%s'\n", argv[1]);
		return (0);
	}

	if (ext2_vfs_create_dir(parent, leaf, &child_ino) != VFS_OK)
	{
		printk("mkdir: failed to create '%s'\n", argv[1]);
		return (0);
	}
	printk("created dir: %s (inode %d)\n", full, child_ino);
	return (0);
}

/* ================================================================
 * Echo write: echo_write <text> <path>
 *   or: echo_write <path> — create empty file
 * ================================================================ */
int	cmd_echo_write(int argc, char **argv)
{
	const char	*path;
	char	leaf[64];
	char	full[VFS_MAX_PATH];
	vfs_node_t	*parent;
	uint32_t	child_ino;
	char	content[512];
	size_t	pos = 0;

	if (argc < 2)
	{
		printk("Usage: echo_write <text> <path>\n");
		printk("       echo_write <path>       (create empty file)\n");
		return (0);
	}

	if (argc == 2)
	{
		/* Just touch the file */
		vfs_cmd_resolve(argv[1], full, sizeof(full));
		parent = vfs_resolve_parent_exported(full, leaf, sizeof(leaf));
		if (!parent || leaf[0] == 0)
		{
			printk("echo: cannot create '%s'\n", argv[1]);
			return (0);
		}
		if (ext2_vfs_create_file(parent, leaf, &child_ino) != VFS_OK)
		{
			printk("echo: failed to create '%s'\n", argv[1]);
			return (0);
		}
		printk("created: %s\n", full);
		return (0);
	}

	/* argc >= 3: last arg is path, the rest is text */
	path = argv[argc - 1];
	vfs_cmd_resolve(path, full, sizeof(full));

	for (int i = 1; i < argc - 1; i++)
	{
		if (i > 1 && pos < sizeof(content) - 1)
			content[pos++] = ' ';
		for (size_t k = 0; argv[i][k] && pos < sizeof(content) - 1; k++)
			content[pos++] = argv[i][k];
	}
	content[pos] = 0;

	parent = vfs_resolve_parent_exported(full, leaf, sizeof(leaf));
	if (!parent || leaf[0] == 0)
	{
		printk("echo: cannot create '%s'\n", path);
		return (0);
	}
	if (ext2_vfs_create_file(parent, leaf, &child_ino) != VFS_OK)
	{
		printk("echo: failed to create '%s'\n", path);
		return (0);
	}

	int	fd = vfs_open(full, VFS_O_READ | VFS_O_WRITE);
	if (fd < 0)
	{
		printk("echo: cannot open '%s'\n", path);
		return (0);
	}
	int	n = vfs_write(fd, (const uint8_t*)content, (uint32_t)pos);
	vfs_close(fd);
	printk("wrote %d bytes to %s\n", n, full);
	return (0);
}

/* ================================================================
 * Rm: remove files (optionally recursively)
 * ================================================================ */
int	cmd_rm(int argc, char **argv)
{
	int	recursive = 0;
	int	start = 1;

	if (argc < 2)
	{
		printk("Usage: rm <path>...      (remove files)\n");
		printk("       rm -r <path>...   (remove directories recursively)\n");
		return (0);
	}
	if (strcmp(argv[1], "-r") == 0 || strcmp(argv[1], "-rf") == 0 ||
		strcmp(argv[1], "-fr") == 0)
	{
		recursive = 1;
		start = 2;
	}
	for (int i = start; i < argc; i++)
	{
		char	full[VFS_MAX_PATH];
		vfs_node_t	*node;

		vfs_cmd_resolve(argv[i], full, sizeof(full));
		node = vfs_open_node(full);

		if (!node)
		{
			printk("rm: %s: no such file\n", argv[i]);
			continue;
		}
		if (node->flags == VFS_FT_DIR && !recursive)
		{
			printk("rm: %s: is a directory (use rm -r)\n", argv[i]);
			continue;
		}
		int	ret = recursive ? vfs_rm_recursive(full) : vfs_unlink(full);
		if (ret == VFS_OK)
			printk("removed: %s\n", full);
		else
			printk("rm: %s: failed (%d)\n", argv[i], ret);
	}
	return (0);
}

/* ================================================================
 * Rmdir: remove an empty directory
 * ================================================================ */
int	cmd_rmdir(int argc, char **argv)
{
	if (argc < 2)
	{
		printk("Usage: rmdir <path>...   (remove empty directories)\n");
		return (0);
	}
	for (int i = 1; i < argc; i++)
	{
		char	full[VFS_MAX_PATH];

		vfs_cmd_resolve(argv[i], full, sizeof(full));
		int	ret = vfs_rmdir(full);
		if (ret == VFS_OK)
			printk("removed: %s\n", full);
		else if (ret == VFS_ERR_IS_FILE)
			printk("rmdir: %s: not a directory\n", argv[i]);
		else
			printk("rmdir: %s: failed (%d)\n", argv[i], ret);
	}
	return (0);
}

/* ================================================================
 * Mv: rename or move a file/directory
 * ================================================================ */
int	cmd_mv(int argc, char **argv)
{
	char	full1[VFS_MAX_PATH];
	char	full2[VFS_MAX_PATH];

	if (argc < 3)
	{
		printk("Usage: mv <source> <dest>\n");
		printk("       (if <dest> is a directory, <source> is moved into it)\n");
		return (0);
	}
	vfs_cmd_resolve(argv[1], full1, sizeof(full1));
	vfs_cmd_resolve(argv[2], full2, sizeof(full2));
	if (vfs_open_node(full1) == 0)
	{
		printk("mv: %s: no such file\n", argv[1]);
		return (0);
	}
	int	ret = vfs_rename(full1, full2);
	if (ret != VFS_OK)
		printk("mv: %s -> %s failed (%d)\n", full1, full2, ret);
	else
		printk("%s -> %s\n", full1, full2);
	return (0);
}
