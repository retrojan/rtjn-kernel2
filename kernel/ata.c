#include <stdint.h>
#include <stddef.h>
#include "io.h"
#include "ata.h"
#include "string.h"
#include "printk.h"

static ata_device_t	ata_devices[4];
static int		ata_initialized = 0;

static void	ata_insw(uint16_t port, uint8_t *buf, uint32_t count)
{
	uint16_t	*w = (uint16_t*)buf;

	for (uint32_t i = 0; i < count; i++)
		w[i] = inw(port);
}

static void	ata_outsw(uint16_t port, const uint8_t *buf, uint32_t count)
{
	const uint16_t	*w = (const uint16_t*)buf;

	for (uint32_t i = 0; i < count; i++)
		outw(port, w[i]);
}

static int	ata_wait(uint16_t base, uint8_t mask, uint8_t expected, uint32_t timeout)
{
	uint32_t	tries = 0;

	while (tries++ < timeout)
	{
		uint8_t	status = inb(base + ATA_REG_STATUS);
		if (status & ATA_SR_ERR)
			return (-1);
		if ((status & mask) == expected)
			return (0);
	}
	return (-1);
}

static int	ata_detect_drive(uint16_t base, uint8_t drive)
{
	uint8_t	buf[512];

	/* Select drive */
	outb(base + ATA_REG_DRIVE_HEAD, (uint8_t)(0xA0 | (drive << 4)));
	ata_wait(base, ATA_SR_DRDY, ATA_SR_DRDY, 1000);

	/* Send identify command */
	outb(base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

	/* Check availability */
	uint8_t	status = inb(base + ATA_REG_STATUS);
	if (status == 0)
		return (0);
	if (status & ATA_SR_ERR)
		return (0);

	/* Wait for the 512 bytes of identify data (DRQ) */
	if (ata_wait(base, ATA_SR_DRQ, ATA_SR_DRQ, 100000) != 0)
		return (0);

	/* Check if it's ATAPI */
	uint8_t	lba_mid = inb(base + ATA_REG_LBA_MID);
	uint8_t	lba_hi = inb(base + ATA_REG_LBA_HI);
	if (lba_mid != 0 && lba_hi != 0)
		return (0);

	/* Read 256 words of identify data */
	ata_insw(base + ATA_REG_DATA, buf, 256);

	/* Sector count is at words 60-61 (LBA28) */
	uint32_t	lba28_sectors = (uint32_t)((buf[120] | (buf[121] << 8)) |
		((uint32_t)buf[122] << 16) | ((uint32_t)buf[123] << 24));

	return (lba28_sectors > 0 ? (int)lba28_sectors : 0);
}

static void	ata_get_model(uint16_t base, uint8_t drive, uint8_t *model, size_t max_len)
{
	uint8_t	buf[512];
	size_t	i;

	for (i = 0; i < max_len; i++)
		model[i] = ' ';
	model[max_len - 1] = 0;

	if (max_len < 40)
		return;

	outb(base + ATA_REG_DRIVE_HEAD, (uint8_t)(0xA0 | (drive << 4)));
	ata_wait(base, ATA_SR_DRDY, ATA_SR_DRDY, 1000);
	outb(base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
	uint8_t	status = inb(base + ATA_REG_STATUS);
	if (status == 0)
		return;
	if (ata_wait(base, ATA_SR_DRQ, ATA_SR_DRQ, 100000) != 0)
		return;

	ata_insw(base + ATA_REG_DATA, buf, 256);

	/* Model string is at words 27-46 (bytes 54-93) */
	for (i = 0; i < 40; i++)
		model[i] = (char)buf[54 + i];
	/* Model string is space-padded, strip trailing spaces */
	for (i = 39; i > 0 && model[i] == ' '; i--)
		model[i] = 0;
	/* And it's byte-swapped */
	for (i = 0; i < 39; i += 2)
	{
		char	tmp = model[i];
		model[i] = model[i + 1];
		model[i + 1] = tmp;
	}
	model[40] = 0;
}

void	ata_init(void)
{
	int	i;

	bzero(ata_devices, sizeof(ata_devices));

	for (i = 0; i < 4; i++)
	{
		uint16_t	base = (i < 2) ? ATA_PRIMARY_IO : ATA_SECONDARY_IO;
		uint8_t		drive = (uint8_t)(i % 2);
		uint8_t		irq = (i < 2) ? ATA_PRIMARY_IRQ : ATA_SECONDARY_IRQ;

		ata_devices[i].base = base;
		ata_devices[i].ctrl = (i < 2) ? ATA_PRIMARY_CTRL : ATA_SECONDARY_CTRL;
		ata_devices[i].irq = irq;
		ata_devices[i].is_master = (drive == 0);
		ata_devices[i].signature[0] = drive;
		ata_devices[i].drive_type = ATA_DRIVE_HDD;
		ata_devices[i].present = 0;

		int	cap = ata_detect_drive(base, drive);
		if (cap > 0)
		{
			ata_devices[i].present = 1;
			ata_devices[i].capacity_sectors = (uint32_t)cap;
			ata_get_model(base, drive, ata_devices[i].model, 41);
			printk("ata: drive %d found: %s (%u MB)\n",
				i, ata_devices[i].model,
				(uint32_t)(cap / 2048));
		}
	}
	ata_initialized = 1;
}

static int	ata_get_index(uint8_t drive)
{
	if (drive >= 4)
		return (-1);
	return ((int)drive);
}

static int	ata_read_sector_lba28(uint16_t base, uint8_t drv, uint32_t lba, uint8_t *buf)
{
	/* Wait for drive ready first */
	uint32_t	tries = 0;
	while ((inb(base + ATA_REG_STATUS) & ATA_SR_BSY) && tries++ < 10000)
		;

	/* Select drive */
	outb(base + ATA_REG_DRIVE_HEAD, (uint8_t)(0xE0 | (drv << 4) | ((lba >> 24) & 0x0F)));

	/* Send LBA and count */
	outb(base + ATA_REG_SECCOUNT, 1);
	outb(base + ATA_REG_LBA_LO, (uint8_t)(lba & 0xFF));
	outb(base + ATA_REG_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
	outb(base + ATA_REG_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));

	/* Issue read command */
	outb(base + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

	/* Wait for DRQ */
	if (ata_wait(base, ATA_SR_DRQ, ATA_SR_DRQ, 100000) != 0)
		return (-1);

	/* Read 256 words */
	ata_insw(base + ATA_REG_DATA, buf, 256);
	return (0);
}

static int	ata_write_sector_lba28(uint16_t base, uint8_t drv, uint32_t lba, const uint8_t *buf)
{
	uint32_t	tries = 0;
	while ((inb(base + ATA_REG_STATUS) & ATA_SR_BSY) && tries++ < 10000)
		;

	/* Select drive */
	outb(base + ATA_REG_DRIVE_HEAD, (uint8_t)(0xE0 | (drv << 4) | ((lba >> 24) & 0x0F)));

	/* Send LBA and count */
	outb(base + ATA_REG_SECCOUNT, 1);
	outb(base + ATA_REG_LBA_LO, (uint8_t)(lba & 0xFF));
	outb(base + ATA_REG_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
	outb(base + ATA_REG_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));

	/* Issue write command */
	outb(base + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

	if (ata_wait(base, ATA_SR_DRQ, ATA_SR_DRQ, 100000) != 0)
		return (-1);

	/* Write 256 words */
	ata_outsw(base + ATA_REG_DATA, buf, 256);

	/* Flush cache */
	tries = 0;
	while (!(inb(base + ATA_REG_STATUS) & (ATA_SR_ERR)) && tries++ < 10)
		;
	outb(base + ATA_REG_COMMAND, 0xE7);	/* Flush cache */

	while ((inb(base + ATA_REG_STATUS) & ATA_SR_BSY) && tries++ < 10000)
		;
	return (0);
}

int	ata_read_sectors(uint8_t drive, uint32_t lba, uint32_t count, uint8_t *buf)
{
	int		idx = ata_get_index(drive);
	ata_device_t	*dev;

	if (idx < 0)
		return (-1);
	dev = &ata_devices[idx];
	if (!dev->present)
		return (-1);
	if (lba + count > dev->capacity_sectors)
		return (-1);

	for (uint32_t i = 0; i < count; i++)
	{
		if (ata_read_sector_lba28(dev->base, dev->is_master ? 0 : 1,
			lba + i, buf + i * ATA_SECTOR_SIZE) != 0)
			return (-1);
	}
	return (0);
}

int	ata_write_sectors(uint8_t drive, uint32_t lba, uint32_t count, const uint8_t *buf)
{
	int		idx = ata_get_index(drive);
	ata_device_t	*dev;

	if (idx < 0)
		return (-1);
	dev = &ata_devices[idx];
	if (!dev->present)
		return (-1);
	if (lba + count > dev->capacity_sectors)
		return (-1);

	for (uint32_t i = 0; i < count; i++)
	{
		if (ata_write_sector_lba28(dev->base, dev->is_master ? 0 : 1,
			lba + i, buf + i * ATA_SECTOR_SIZE) != 0)
			return (-1);
	}
	return (0);
}

ata_device_t	*ata_get_device(uint8_t drive)
{
	int	idx = ata_get_index(drive);

	if (idx < 0)
		return (0);
	return (&ata_devices[idx]);
}
