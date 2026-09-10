NAME=rtjn-kernel.img
IMGFILE=rtjn-kernel.img

# Separate disk image holding the ext2 filesystem (attached as ide0-slave)
FSIMG=fs.img
FSIMG_SIZE_MB=8

SRC_DIR=./
BUILD_DIR=build/
INCLUDE_DIR=include

# Custom bootloader (2-stage)
STAGE1=$(BUILD_DIR)stage1.bin
STAGE2=$(BUILD_DIR)stage2.bin
BOOT_DISK_LBA=17                # kernel starts right after stage1 + stage2 (sectors)
STAGE2_SECTORS=16

BOOTL_DIR=boot/
BOOTL_FILES=boot.s gdt_flush.s idt_exceptions.s load_idt.s irqs.s paging.s
BOOTL_SRCS=$(addprefix $(SRC_DIR), $(addprefix $(BOOTL_DIR), $(BOOTL_FILES)))

KERNEL_DIR=kernel/
KERNEL_FILES=kernel.c \
	arch/gdt.c arch/idt.c arch/tss.c arch/exception_handler.c arch/irq_handler.c arch/install_irq.c arch/system_timer.c arch/acpi.c \
	mm/pmm.c mm/vmm.c mm/heap.c mm/get_mem_max_addr.c \
	dev/keyboard.c dev/ata.c dev/pci.c dev/usb.c dev/rtl8139.c dev/fbcon.c dev/font.c \
	net/network.c \
	fs/vfs.c fs/ext2.c \
	task/sched.c task/syscall.c task/usertask.c \
	shell/shell.c shell/commands.c shell/editors.c shell/readline.c \
	lib/string.c lib/term.c lib/demos.c lib/print_stack.c
KERNEL_SRCS=$(addprefix $(SRC_DIR), $(addprefix $(KERNEL_DIR), $(KERNEL_FILES)))

USER_DIR=user/
USER_ELF=$(BUILD_DIR)user.elf
USER_BIN=$(BUILD_DIR)user.bin
USER_BIN_OBJ=$(BUILD_DIR)userbin.o

SRCS=$(KERNEL_FILES) $(BOOTL_FILES)
OBJS=$(addprefix $(BUILD_DIR), $(KERNEL_FILES:.c=.o) $(BOOTL_FILES:.s=.o)) $(USER_BIN_OBJ)

# The kernel image itself is an ELF executable (multiboot-compatible).
KELF=$(BUILD_DIR)rtjn-kernel.elf


.PHONY: all run clean fclean re fs

all: $(IMGFILE)

fs: $(FSIMG)

$(KELF): $(OBJS)
	i686-elf-gcc -T linker.ld -o $@ -ffreestanding -fno-builtin -fno-exceptions -fno-stack-protector -nodefaultlibs -nostdlib $(OBJS) -lgcc

# ---------------- ring-3 user program ----------------

$(USER_ELF): $(addprefix $(SRC_DIR), $(addprefix $(USER_DIR), user.c)) $(INCLUDE_DIR)/syscall.h
	mkdir -p $(BUILD_DIR)
	i686-elf-gcc -T $(SRC_DIR)$(USER_DIR)linker.ld -o $@ -ffreestanding -fno-builtin -fno-exceptions -fno-stack-protector -nostdlib -nodefaultlibs -Wall -Wextra -I $(INCLUDE_DIR) $<

$(USER_BIN): $(USER_ELF)
	i686-elf-objcopy -O binary $< $@

# Embed user.bin as a kernel object; names come out as _binary_user_bin_{start,end,size}
$(USER_BIN_OBJ): $(USER_BIN)
	cd $(BUILD_DIR) && i686-elf-objcopy -I binary -O elf32-i386 -B i386 user.bin userbin.o

# ---------------- custom bootloader ----------------

$(STAGE1): $(BOOTL_DIR)stage1.asm
	mkdir -p $(BUILD_DIR)
	nasm -f bin $< -o $@ -DSTAGE2_SECTORS=$(STAGE2_SECTORS)

$(STAGE2): $(BOOTL_DIR)stage2.asm $(KELF)
	@sz=$$(stat -c %s $(KELF)); sectors=$$(( (sz + 511) / 512 )); \
	nasm -f bin $< -o $@ -DKERNEL_LBA=$(BOOT_DISK_LBA) -DKERNEL_SECTORS=$$sectors

$(IMGFILE): $(STAGE1) $(STAGE2) $(KELF)
	@cp $(STAGE1) $@
	@dd if=$(STAGE2) of=$@ bs=512 seek=1 conv=notrunc status=none
	@dd if=$(KELF) of=$@ bs=512 seek=$(BOOT_DISK_LBA) conv=notrunc status=none
	@echo "$@ built with kernel at LBA $(BOOT_DISK_LBA)"

$(FSIMG):
	dd if=/dev/zero of=$@ bs=1M count=$(FSIMG_SIZE_MB) status=none
	mkfs.ext2 -F -b 1024 -m 0 $@
	@echo "ext2 filesystem ready: $@"

# ---------------- generic build rules ----------------

$(BUILD_DIR)%.o: $(SRC_DIR)/$(BOOTL_DIR)%.s
	mkdir -p $(BUILD_DIR)
	nasm -felf32 $< -o $@

$(BUILD_DIR)%.o: $(SRC_DIR)/$(KERNEL_DIR)%.c
	mkdir -p $(dir $@)
	i686-elf-gcc -c $< -o $@ -std=gnu99 -ffreestanding -fno-builtin -fno-exceptions -fno-stack-protector -nostdlib -nodefaultlibs -Wall -Wextra -I $(INCLUDE_DIR) -MMD -MP

-include $(BUILD_DIR)$(KERNEL_FILES:.c=.d)

run: all $(FSIMG)
	qemu-system-i386 -drive file=$(IMGFILE),format=raw,if=ide,index=0 -drive file=$(FSIMG),format=raw,if=ide,index=2 -boot c -m 128M -no-reboot -net nic,model=rtl8139 -net user -display curses 

clean:
	rm -rf $(BUILD_DIR)
	rm -rf $(IMGFILE)

fclean: clean
	rm -rf $(FSIMG)

re: fclean all
