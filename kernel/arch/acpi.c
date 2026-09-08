#include <stdint.h>
#include <stddef.h>
#include "io.h"
#include "printk.h"
#include "memory.h"
#include "string.h"
#include "acpi.h"

/* Generic ACPI 2.0 table signatures (the FADT is tagged "FACP"). */
#define ACPI_RSDP_SIG	"RSD PTR "
#define ACPI_FACP_SIG	"FACP"

/* Sleep enable bit in the PM1 control registers. */
#define ACPI_SLP_EN		(1 << 13)
/* Default S5 (power off) sleep value used by most firmwares / QEMU. */
#define ACPI_S5_TYP		1

/* QEMU-specific shutdown ports (used as a fallback when no usable ACPI
 * controller is found). */
#define QEMU_PIIX4_PORT	0x4004
#define QEMU_PIIX4_VAL	0x3400
#define QEMU_DEBUG_PORT	0x604
#define QEMU_DEBUG_VAL	0x2000

typedef struct __attribute__((packed)) {
	char		signature[8];
	uint8_t		checksum;
	char		oem_id[6];
	uint8_t		revision;
	uint32_t	rsdt_address;
	uint32_t	length;
	uint64_t	xsdt_address;
	uint8_t		extended_checksum;
	uint8_t		reserved[3];
} rsdp_t;

typedef struct __attribute__((packed)) {
	char		signature[4];
	uint32_t	length;
	uint8_t		revision;
	uint8_t		checksum;
	char		oem_id[6];
	char		oem_table_id[8];
	uint32_t	oem_revision;
	uint32_t	creator_id;
	uint32_t	creator_revision;
} sdt_header_t;

typedef struct __attribute__((packed)) {
	sdt_header_t	header;
	uint32_t		entry[];
} rsdt_t;

typedef struct __attribute__((packed)) {
	sdt_header_t	header;
	char			firmware_ctrl;
	char			dsdt;
	uint8_t			reserved;
	uint8_t			preferred_pm_profile;
	uint16_t		sci_int;
	uint32_t		smi_cmd;
	uint8_t			acpi_enable;
	uint8_t			acpi_disable;
	uint8_t			s4bios_req;
	uint8_t			pstate_cnt;
	uint32_t		pm1a_evt_blk;
	uint32_t		pm1b_evt_blk;
	uint32_t		pm1a_cnt_blk;
	uint32_t		pm1b_cnt_blk;
	uint32_t		pm2_cnt_blk;
	uint32_t		pm_timer_blk;
	uint32_t		gpe0_blk;
	uint32_t		gpe1_blk;
	uint8_t			pm1_evt_len;
	uint8_t			pm1_cnt_len;
	uint8_t			pm2_cnt_len;
	uint8_t			pm_timer_len;
	uint8_t			gpe0_blk_len;
	uint8_t			gpe1_blk_len;
	uint8_t			gpe1_base;
	uint8_t			cst_cnt;
	uint16_t		p_lvl2_lat;
	uint16_t		p_lvl3_lat;
	uint16_t		flush_size;
	uint16_t		flush_stride;
	uint8_t			duty_offset;
	uint8_t			duty_width;
	uint8_t			day_alrm;
	uint8_t			mon_alrm;
	uint8_t			century;
	uint16_t		iapc_boot_arch;
	uint8_t			reserved2;
	uint32_t		flags;
} fadt_t;

/* The kernel identity-maps only the first 4 MiB by default; ACPI tables
 * may live above that, so map their pages before touching them. */
static void	acpi_map(uint32_t phys, uint32_t len)
{
	uint32_t	base = phys & ~0xFFFUL;

	for (uint32_t p = base; p < base + len; p += PAGE_SIZE)
	{
		vm_map_page((void*)p, (void*)p);
		__native_flush_tlb_single(p);
	}
}

static uint8_t	acpi_checksum(const uint8_t *data, size_t len)
{
	uint8_t	sum = 0;

	for (size_t i = 0; i < len; i++)
		sum += data[i];
	return (sum);
}

static rsdp_t	*acpi_find_rsdp(void)
{
	uint32_t	ebda;
	uint32_t	addr;

	/* SeaBIOS stores the EBDA segment at 0x40E. */
	ebda = (uint32_t)(*(uint16_t*)(0x400 + 0x0E)) << 4;
	if (ebda > 0x7FF00)
		ebda = 0;
	for (addr = ebda & ~0xFUL; addr < ebda + 0x400; addr += 16)
	{
		if (memcmp((void*)addr, ACPI_RSDP_SIG, 8) == 0
			&& acpi_checksum((const uint8_t*)addr, 20) == 0)
			return ((rsdp_t*)addr);
	}
	/* Fall back to the BIOS read-only memory area. */
	for (addr = 0xE0000; addr < 0x100000; addr += 16)
	{
		if (memcmp((void*)addr, ACPI_RSDP_SIG, 8) == 0
			&& acpi_checksum((const uint8_t*)addr, 20) == 0)
			return ((rsdp_t*)addr);
	}
	return (0);
}

static fadt_t	*acpi_find_fadt(rsdp_t *rsdp)
{
	uint32_t	rsdt_phys = rsdp->rsdt_address;
	uint32_t	count;
	uint32_t	len;
	uint32_t	i;

	if (!rsdt_phys)
		return (0);
	acpi_map(rsdt_phys, sizeof(sdt_header_t) + sizeof(uint32_t));
	len = ((sdt_header_t*)rsdt_phys)->length;
	acpi_map(rsdt_phys, len);
	if (acpi_checksum((const uint8_t*)rsdt_phys, len) != 0)
		return (0);
	count = (len - sizeof(sdt_header_t)) / sizeof(uint32_t);
	for (i = 0; i < count; i++)
	{
		uint32_t	entry = ((rsdt_t*)rsdt_phys)->entry[i];

		acpi_map(entry, sizeof(sdt_header_t));
		if (memcmp(((sdt_header_t*)entry)->signature, ACPI_FACP_SIG, 4) == 0)
			return ((fadt_t*)entry);
	}
	return (0);
}

/*
** Write SLP_TYP+S LP_EN to the PM1 control register to request an S5 (power
** off). The \_S5 default of 1 is what QEMU and most firmwares use.
*/
static int	acpi_power_off(fadt_t *fadt)
{
	uint32_t	pm1a = fadt->pm1a_cnt_blk;
	uint16_t	val;

	if (!pm1a || fadt->pm1_cnt_len < 2)
		return (0);
	val = inw((uint16_t)pm1a);
	val = (uint16_t)((val & ~0x1FFF) | (ACPI_S5_TYP & 0x1F) | ACPI_SLP_EN);
	outw((uint16_t)pm1a, val);
	return (1);
}

static void	qemu_fallback(void)
{
	outw(QEMU_PIIX4_PORT, QEMU_PIIX4_VAL);
	outw(QEMU_DEBUG_PORT, QEMU_DEBUG_VAL);
}

void	acpi_shutdown(void)
{
	rsdp_t	*rsdp = acpi_find_rsdp();
	fadt_t	*fadt;

	if (rsdp && (fadt = acpi_find_fadt(rsdp)) != 0)
	{
		printk("ACPI: S5 via FADT (PM1a_CNT 0x%x)\n", fadt->pm1a_cnt_blk);
		if (acpi_power_off(fadt))
			goto done;
	}
	else
	{
		printk("ACPI: no usable tables, using QEMU ports\n");
	}
	qemu_fallback();
done:
	/* Give ACPI / firmware a moment to act, then park the CPU. */
	asm volatile ("cli");
	for (;;)
		asm volatile ("hlt");
}
