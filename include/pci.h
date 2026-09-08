#ifndef PCI_H
# define PCI_H

# include <stdint.h>

/* PCI configuration registers (I/O space) */
# define PCI_CONFIG_ADDR	0xCF8
# define PCI_CONFIG_DATA	0xCFC

/* Common vendor/device IDs */
# define PCI_VENDOR_REALTEK	0x10EC
# define PCI_DEVICE_RTL8139	0x8139

typedef struct pci_device
{
	uint16_t	vendor;
	uint16_t	device;
	uint8_t		bus;
	uint8_t		slot;
	uint8_t		func;
	uint8_t		class_code;
	uint8_t		subclass;
	uint8_t		prog_if;
	uint32_t	bar0;
	uint32_t	bar1;
	uint8_t		irq_line;
} pci_device_t;

uint32_t	pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void		pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
int			pci_find_device(uint16_t vendor, uint16_t device, pci_device_t *out);

#endif
