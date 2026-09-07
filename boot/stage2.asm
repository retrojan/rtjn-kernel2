;===========================================================================
;  rtjn bootloader - Stage 2
;
;  Loaded by stage 1 at linear 0x1000. Runs in real mode, then:
;    1. collects the BIOS E820 memory map into a Multiboot-format map
;    2. builds a minimal Multiboot info structure
;    3. loads the kernel ELF file from disk into a scratch area
;    4. switches to protected mode
;    5. copies the ELF LOAD segments to their virtual addresses
;    6. jumps to the kernel entry point (EAX=magic, EBX=mbd)
;
;  Build-time defines (passed by the Makefile):
;    KERNEL_LBA     - first LBA of the kernel ELF file
;    KERNEL_SECTORS - number of sectors the kernel file occupies
;
;  Memory layout (linear):
;    0x2000  Multiboot info structure (mbd)
;    0x2020  Multiboot memory map (24-byte entries)
;    0x2100  E820 scratch
;    0x10000 Kernel ELF file scratch area
;===========================================================================

[org 0x1000]
[bits 16]

%ifndef KERNEL_LBA
%define KERNEL_LBA 17
%endif
%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 1
%endif

MBD_LIN      equ 0x2000      ; multiboot info structure
MMAP_LIN     equ 0x2020      ; multiboot memory map
E820_SCRATCH equ 0x2100      ; E820 entry scratch (20 bytes)
KERNEL_LOAD  equ 0x10000     ; kernel ELF file scratch

KERNEL_PHYS  equ 0x100000    ; where the kernel is relocated to

start:
    cli                             ; keep IF=0: BIOS calls below must not be
                                    ; interrupted (PIT would corrupt the return)
    xor  ax, ax
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  sp, 0x7c00                 ; real-mode stack below stage 1

    mov  [boot_drive], dl

    mov  si, msg_stage2
    call vga_print

    ; force 80x25 color text mode so direct VGA writes at 0xB8000 are visible
    ; (CSM may leave the video in graphics mode on real hardware)
    mov  ax, 0x0003
    int  0x10

    ; ---- 1. detect memory (E820) ----
    call e820_collect

    ; ---- 2. build minimal Multiboot info struct ----
    mov  word [MBD_LIN], 0x40           ; flags: memory map present
    mov  ax, [mmap_count]
    mov  cx, 24
    mul  cx
    mov  [MBD_LIN + 44], ax             ; mmap_length
    mov  dword [MBD_LIN + 48], MMAP_LIN ; mmap_addr

    ; ---- 3. force the A20 gate ON before the loaded kernel could be
    ;          clobbered by the a20_is_on memory-wrap probe below.
    ;          NB: a20_is_on writes a test byte to linear 0x18000, which
    ;          lies INSIDE the kernel scratch buffer (0x10000-0x2cA00).
    ;          Doing it here, before load_kernel, keeps the ELF intact.
    call enable_a20

    ; ---- 4. load kernel ELF ----
    mov  si, msg_loadk
    call vga_print
    call load_kernel

    ; ---- 5. switch to protected mode ----
    call switch_pm

;===========================================================================
;  Now in protected mode (flat 32-bit).
;===========================================================================
[bits 32]
pm_entry:
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax
    mov  esp, 0x9fc00

    call relocate_elf

    mov  eax, 0x2badb002               ; MULTIBOOT_BOOTLOADER_MAGIC
    mov  ebx, MBD_LIN                  ; pointer to multiboot info
    mov  ecx, [entry_addr]
    push dword 0x08
    push ecx
    retf                               ; far return -> 0x08:entry

.parked:
    cli
    hlt
    jmp  .parked

;===========================================================================
;  Real mode helpers
;===========================================================================
[bits 16]

;---------------------------------------------------------------------------
;  e820_collect: fill the Multiboot memory map at MMAP_LIN.
;---------------------------------------------------------------------------
e820_collect:
    pusha
    xor  ebx, ebx
    mov  word [mmap_next], MMAP_LIN
    mov  word [mmap_count], 0
    mov  word [e820_guard], 64         ; iteration safety limit
