;===========================================================================
;  rtjn bootloader - Stage 1 (MBR)
;  Loads stage 2 from disk into memory and jumps to it.
;
;  Memory layout:
;    Stage 1 loaded by BIOS at  0x7C00
;    Stage 2 loaded here at     0x1000
;
;  Disk layout (sectors):
;    sector 0  : stage 1 (MBR)
;    sector 1..N: stage 2  (N = STAGE2_SECTORS)
;    thereafter: kernel ELF
;===========================================================================

[org 0x7c00]
[bits 16]

STAGE2_SEG     equ 0x0000
STAGE2_OFF     equ 0x1000
STAGE2_LBA     equ 1

%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS 16
%endif

start:
    cli
    xor  ax, ax
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  sp, 0x7c00
    ; interrupts stay disabled; BIOS calls below handle enabling internally

    mov  [boot_drive], dl        ; BIOS gives us the boot drive in DL

    ; check INT 13h extensions support
    mov  dl, [boot_drive]
    mov  ah, 0x41
    mov  bx, 0x55aa
    int  0x13
    jc   disk_error
    cmp  bx, 0xaa55
    jne  disk_error

    mov  si, msg_load
    call print

    ; Load stage 2
    mov  si, dap
    mov  dl, [boot_drive]
    mov  ah, 0x42
    int  0x13
    jc   disk_error

    ; Jump to stage 2 (real mode, far)
    jmp  STAGE2_SEG:STAGE2_OFF

disk_error:
    mov  si, msg_err
    call print
halt_forever:
    cli
.hlt:
    hlt
    jmp  .hlt

print:
    lodsb
    or   al, al
    jz   .done
    mov  ah, 0x0e
    mov  bx, 0x0007
    int  0x10
    jmp  print
.done:
    ret

; Disk Address Packet for int 13h / AH=0x42
dap:
    db 0x10          ; size of DAP
    db 0             ; reserved
    dw STAGE2_SECTORS; count of sectors to read
    dw STAGE2_OFF    ; destination offset
    dw STAGE2_SEG    ; destination segment
    dd STAGE2_LBA    ; LBA (low)
    dd 0             ; LBA (high)

msg_load:  db "rtjn: stage1 -> loading stage2", 0x0d, 0x0a, 0
msg_err:   db "rtjn: stage1 disk error", 0x0d, 0x0a, 0

boot_drive: db 0

; pad the boot code up to the start of the partition table (offset 446)
times 446 - ($ - $$) db 0

;---------------------------------------------------------------------------
;  Master Boot Record partition table.
;  A CSM BIOS often refuses to treat a USB stick as bootable unless the MBR
;  contains a valid, active partition table entry. We declare one partition
;  covering the whole image (starting at LBA 1). The custom boot code above
;  ignores it, but the BIOS uses it to recognise the device as bootable.
;---------------------------------------------------------------------------
partition_table:
    ; entry 1: active, start CHS 0/0/1, type 0x0C (FAT32 LBA), whole disk
    db 0x80                      ; bootable flag
    db 0x00, 0x02, 0x00          ; CHS start (head 0, sector 2, cylinder 0)
    db 0x0C                      ; partition type
    db 0xFE, 0xFF, 0xFF          ; CHS end (head 254, sector 63, cylinder 1023)
    dd 1                         ; LBA start = 1 (right after MBR)
    dd 0x00040000                ; size = 262144 sectors (128 MiB)
    ; entries 2-4 unused
    times 3 * 16 db 0

dw 0xaa55
