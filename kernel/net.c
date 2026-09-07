#include <stdint.h>
#include <stddef.h>
#include "string.h"
#include "printk.h"
#include "memory.h"
#include "net.h"
#include "pci.h"
#include "rtl8139.h"
#include "ethernet.h"
#include "arp.h"

net_iface_t	net_iface;

extern int	net_handle_frame(uint8_t *frame, size_t len);

uint32_t	ip4_to_u32(net_ip4_t ip)
{
	return (((uint32_t)ip.b[0] << 24) | ((uint32_t)ip.b[1] << 16) |
		((uint32_t)ip.b[2] << 8) | (uint32_t)ip.b[3]);
}

net_ip4_t	u32_to_ip4(uint32_t v)
{
	net_ip4_t	ip;

	ip.b[0] = (uint8_t)((v >> 24) & 0xFF);
	ip.b[1] = (uint8_t)((v >> 16) & 0xFF);
	ip.b[2] = (uint8_t)((v >> 8) & 0xFF);
	ip.b[3] = (uint8_t)(v & 0xFF);
	return (ip);
}

int	ip4_to_string(net_ip4_t ip, char *out, size_t n)
{
	char	tmp[4];
	size_t	used = 0;

	for (int i = 0; i < 4; i++)
	{
		if (i > 0 && used + 1 < n)
			out[used++] = '.';
		itoa_buf(ip.b[i], tmp);
		size_t	tl = strlen(tmp);
		if (used + tl >= n)
			return (-1);
		memcpy(out + used, tmp, tl);
		used += tl;
	}
	if (used < n)
		out[used] = 0;
	return (0);
}

int	string_to_ip4(const char *s, net_ip4_t *out)
{
	int	octets[4] = {0, 0, 0, 0};
	int	idx = 0;
	int	cur = 0;

	if (!s)
		return (-1);
	while (*s)
	{
		if (*s >= '0' && *s <= '9')
		{
			cur = cur * 10 + (*s - '0');
			if (cur > 255)
				return (-1);
		}
		else if (*s == '.')
		{
			if (idx >= 3)
				return (-1);
			octets[idx++] = cur;
			cur = 0;
		}
		else
			return (-1);
		s++;
	}
	if (idx != 3)
		return (-1);
	octets[3] = cur;
	if (out)
	{
		for (int i = 0; i < 4; i++)
			out->b[i] = (uint8_t)octets[i];
	}
	return (0);
}

void	net_init(void)
{
	strcpy(net_iface.name, "eth0");
	bzero(&net_iface.mac, sizeof(net_iface.mac));
	string_to_ip4("10.0.2.15", &net_iface.ip);
	string_to_ip4("255.255.255.0", &net_iface.netmask);
	string_to_ip4("10.0.2.2", &net_iface.gateway);
	string_to_ip4("10.0.2.3", &net_iface.dns);
	net_iface.up = 0;
}

void	net_link_up(net_mac_t mac)
{
	net_iface.mac = mac;
	net_iface.up = 1;
	printk("net: link up on %s, MAC %x:%x:%x:%x:%x:%x\n",
		net_iface.name,
		mac.b[0], mac.b[1], mac.b[2], mac.b[3], mac.b[4], mac.b[5]);
}

void	net_receive(uint8_t *frame, size_t len)
{
	net_handle_frame(frame, len);
}

int	net_send_frame(uint8_t *frame, size_t len)
{
	extern int	rtl8139_tx(uint8_t *data, size_t len);

	if (!net_iface.up)
		return (-1);
	return (rtl8139_tx(frame, len));
}

void	net_driver_init(void)
{
	pci_device_t	dev;

	eth_init();
	arp_cache_boot();

	if (pci_find_device(PCI_VENDOR_REALTEK, PCI_DEVICE_RTL8139, &dev) >= 0)
	{
		printk("net: found RTL8139 (%x:%x) at bus %d slot %d IRQ %d\n",
			dev.vendor, dev.device, dev.bus, dev.slot, dev.irq_line);
		rtl8139_init(&dev);
		net_link_up(net_iface.mac);
	}
	else
	{
		printk("net: no supported NIC found\n");
	}
}