.loop:
    mov  eax, 0xe820
    mov  ecx, 20
    mov  edx, 0x534d4150               ; 'SMAP'
    mov  di, E820_SCRATCH
    int  0x15
    jc   .done
    cmp  eax, 0x534d4150
    jne  .done
    cmp  ecx, 20                       ; need at least 20 bytes
    jb   .skip

    ; compose a 24-byte Multiboot memory map entry
    mov  di, [mmap_next]
    mov  dword [di], 20                ; size (excluding this field)
    mov  eax, [E820_SCRATCH]           ; base low
    mov  [di + 4], eax
    mov  eax, [E820_SCRATCH + 4]       ; base high
    mov  [di + 8], eax
    mov  eax, [E820_SCRATCH + 8]       ; len low
    mov  [di + 12], eax
    mov  eax, [E820_SCRATCH + 12]      ; len high
    mov  [di + 16], eax
    mov  eax, [E820_SCRATCH + 16]      ; type
    mov  [di + 20], eax

    add  word [mmap_next], 24
    inc  word [mmap_count]
.skip:
    dec  word [e820_guard]
    jz   .done
    cmp  ebx, 0
    jne  .loop
.done:
    popa
    ret

;---------------------------------------------------------------------------
;  load_kernel: read KERNEL_SECTORS sectors from KERNEL_LBA into 0x10000.
;  Reads in 16-sector chunks with retries.  Some USB/CF BIOS stacks reset the
;  machine on large or transiently-failing reads, so we cap each request and
;  retry it before giving up.
;---------------------------------------------------------------------------
load_kernel:
    pusha
    mov  word [k_remaining], KERNEL_SECTORS
    mov  dword [k_dap + 8], KERNEL_LBA ; LBA
    mov  word [k_dap + 6], 0x1000      ; dest segment (linear 0x10000)
    mov  word [k_dap + 4], 0           ; dest offset

    ; verify int 13h extensions are present
    mov  dl, [boot_drive]
    mov  ah, 0x41
    mov  bx, 0x55aa
    int  0x13
    jc   disk_error
    cmp  bx, 0xaa55
    jne  disk_error

.chunk:
    mov  ax, [k_remaining]
    test ax, ax
    jz   .done

    mov  cx, ax
    cmp  cx, 16
    jbe  .count_ok
    mov  cx, 16
.count_ok:
    mov  [k_dap + 2], cx               ; sectors in this chunk
    mov  di, 10                        ; retry counter per chunk
.retry:
    mov  si, k_dap
    mov  dl, [boot_drive]
    mov  ah, 0x42
    int  0x13
    jnc  .ok
    call io_delay
    dec  di
    jnz  .retry
    jmp  disk_error
.ok:
    ; advance LBA and destination segment
    movzx eax, cx
    add  dword [k_dap + 8], eax
    sub  word [k_remaining], cx
    shl  cx, 5                         ; cx * 32 = sectors*512/16
    add  word [k_dap + 6], cx
    jmp  .chunk
.done:
    popa
    ret

disk_error:
    mov  si, msg_err
    call vga_print
.parked:
    cli
    hlt
    jmp  .parked

;---------------------------------------------------------------------------
;  vga_print: write a NUL/CR/LF-terminated string at DS:SI directly to the
;  VGA text buffer. Tracks a persistent cursor across calls.
;---------------------------------------------------------------------------
vga_print:
    pusha
    push es
    mov  ax, 0xb800
    mov  es, ax
    mov  di, [vga_cursor]
.next:
    lodsb
    or   al, al
    jz   .done
    cmp  al, 0x0d
    je   .next
    cmp  al, 0x0a
    je   .newline
    mov  ah, 0x07
    stosw
    jmp  .next
.newline:
    ; advance DI to the start of the next row
    mov  ax, di
    xor  dx, dx
    mov  bx, 160
    div  bx
    inc  ax
    cmp  ax, 25
    jb   .noroll
    xor  ax, ax
.noroll:
    mul  bx
    mov  di, ax
    jmp  .next
.done:
    mov  [vga_cursor], di
    pop es
    popa
    ret

