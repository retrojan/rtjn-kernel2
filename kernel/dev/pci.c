#include <stdint.h>
#include "io.h"
#include "pci.h"
#include "printk.h"

uint32_t	pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
	uint32_t	addr = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) |
		(offset & 0xFC) | 0x80000000);

	outl(PCI_CONFIG_ADDR, addr);
	return (inl(PCI_CONFIG_DATA));
}

void	pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value)
{
	uint32_t	addr = (uint32_t)((bus << 16) | (slot << 11) | (func << 8) |
		(offset & 0xFC) | 0x80000000);

	outl(PCI_CONFIG_ADDR, addr);
	outl(PCI_CONFIG_DATA, value);
}

static uint8_t	pci_read8_helper(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
	uint32_t	v = pci_read(bus, slot, func, offset & 0xFC);
	return ((uint8_t)((v >> ((offset & 3) * 8)) & 0xFF));
}

int	pci_find_device(uint16_t vendor, uint16_t device, pci_device_t *out)
{
	for (uint8_t bus = 0; bus < 1; bus++)
	{
		for (uint8_t slot = 0; slot < 32; slot++)
		{
			uint32_t id = pci_read(bus, slot, 0, 0);
			if (id == 0xFFFFFFFF)
				continue;
			uint16_t v = (uint16_t)(id & 0xFFFF);
			uint16_t d = (uint16_t)((id >> 16) & 0xFFFF);
			if (v == vendor && d == device)
			{
				if (out)
				{
					out->vendor = v;
					out->device = d;
					out->bus = bus;
					out->slot = slot;
					out->func = 0;
					out->class_code = pci_read8_helper(bus, slot, 0, 0x0B);
					out->subclass  = pci_read8_helper(bus, slot, 0, 0x0A);
					out->prog_if   = pci_read8_helper(bus, slot, 0, 0x09);
					out->bar0 = (pci_read(bus, slot, 0, 0x10) & 0xFFFFFFF0) | 0x1;
					out->bar1 = pci_read(bus, slot, 0, 0x14);
					out->irq_line = pci_read8_helper(bus, slot, 0, 0x3C);
				}
				return (bus * 32 + slot);
			}
		}
	}
	return (-1);
}
