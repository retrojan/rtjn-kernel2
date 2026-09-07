#ifndef SYSCALL_H
# define SYSCALL_H

/* Syscall numbers shared between the kernel (ring 0) and user programs (ring 3). */
# define SYS_WRITE	1
# define SYS_EXIT	2
# define SYS_GETPID	3
# define SYS_YIELD	4
# define SYS_TICKS	5

#endif