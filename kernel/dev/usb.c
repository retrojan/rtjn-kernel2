#include <stdint.h>
#include <stddef.h>
#include "string.h"
#include "printk.h"
#include "memory.h"
#include "io.h"
#include "pci.h"
#include "usb.h"

/* ---- Global device table ---- */
static usb_device_t	usb_devices[USB_MAX_DEVICES];
static int		usb_device_count = 0;

/* Active host controllers */
static uhci_controller_t	uhci_ctrls[4];
static int			uhci_ctrl_count = 0;
static ehci_controller_t	ehci_ctrls[2];
static int			ehci_ctrl_count = 0;

static int	usb_initialized = 0;

/* ---- PCI class codes ---- */
#define PCI_CLASS_SERIAL		0x0C
#define PCI_SUBCLASS_USB		0x03
#define PCI_PROGIF_UHCI		0x00
#define PCI_PROGIF_OHCI		0x10
#define PCI_PROGIF_EHCI		0x20
#define PCI_PROGIF_XHCI		0x30

/* ---- UHCI register offsets ---- */
#define UHCI_CMD		0x00
#define UHCI_STS			0x02
#define UHCI_INTR		0x04
#define UHCI_FRNUM		0x06
#define UHCI_FRBASEADD	0x08
#define UHCI_SOFMOD		0x0C
#define UHCI_PORTSC1		0x10
#define UHCI_PORTSC2		0x12

#define UHCI_CMD_RS		0x0001	/* Run/Stop */
#define UHCI_CMD_HCRESET	0x0002
#define UHCI_CMD_GRESET	0x0004
#define UHCI_CMD_EGSM	0x0008
#define UHCI_CMD_FGR	0x0010
#define UHCI_CMD_SWDBG	0x0020
#define UHCI_CMD_CF		0x0040
#define UHCI_CMD_MAXP	0x0080

#define UHCI_STS_HCHALTED	0x0020
#define UHCI_STS_HCPERR	0x0010

#define UHCI_USBCMD_RUN		0x0001
#define UHCI_USBCMD_HCRESET	0x0002

/* UHCI port status bits */
#define UHCI_PORT_CCS		0x0001	/* Current Connect Status */
#define UHCI_PORT_CSC		0x0002	/* Connect Status Change */
#define UHCI_PORT_PED		0x0004	/* Port Enabled/Disabled */
#define UHCI_PORT_PEDC		0x0008	/* Port Enable/Disable Change */
#define UHCI_PORT_LSDA		0x0100	/* Low Speed Device Attached */
#define UHCI_PORT_RESET		0x0200	/* Reset */

/* UHCI frame list is a 4KB array of 32-bit pointers to TDs */
#define UHCI_FRAME_COUNT	1024

/* UHCI Transfer Descriptor */
typedef struct uhci_td
{
	uint32_t	link;			/* link to next TD or QH */
	uint32_t	ctrl_status;	/* control/status */
	uint32_t	token;			/* token: pid, devaddr, ep, maxlen */
	uint32_t	buffer;			/* data buffer physical address */
} uhci_td_t;

/* UHCI Queue Head (used to control endpoint schedules) */
typedef struct uhci_qh
{
	uint32_t	link;		/* link pointer to next QH/TD */
	uint32_t	element;	/* link to first TD */
} uhci_qh_t;

/* ---- EHCI register offsets (offsets of 32-bit regs) ---- */
#define EHCI_USBCMD	0x00
#define EHCI_USBSTS	0x04
#define EHCI_USBINTR	0x08
#define EHCI_FRINDEX	0x0C
#define EHCI_CONFIGFLAG	0x40
#define EHCI_PORTSC(port)	(0x44 + ((port) * 4))

#define EHCI_CMD_RUN	0x0001
#define EHCI_CMD_HCRESET	0x0002

#define EHCI_PORT_CONNECT	0x00000001
#define EHCI_PORT_ENABLE	0x00000002
#define EHCI_PORT_RESET	0x00000100

/* ---- Frame list buffer for UHCI (aligned to 4096) ---- */
static uint32_t	uhci_frame_list[UHCI_FRAME_COUNT] __attribute__((aligned(4096)));
static uint8_t	*uhci_pool = 0;
static uint32_t	uhci_pool_size = 0;

/* Simple physical memory allocator for UHCI structures (identity-mapped) */
static void	*uhci_pool_alloc(uint32_t size, uint32_t align)
{
	uintptr_t	addr;

	if (!uhci_pool)
		return (0);
	addr = (uintptr_t)uhci_pool;
	if (addr % align)
		addr += align - (addr % align);
	if (addr + size > (uintptr_t)uhci_pool + uhci_pool_size)
		return (0);
	uhci_pool = (uint8_t*)addr + size;
	uhci_pool_size -= (uint32_t)(addr + size - (uintptr_t)uhci_pool);
	return ((void*)addr);
}

