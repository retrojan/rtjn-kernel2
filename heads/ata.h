#ifndef ATA_H
# define ATA_H

# include <stdint.h>
# include <stddef.h>

/* ATA/IDE primary controller ports */
# define ATA_PRIMARY_IO		0x1F0
# define ATA_PRIMARY_CTRL	0x3F6
# define ATA_PRIMARY_IRQ	14

/* ATA/IDE secondary controller ports */
# define ATA_SECONDARY_IO	0x170
# define ATA_SECONDARY_CTRL	0x376
# define ATA_SECONDARY_IRQ	15

/* ATA registers (offset from base I/O) */
# define ATA_REG_DATA		0x00
# define ATA_REG_ERROR		0x01
# define ATA_REG_FEATURES	0x01
# define ATA_REG_SECCOUNT	0x02
# define ATA_REG_LBA_LO		0x03
# define ATA_REG_LBA_MID	0x04
# define ATA_REG_LBA_HI		0x05
# define ATA_REG_DRIVE_HEAD	0x06
# define ATA_REG_STATUS		0x07
# define ATA_REG_COMMAND	0x07

/* Status bits */
# define ATA_SR_BSY		0x80
# define ATA_SR_DRDY	0x40
# define ATA_SR_DRQ		0x08
# define ATA_SR_ERR		0x01

/* Commands */
# define ATA_CMD_READ_PIO	0x20
# define ATA_CMD_READ_DMA	0xC8
# define ATA_CMD_WRITE_PIO	0x30
# define ATA_CMD_WRITE_DMA	0xCA
# define ATA_CMD_IDENTIFY	0xEC
# define ATA_CMD_IDENTIFY_DMA	0xEE

/* Drive types */
# define ATA_DRIVE_HDD		0
# define ATA_DRIVE_CDROM	1

/* Sector size */
# define ATA_SECTOR_SIZE	512

typedef struct ata_device
{
	int		present;
	int		is_master;
	uint16_t	base;
	uint16_t	ctrl;
	uint8_t	irq;
	uint8_t	signature[2];
	uint8_t	model[41];
	uint32_t	capacity_sectors;
	uint8_t	drive_type;
} ata_device_t;

void		ata_init(void);
int			ata_read_sectors(uint8_t drive, uint32_t lba, uint32_t count, uint8_t *buf);
int			ata_write_sectors(uint8_t drive, uint32_t lba, uint32_t count, const uint8_t *buf);
ata_device_t	*ata_get_device(uint8_t drive);

#endif
