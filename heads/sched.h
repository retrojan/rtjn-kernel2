#ifndef SCHED_H
# define SCHED_H

# include <stdint.h>

# define SCHED_MAX_TASKS	8
# define SCHED_KSTACK_SIZE	4096

enum
{
	S_EMPTY = 0,
	S_RUNNABLE,
	S_RUNNING,
	S_ZOMBIE
};

typedef struct s_task
{
	uint32_t	pid;
	char		name[16];
	int			state;
	uint32_t	saved_esp;	/* resume frame pointer (gs slot) */
	uint32_t	kstack_top;	/* TSS esp0 target while this task runs */
	uint32_t	kstack_bot;
	uint32_t	entry;
	uint32_t	user_esp;
}			task_t;

extern volatile uint32_t	sched_cur_esp;
extern volatile uint32_t	sched_next_esp;
extern volatile uint32_t	sched_force_switch;
extern volatile int			sched_active;

int			sched_init_kernel(void);
int			sched_add_user_task(const char *name, uint32_t entry, uint32_t user_esp);
void		sched_tick(void);
void		sched_yield(void);
void		sched_exit(int status);
uint32_t	sched_current_pid(void);
int			sched_task_count(void);
int			sched_pid_alive(uint32_t pid);
void		sched_list_tasks(void);

#endif