;---------------------------------------------------------------------------
;  enable_a20: force-enable the A20 gate.
;    Methods tried per pass:
;      1. BIOS INT 15h, AH=2401h
;      2. Fast A20 via port 0x92
;      3. Keyboard controller (8042), with bounded waits
;    After each pass the A20 state is verified by a memory wrap test and the
;    whole sequence is retried up to 3 times.
;---------------------------------------------------------------------------
enable_a20:
    pusha

    ; Most systems boot with A20 already enabled. Test first: poking the 8042
    ; keyboard controller on machines without a PS/2 controller (or with a
    ; controller that shares its I/O with other hardware) can reset the CPU,
    ; so we only touch it when A20 really is off.
    call a20_is_on
    test al, al
    jnz  .done

    mov  cx, 3                ; retry passes
.again:
    ; Method 1: BIOS function
    mov  ax, 0x2401
    int  0x15

    ; Method 2: fast A20 (port 0x92)
    call fast_a20

    ; Method 3: keyboard controller (timeout-protected)
    call a20_keyboard

    ; verify A20 actually enabled
    call a20_is_on
    test al, al
    jnz  .done

    dec  cx
    jnz  .again

.done:
    popa
    ret

;---------------------------------------------------------------------------
;  fast_a20: toggle A20 via the system-control port 0x92.
;---------------------------------------------------------------------------
fast_a20:
    in   al, 0x92
    or   al, 0x02             ; set A20 bit
    and  al, 0xfe             ; clear bit 0 (don't reset)
    out  0x92, al
    ret

;---------------------------------------------------------------------------
;  io_delay: give the 8042/chipset time between pokes.  Reading the port 0x80
;  POST-code register is a safe way to waste a few I/O cycles.
;---------------------------------------------------------------------------
io_delay:
    push ax
    in   al, 0x80
    in   al, 0x80
    in   al, 0x80
    pop  ax
    ret

;---------------------------------------------------------------------------
;  a20_keyboard: enable A20 through the 8042 keyboard controller.
;  All waits are bounded so we cannot hang on systems without a PS/2
;  controller (common on CSM-only machines).
;---------------------------------------------------------------------------
a20_keyboard:
    ; disable keyboard
    call .wait_input
    mov  al, 0xad
    out  0x64, al
    call io_delay

    ; read output port
    call .wait_input
    mov  al, 0xd0
    out  0x64, al
    call io_delay

    call .wait_output
    in   al, 0x60
    push ax
    call io_delay

    ; write back output port with A20 bit set
    call .wait_input
    mov  al, 0xd1
    out  0x64, al
    call io_delay

    call .wait_input
    pop  ax
    or   al, 0x02
    out  0x60, al
    call io_delay

    ; re-enable keyboard
    call .wait_input
    mov  al, 0xae
    out  0x64, al
    call io_delay
    ret

.wait_input:
    push cx
    mov  cx, 0xffff
.iloop:
    in   al, 0x64
    test al, 0x02
    jz   .iok
    dec  cx
    jnz  .iloop
.iok:
    pop  cx
    ret

.wait_output:
    push cx
    mov  cx, 0xffff
.oloop:
    in   al, 0x64
    test al, 0x01
    jnz  .ook
    dec  cx
    jnz  .oloop
.ook:
    pop  cx
    ret

;---------------------------------------------------------------------------
;  a20_is_on: test the A20 gate state via a memory wrap check.
;  Returns AL=1 if enabled, AL=0 if not.
;  Uses scratch memory at 0x80000 / 0x180000 (bit 20 differs).
;---------------------------------------------------------------------------
a20_is_on:
    push es
    push ds
    push bx

    mov  bx, 0x8000          ; segment for 0x80000
    mov  es, bx
    mov  bx, 0x1800          ; segment for 0x180000
    mov  ds, bx

    mov  byte [es:0], 0x00
    mov  byte [ds:0], 0xff   ; if A20 is off this wraps to 0x80000

    mov  al, [es:0]
    test al, al
    jz   .on
    xor  al, al
    jmp  .done
.on:
    mov  al, 1
.done:
    pop  bx
    pop  ds
    pop  es
    ret

;---------------------------------------------------------------------------
;  switch_pm: load a flat GDT and enter 32-bit protected mode.
;---------------------------------------------------------------------------
switch_pm:
    cli
    lgdt [gdtr]
    mov  eax, cr0
    or   eax, 1
    mov  cr0, eax
    jmp  0x08:pm_entry

;---------------------------------------------------------------------------
;  Real mode data
;---------------------------------------------------------------------------
boot_drive:   db 0
mmap_next:    dw 0
mmap_count:   dw 0
k_remaining:  dw 0
vga_cursor:   dw 0
e820_guard:   dw 0

k_dap:
    db 0x10
    db 0
    dw 0          ; sectors
    dw 0          ; offset
    dw 0          ; segment
    dd 0          ; LBA low
    dd 0          ; LBA high

msg_stage2: db "rtjn: stage2 -> E820, load elf", 0x0d, 0x0a, 0
msg_loadk:  db "rtjn: loading kernel...", 0x0d, 0x0a, 0
msg_err:    db "rtjn: stage2 disk error", 0x0d, 0x0a, 0

;---------------------------------------------------------------------------
;  Flat GDT for protected mode (code 0x08, data 0x10)
;---------------------------------------------------------------------------
align 8, db 0
gdt:
    dq 0                                 ; null
    dw 0xffff, 0x0000
    db 0x00, 0x9a, 0xcf, 0x00
    dw 0xffff, 0x0000
    db 0x00, 0x92, 0xcf, 0x00
gdtr:
    dw gdtr - gdt - 1
    dd gdt

;===========================================================================
;  32-bit: relocate ELF LOAD segments and record the entry point
;===========================================================================
[bits 32]
relocate_elf:
    mov  esi, KERNEL_LOAD
    cmp  byte [esi], 0x7f
    jne  .bad
    cmp  byte [esi + 1], 'E'
    jne  .bad
    cmp  byte [esi + 2], 'L'
    jne  .bad
    cmp  byte [esi + 3], 'F'
    jne  .bad

    mov  eax, [esi + 0x18]             ; e_entry
    mov  [entry_addr], eax

    mov  eax, [esi + 0x1c]             ; e_phoff
    add  eax, KERNEL_LOAD              ; eax = program header table
    mov  ebp, eax                      ; keep phdr pointer in ebp
    movzx ebx, word [esi + 0x2c]       ; ebx = number of program headers
    test ebx, ebx
    jz   .bad

.phloop:
    mov  edx, [ebp]                    ; p_type
    cmp  edx, 1                        ; PT_LOAD ?
    jne  .next

    ; copy p_filesz bytes from scratch+p_offset to p_vaddr
    mov  edi, [ebp + 8]                ; p_vaddr
    mov  esi, [ebp + 4]                ; p_offset
    add  esi, KERNEL_LOAD              ; source in scratch
    mov  ecx, [ebp + 16]               ; p_filesz
    test ecx, ecx
    jz   .memsz
    rep  movsb

.memsz:
    ; zero the gap between p_filesz and p_memsz (BSS)
    mov  ecx, [ebp + 20]               ; p_memsz
    sub  ecx, [ebp + 16]               ; - p_filesz
    jbe  .next
    mov  edi, [ebp + 8]
    add  edi, [ebp + 16]               ; vaddr + filesz
    xor  eax, eax
    rep  stosb

.next:
    add  ebp, 0x20                     ; 32-byte program headers
    dec  ebx
    jnz  .phloop

    ; also mark the boot drive for the kernel? not needed (multiboot has no bootdev)
    ret
.bad:
    mov  esi, msg_bad
    call puts32
.parked:
    cli
    hlt
    jmp  .parked

;---------------------------------------------------------------------------
;  32-bit helpers / data
;---------------------------------------------------------------------------
puts32:
    ; minimal VGA text output for diagnostics (row 0 col 0x4f)
    mov  ax, 0xb800
    mov  es, ax
    mov  edi, 160
.nextch:
    lodsb
    test al, al
    jz   .done
    mov  ah, 0x07
    stosw
    jmp  .nextch
.done:
    ret

entry_addr: dd 0
msg_bad:     db "rtjn: bad ELF", 0
