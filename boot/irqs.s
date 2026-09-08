extern irq_dispatch
extern syscall_handler
extern sched_cur_esp
extern sched_next_esp
extern sched_force_switch
extern sched_active

global irq_stub_table
global syscall_stub

section .text
%macro irq 1
irq_%+%1:
	cli
	push	byte 0
	push	byte 32+%1
	jmp		irq_common_stub
%endmacro

irq 0
irq 1
irq 2
irq 3
irq 4
irq 5
irq 6
irq 7
irq 8
irq 9
irq 10
irq 11
irq 12
irq 13
irq 14
irq 15

global irq_stub_table
irq_stub_table:
%assign	i 0
%rep	16
		dd irq_%+i
%assign i i+1
%endrep

irq_common_stub:
	cli
	pusha
	push	ds
	push	es
	push	fs
	push	gs
	mov		[sched_cur_esp], esp
	mov		eax, esp
	mov		[sched_next_esp], eax
	mov		ax, 0x10
	mov		ds, ax
	mov		es, ax
	mov		fs, ax
	mov		gs, ax
	mov		eax, esp
	push	eax
	mov		eax, irq_dispatch
	call	eax
	add		esp, 4
	mov		eax, [sched_active]
	test	eax, eax
	jz		.noext
	mov		esp, [sched_next_esp]
.noext:
	pop		gs
	pop		fs
	pop		es
	pop		ds
	popa
	add		esp, 8
	iret

syscall_stub:
	cli
	push	dword 0
	push	dword 0x80
	jmp		sys_common_stub

sys_common_stub:
	pusha
	push	ds
	push	es
	push	fs
	push	gs
	mov		[sched_cur_esp], esp
	mov		ax, 0x10
	mov		ds, ax
	mov		es, ax
	mov		fs, ax
	mov		gs, ax
	mov		eax, esp
	push	eax
	mov		eax, syscall_handler
	call	eax
	add		esp, 4
	mov		eax, [sched_force_switch]
	test	eax, eax
	jz		.noswitch
	mov		dword [sched_force_switch], 0
	mov		esp, [sched_next_esp]
.noswitch:
	pop		gs
	pop		fs
	pop		es
	pop		ds
	popa
	add		esp, 8
	iret