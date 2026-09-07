#include <stddef.h>
#include <stdint.h>
#include "string.h"
#include "printk.h"
#include "usb.h"
#include "commands.h"

int	cmd_usb(int argc, char **argv)
{
	(void)argc; (void)argv;
	int	count = usb_get_device_count();

	printk("USB devices: %d\n", count);
	for (int i = 0; i < count; i++)
	{
		usb_device_t	*d = usb_get_device((uint8_t)i);
		if (!d || !d->in_use)
			continue;
		printk("  %d: port %d  speed %s  addr %d\n",
			i, d->port,
			d->speed == USB_SPEED_LOW ? "low" :
			(d->speed == USB_SPEED_HIGH ? "high" : "full"),
			d->address);
		printk("     vid:prod %04x:%04x  class %d\n",
			d->vendor_id, d->product_id, d->device_class);
		for (int j = 0; j < 4 && j < d->num_interfaces; j++)
		{
			printk("     iface %d: class %d sub %d proto %d\n",
				j, d->interfaces[j].class,
				d->interfaces[j].subclass, d->interfaces[j].protocol);
		}
	}
	return (0);
}

int	cmd_uhci(int argc, char **argv)
{
	(void)argc; (void)argv;
	printk("UHCI: basic host controller support\n");
	printk("  devices found: %d\n", usb_get_device_count());
	return (0);
}