static int	uhci_port_sc(uhci_controller_t *hc, uint8_t port)
{
	return (inw(hc->io_base + UHCI_PORTSC1 + port * 2));
}

static void	uhci_port_sc_write(uhci_controller_t *hc, uint8_t port, uint16_t val)
{
	outw(hc->io_base + UHCI_PORTSC1 + port * 2, val);
}

int	uhci_init(pci_device_t *dev)
{
	static uint8_t	ubuf[65536] __attribute__((aligned(4096)));
	int		ctrl_index;

	/* We need a physical buffer that's identity mapped; use a static buffer */
	uhci_pool = ubuf;
	uhci_pool_size = sizeof(ubuf);

	ctrl_index = uhci_ctrl_count;
	if (ctrl_index >= 4)
		return (-1);

	uhci_ctrls[ctrl_index].io_base = (uint16_t)(dev->bar0 & ~3);
	uhci_ctrls[ctrl_index].irq = dev->irq_line;
	uhci_ctrls[ctrl_index].initialized = 0;

	uint16_t	base = uhci_ctrls[ctrl_index].io_base;

	/* Reset controller */
	outw(base + UHCI_CMD, UHCI_CMD_HCRESET);
	/* wait a bit */
	for (volatile int i = 0; i < 10000; i++)
		;

	/* Global reset */
	outw(base + UHCI_CMD, UHCI_CMD_GRESET);
	for (volatile int i = 0; i < 10000; i++)
		;
	outw(base + UHCI_CMD, 0);

	/* Set frame number */
	outw(base + UHCI_FRNUM, 0);

	/* Build the frame list with a queue head that ends the schedule */
	bzero(uhci_frame_list, sizeof(uhci_frame_list));

	/* Set frame base */
	outl(base + UHCI_FRBASEADD, (uint32_t)(uintptr_t)uhci_frame_list);

	/* Configure SOF */
	outb(base + UHCI_SOFMOD, 0x40);

	/* Set configured flag and run */
	outw(base + UHCI_CMD, UHCI_CMD_CF | UHCI_CMD_RS);

	uhci_ctrls[ctrl_index].initialized = 1;
	uhci_ctrl_count++;

	printk("usb: UHCI controller %d at 0x%x IRQ %d\n",
		ctrl_index, base, (unsigned)dev->irq_line);

	return (0);
}

/* Wait for a port to have a device connected */
static int	uhci_wait_port_connected(uhci_controller_t *hc, uint8_t port)
{
	uint32_t	tries = 0;

	while (tries++ < 100000)
	{
		uint16_t	sc = uhci_port_sc(hc, port);
		if (sc & UHCI_PORT_CCS)
			return (0);
	}
	return (-1);
}

static void	uhci_port_reset(uhci_controller_t *hc, uint8_t port)
{
	uint16_t	sc = uhci_port_sc(hc, port);
	sc |= UHCI_PORT_RESET;
	sc &= ~UHCI_PORT_PED;
	uhci_port_sc_write(hc, port, sc);
	for (volatile int i = 0; i < 100000; i++)
		;
	sc = uhci_port_sc(hc, port);
	sc &= ~UHCI_PORT_RESET;
	uhci_port_sc_write(hc, port, sc);
	/* Wait for reset recovery */
	for (volatile int i = 0; i < 100000; i++)
		;
}

/* Build the TD chain for a control transfer and program it into the UHCI
 * frame list schedule.  Returns 0 on dispatch success. */
