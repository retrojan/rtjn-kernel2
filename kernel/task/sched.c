#include <stdint.h>
#include <stddef.h>
#include "string.h"
#include "printk.h"
#include "io.h"
#include "gdt.h"
#include "sched.h"

static task_t	g_tasks[SCHED_MAX_TASKS];
static uint8_t	g_kstacks[SCHED_MAX_TASKS][SCHED_KSTACK_SIZE]
	__attribute__((aligned(16)));

static int	g_cur = 0;
static int	g_rr = 1;
static int	g_enabled = 0;
static uint32_t	g_next_pid = 1;

volatile uint32_t	sched_cur_esp = 0;
volatile uint32_t	sched_next_esp = 0;
volatile uint32_t	sched_force_switch = 0;
volatile int		sched_active = 0;

static int	sched_pick(void)
{
	for (int step = 0; step < SCHED_MAX_TASKS; step++)
	{
		int	i = (g_rr + step) % SCHED_MAX_TASKS;

		if (g_tasks[i].state == S_RUNNABLE)
		{
			g_rr = (i + 1) % SCHED_MAX_TASKS;
			return (i);
		}
	}
	/* Nothing explicitly runnable: fall back to any live task so the
	 * shell (pid 0) keeps running instead of hanging. */
	for (int step = 0; step < SCHED_MAX_TASKS; step++)
	{
		int	i = (g_rr + step) % SCHED_MAX_TASKS;

		if (g_tasks[i].state != S_EMPTY && g_tasks[i].state != S_ZOMBIE)
			return (i);
	}
	return (g_cur);
}

/* Build the initial resume frame for a task that has never run yet.
 * Mirrors exactly what irq_common_stub expects: gs..ds, regs from pusha,
 * int_no/err dummy, then eip/cs/eflags/useresp/ss -> iret enters ring 3. */
static void	sched_build_initial_frame(task_t *t)
{
	regs_t	*fr = (regs_t*)(t->kstack_top - sizeof(regs_t));

	bzero(fr, sizeof(regs_t));
	fr->gs = fr->fs = fr->es = fr->ds = GDT_SEL_USER_DATA;
	fr->eip = t->entry;
	fr->cs = GDT_SEL_USER_CODE;
	fr->eflags = 0x202;
	fr->useresp = t->user_esp;
	fr->ss = GDT_SEL_USER_DATA;
	fr->esp = t->user_esp;
	t->saved_esp = (uint32_t)&fr->gs;
}

int	sched_init(void)
{
	bzero(g_tasks, sizeof(g_tasks));

	g_tasks[0].pid = 0;
	strcpy(g_tasks[0].name, "kernel");
	g_tasks[0].state = S_RUNNING;
	g_tasks[0].saved_esp = 0;
	g_tasks[0].kstack_top = 0;
	g_tasks[0].kstack_bot = 0;
	g_tasks[0].entry = 0;
	g_tasks[0].user_esp = 0;

	g_cur = 0;
	g_rr = 1;
	g_enabled = 1;
	sched_active = 1;

	tss_set_kernel_esp0((uint32_t)&g_kstacks[0][SCHED_KSTACK_SIZE]);
	return (0);
}

int	sched_add(const char *name, uint32_t entry, uint32_t user_esp)
{
	int	slot = -1;

	for (int i = 0; i < SCHED_MAX_TASKS; i++)
	{
		if (g_tasks[i].state == S_EMPTY || g_tasks[i].state == S_ZOMBIE)
		{
			slot = i;
			break;
		}
	}
	if (slot < 0)
		return (-1);

	task_t	*t = &g_tasks[slot];

	bzero(t, sizeof(*t));
	t->pid = g_next_pid++;
	strncpy(t->name, name, sizeof(t->name) - 1);
	t->name[sizeof(t->name) - 1] = 0;
	t->state = S_RUNNABLE;
	t->kstack_bot = (uint32_t)g_kstacks[slot];
	t->kstack_top = t->kstack_bot + SCHED_KSTACK_SIZE;
	t->entry = entry;
	t->user_esp = user_esp;
	sched_build_initial_frame(t);
	return ((int)t->pid);
}

void	sched_tick(void)
{
	if (!g_enabled)
	{
		sched_next_esp = sched_cur_esp;
		return;
	}

	task_t	*cur = &g_tasks[g_cur];

	cur->saved_esp = sched_cur_esp;

	int	next_i = sched_pick();
	task_t	*next = &g_tasks[next_i];

	if (next_i == g_cur && cur->state == S_RUNNING)
	{
		sched_next_esp = cur->saved_esp;
		return;
	}

	if (cur->state == S_RUNNING)
		cur->state = S_RUNNABLE;
	next->state = S_RUNNING;
	if (next->kstack_top)
		tss_set_kernel_esp0(next->kstack_top);
	sched_next_esp = next->saved_esp;
	g_cur = next_i;
}

void	sched_yield(void)
{
	if (!g_enabled)
		return;

	int	next_i = sched_pick();

	if (next_i == g_cur)
		return;

	task_t	*cur = &g_tasks[g_cur];
	task_t	*next = &g_tasks[next_i];

	cur->saved_esp = sched_cur_esp;
	cur->state = S_RUNNABLE;
	next->state = S_RUNNING;
	if (next->kstack_top)
		tss_set_kernel_esp0(next->kstack_top);
	sched_next_esp = next->saved_esp;
	g_cur = next_i;
	sched_force_switch = 1;
}

void	sched_exit(int status)
{
	(void)status;
	if (!g_enabled)
		return;

	task_t	*cur = &g_tasks[g_cur];

	cur->state = S_ZOMBIE;

	int	next_i = sched_pick();

	if (next_i == g_cur)
	{
		/* No one to run: stop. */
		for (;;)
			__asm__ volatile ("hlt");
	}

	task_t	*next = &g_tasks[next_i];

	next->state = S_RUNNING;
	if (next->kstack_top)
		tss_set_kernel_esp0(next->kstack_top);
	sched_next_esp = next->saved_esp;
	g_cur = next_i;
	sched_force_switch = 1;
}

uint32_t	sched_pid(void)
{
	return (g_tasks[g_cur].pid);
}

int	sched_count(void)
{
	int	n = 0;

	for (int i = 0; i < SCHED_MAX_TASKS; i++)
		if (g_tasks[i].state != S_EMPTY)
			n++;
	return (n);
}

int	sched_alive(uint32_t pid)
{
	for (int i = 0; i < SCHED_MAX_TASKS; i++)
	{
		if (g_tasks[i].pid == pid && g_tasks[i].state != S_EMPTY &&
			g_tasks[i].state != S_ZOMBIE)
			return (1);
	}
	return (0);
}

void	sched_list(void)
{
	static const char	*st[] = { "empty", "ready", "running", "zombie" };

	printk("pid state   name\n");
	printk("--- -----   ----\n");
	for (int i = 0; i < SCHED_MAX_TASKS; i++)
	{
		if (g_tasks[i].state == S_EMPTY)
			continue;
		printk(" %d  %-7s  %s\n", g_tasks[i].pid,
			st[g_tasks[i].state & 3], g_tasks[i].name);
	}
}