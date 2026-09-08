#ifndef COMMANDS_H
# define COMMANDS_H

# include <stddef.h>
# include <stdint.h>
# include "vfs.h"

# define MAX_ARGS		16
# define MAX_ARG_LEN	64

typedef struct s_command
{
	const char	*name;
	const char	*help;
	int			(*func)(int argc, char **argv);
}			t_command;

int		cmd_help(int argc, char **argv);
int		cmd_clear(int argc, char **argv);
int		cmd_uname(int argc, char **argv);
int		cmd_whoami(int argc, char **argv);
int		cmd_version(int argc, char **argv);
int		cmd_uptime(int argc, char **argv);
int		cmd_meminfo(int argc, char **argv);
int		cmd_history(int argc, char **argv);
int		cmd_echo(int argc, char **argv);
int		cmd_color(int argc, char **argv);
int		cmd_ctest(int argc, char **argv);
int		cmd_sleep(int argc, char **argv);
int		cmd_calc(int argc, char **argv);
int		cmd_hexdump(int argc, char **argv);
int		cmd_cpuid(int argc, char **argv);
int		cmd_gdt(int argc, char **argv);
int		cmd_idt(int argc, char **argv);
int		cmd_irq(int argc, char **argv);
int		cmd_hlt(int argc, char **argv);
int		cmd_tfault(int argc, char **argv);
int		cmd_stkp(int argc, char **argv);

int		cmd_info(int argc, char **argv);
int		cmd_arch(int argc, char **argv);
int		cmd_pit(int argc, char **argv);
int		cmd_date(int argc, char **argv);
int		cmd_cpu(int argc, char **argv);
int		cmd_colors(int argc, char **argv);

int		cmd_cd(int argc, char **argv);
int		cmd_ls(int argc, char **argv);
int		cmd_cat(int argc, char **argv);
extern char	g_cwd[VFS_MAX_PATH];
void		vfs_cmd_resolve(const char *path, char *out, size_t out_sz);
int		cmd_mount(int argc, char **argv);
int		cmd_pwd(int argc, char **argv);
int		cmd_df(int argc, char **argv);
int		cmd_touch(int argc, char **argv);
int		cmd_mkdir(int argc, char **argv);
int		cmd_echo_write(int argc, char **argv);
int		cmd_rm(int argc, char **argv);
int		cmd_rmdir(int argc, char **argv);
int		cmd_mv(int argc, char **argv);
int		cmd_nano(int argc, char **argv);
int		cmd_vi(int argc, char **argv);
int		cmd_usb(int argc, char **argv);
int		cmd_uhci(int argc, char **argv);
int		cmd_ps(int argc, char **argv);
int		cmd_exec(int argc, char **argv);
int		cmd_ifconfig(int argc, char **argv);
int		cmd_netstat(int argc, char **argv);
int		cmd_ping(int argc, char **argv);
int		cmd_curl(int argc, char **argv);

void	shell_record(char *line);
void	shell_history_prev(void);
void	shell_history_next(void);
const char	*shell_history_get(size_t idx);
size_t	shell_history_count(void);
const char	*shell_history_current(void);
void	shell_history_pos_set(size_t p);

extern t_command	g_commands[];
extern const size_t	g_command_count;

#endif