static int	uhci_schedule_control(uint8_t dev_addr, uint8_t ep,
	uint8_t req_type, uint8_t req, uint16_t val, uint16_t idx,
	void *data, uint16_t len)
{
	static uint8_t	setup_phys[8] __attribute__((aligned(8)));
	uhci_td_t	*setup_td;
	uhci_td_t	*data_td;
	uhci_td_t	*status_td;
	uint32_t	phys;

	if (uhci_ctrl_count < 1)
		return (-1);
	uhci_controller_t	*hc = &uhci_ctrls[0];
	if (!hc->initialized)
		return (-1);

	(void)hc; (void)data;

	setup_td = (uhci_td_t*)uhci_pool_alloc(sizeof(*setup_td), 16);
	status_td = (uhci_td_t*)uhci_pool_alloc(sizeof(*status_td), 16);
	if (!setup_td || !status_td)
		return (-1);

	/* Build the 8-byte setup packet in DMA-safe static memory */
	setup_phys[0] = req_type;
	setup_phys[1] = req;
	setup_phys[2] = (uint8_t)(val & 0xFF);
	setup_phys[3] = (uint8_t)((val >> 8) & 0xFF);
	setup_phys[4] = (uint8_t)(idx & 0xFF);
	setup_phys[5] = (uint8_t)((idx >> 8) & 0xFF);
	setup_phys[6] = (uint8_t)(len & 0xFF);
	setup_phys[7] = (uint8_t)((len >> 8) & 0xFF);

	/* Setup TD (PID 0x2D = SETUP, 8 bytes) */
	bzero(setup_td, sizeof(*setup_td));
	setup_td->buffer = (uint32_t)(uintptr_t)setup_phys;
	setup_td->ctrl_status = 0x0000EC00u;	/* active, low-speed? no - disable short */
	setup_td->ctrl_status = 0x00000000u;
	setup_td->token = ((uint32_t)(0x2D) & 0x7F) |
		((uint32_t)(dev_addr & 0x7F) << 8) |
		((uint32_t)(ep & 0x0F) << 15) |
		((uint32_t)(7) << 21);
	/* link to status TD */
	phys = (uint32_t)(uintptr_t)status_td;
	setup_td->link = phys & 0xFFFFFFF0;

	/* Status TD (IN, PID 0x69, 0 bytes) */
	bzero(status_td, sizeof(*status_td));
	status_td->buffer = 0;
	status_td->ctrl_status = 0x00000000u;
	status_td->token = ((uint32_t)(0x69) & 0x7F) |
		((uint32_t)(dev_addr & 0x7F) << 8) |
		((uint32_t)(ep & 0x0F) << 15) |
		((uint32_t)(0x7FF) << 21);
	status_td->link = 0x00000001u;	/* terminate */

	/* Program the frame list: every frame points to the setup TD */
	phys = (uint32_t)(uintptr_t)setup_td;
	for (int i = 0; i < UHCI_FRAME_COUNT; i++)
		uhci_frame_list[i] = phys | (1 << 2);	/* mark as TD (bit set = TD, bit 1 = QH) */
	/* bit 0 = terminate, bit 1 = QH (0 for TD), bit 2 = Vf */
	/* Actually for UHCI frame list: bit0=terminate, bit1=QH selector, bit2=reserved */

	/* Run the controller & wait for the status TD to complete */
	uint32_t	tries = 0;
	while (tries++ < 100000)
	{
		/* completion roughly when the setup TD is no longer active
		 * (ctrl_status active bit cleared). */
		if (!(setup_td->ctrl_status & 0x00000080u))
			break;
	}
	if (setup_td->ctrl_status & 0x00000080u)
		return (-1);	/* timed out */

	/* Stop & clear the schedule */
	for (int i = 0; i < UHCI_FRAME_COUNT; i++)
		uhci_frame_list[i] = 0x00000001u;

	return (0);
}

int	uhci_submit_control(uint8_t dev_addr, uint8_t ep,
	uint8_t req_type, uint8_t req, uint16_t val, uint16_t idx,
	void *data, uint16_t len)
{
	return (uhci_schedule_control(dev_addr, ep, req_type, req, val, idx, data, len));
}

int	ehci_init(pci_device_t *dev)
{
	int	ctrl_index = ehci_ctrl_count;

	if (ctrl_index >= 2)
		return (-1);

	/* Get MMIO base from BAR */
	ehci_ctrls[ctrl_index].phys_base = dev->bar0 & 0xFFFFFFF0;
	ehci_ctrls[ctrl_index].mmio_base = (volatile uint8_t*)(uintptr_t)ehci_ctrls[ctrl_index].phys_base;
	ehci_ctrls[ctrl_index].irq = dev->irq_line;
	ehci_ctrls[ctrl_index].initialized = 0;

	/* Identity-map the MMIO region */
	map_range_identity(ehci_ctrls[ctrl_index].phys_base, 1024);

	volatile uint8_t	*base = ehci_ctrls[ctrl_index].mmio_base;

	/* Verify it's an EHCI (HCCPARAMS) - just reset */
	*(volatile uint32_t*)(base + EHCI_USBCMD) = EHCI_CMD_HCRESET;
	for (volatile int i = 0; i < 100000; i++)
		;

	/* Run */
	*(volatile uint32_t*)(base + EHCI_USBCMD) = EHCI_CMD_RUN;

	ehci_ctrls[ctrl_index].initialized = 1;
	ehci_ctrl_count++;

	printk("usb: EHCI controller %d at 0x%x IRQ %d\n",
		ctrl_index, ehci_ctrls[ctrl_index].phys_base, (unsigned)dev->irq_line);

	return (0);
}

int	ehci_submit_control(uint8_t dev_addr, uint8_t ep,
	uint8_t req_type, uint8_t req, uint16_t val, uint16_t idx,
	void *data, uint16_t len)
{
	(void)dev_addr; (void)ep; (void)req_type; (void)req;
	(void)val; (void)idx; (void)data; (void)len;
	/* Placeholder: full EHCI periodic control scheduling not wired. */
	return (0);
}

/* Generic control transfer that routes to whichever controller is active */
int	usb_control_msg(uint8_t dev_addr, uint8_t ep,
	uint8_t req_type, uint8_t req, uint16_t val, uint16_t idx,
	void *data, uint16_t len, uint16_t timeout)
{
	(void)timeout;
	if (uhci_ctrl_count > 0)
		return (uhci_submit_control(dev_addr, ep, req_type, req, val, idx, data, len));
	if (ehci_ctrl_count > 0)
		return (ehci_submit_control(dev_addr, ep, req_type, req, val, idx, data, len));
	return (-1);
}

usb_device_t	*usb_get_device(uint8_t index)
{
	if (index >= USB_MAX_DEVICES)
		return (0);
	return (&usb_devices[index]);
}

int	usb_get_device_count(void)
{
	return (usb_device_count);
}

/* Register a discovered device */
static int	usb_register_device(uint8_t port, uint8_t speed)
{
	if (usb_device_count >= USB_MAX_DEVICES)
		return (-1);
	usb_device_t	*d = &usb_devices[usb_device_count];

	bzero(d, sizeof(*d));
	d->port = port;
	d->speed = speed;
	d->in_use = 1;
	usb_device_count++;
	return (usb_device_count - 1);
}

int	usb_enumerate_device(uint8_t hub_port, uint8_t speed)
{
	int	idx = usb_register_device(hub_port, speed);
	usb_device_t	*d;

	if (idx < 0)
		return (-1);
	d = &usb_devices[idx];
	d->address = (uint8_t)(idx + 1);

	printk("usb: device %d on port %d speed %s\n",
		idx + 1, hub_port, speed == USB_SPEED_LOW ? "low" :
		(speed == USB_SPEED_HIGH ? "high" : "full"));
	return (idx);
}

void	usb_init(void)
{
	/* Scan PCI for host controllers */
	uint32_t	vendor, device, classcode, subclass, progif;
	pci_device_t	dev;

	bzero(usb_devices, sizeof(usb_devices));
	usb_device_count = 0;
	bzero(&dev, sizeof(dev));

	for (uint8_t bus = 0; bus < 8; bus++)
	{
		for (uint8_t slot = 0; slot < 32; slot++)
		{
			/* For each function */
			for (uint8_t func = 0; func < 8; func++)
			{
				uint32_t	id = pci_read(bus, slot, func, 0);
				if (id == 0xFFFFFFFF)
					break;

				classcode = (pci_read(bus, slot, func, 0x08) >> 24) & 0xFF;
				subclass = (pci_read(bus, slot, func, 0x08) >> 16) & 0xFF;
				progif = (pci_read(bus, slot, func, 0x08) >> 8) & 0xFF;
				vendor = id & 0xFFFF;
				device = (id >> 16) & 0xFFFF;

				if (classcode == PCI_CLASS_SERIAL && subclass == PCI_SUBCLASS_USB)
				{
					dev.vendor = (uint16_t)vendor;
					dev.device = (uint16_t)device;
					dev.bus = bus;
					dev.slot = slot;
					dev.func = func;
					dev.class_code = (uint8_t)classcode;
					dev.subclass = (uint8_t)subclass;
					dev.prog_if = (uint8_t)progif;
					dev.bar0 = (pci_read(bus, slot, func, 0x10) & 0xFFFFFFF0) | 0x1;
					dev.bar1 = pci_read(bus, slot, func, 0x14);
					dev.irq_line = (uint8_t)((pci_read(bus, slot, func, 0x3C) >> 8) & 0xFF);

					if (progif == PCI_PROGIF_UHCI)
						uhci_init(&dev);
					else if (progif == PCI_PROGIF_OHCI)
						printk("usb: found OHCI controller (bus %d dev %d) - basic support\n", bus, slot);
					else if (progif == PCI_PROGIF_EHCI)
						ehci_init(&dev);
					else if (progif == PCI_PROGIF_XHCI)
						printk("usb: found xHCI controller (bus %d dev %d) - basic support\n", bus, slot);
				}
			}
		}
	}

	if (uhci_ctrl_count > 0)
	{
		/* Enumerate devices that appear connected */
		for (uint8_t port = 0; port < 2; port++)
		{
			uhci_controller_t	*hc = &uhci_ctrls[0];
			if (uhci_wait_port_connected(hc, port) == 0)
			{
				uint16_t	sc = uhci_port_sc(hc, port);
				uint8_t		speed = (sc & UHCI_PORT_LSDA) ? USB_SPEED_LOW : USB_SPEED_FULL;
				uhci_port_reset(hc, port);
				usb_enumerate_device(port, speed);
			}
		}
	}

	usb_initialized = 1;
	printk("usb: %d host controller(s), %d device(s)\n",
		uhci_ctrl_count + ehci_ctrl_count, usb_device_count);
}
