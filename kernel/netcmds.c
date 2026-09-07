#include <stddef.h>
#include <stdint.h>
#include "string.h"
#include "printk.h"
#include "net.h"
#include "dns.h"
#include "icmp.h"
#include "http.h"
#include "commands.h"
#include "time.h"

extern volatile uint32_t	timer_ticks;
extern volatile uint8_t	cancel_input;

static void	print_ip(net_ip4_t ip)
{
	char	buf[16];
	ip4_to_string(ip, buf, sizeof(buf));
	printk("%s", buf);
}

int	cmd_ping(int argc, char **argv)
{
	if (argc < 2)
	{
		printk("Usage: ping <host> [-c count]\n");
		return (0);
	}

	const char	*host = 0;
	int			count = 4;

	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--count") == 0)
		{
			if (i + 1 >= argc)
			{
				printk("ping: option '%s' requires an argument\n", argv[i]);
				return (0);
			}
			count = atoi(argv[++i]);
			continue;
		}
		if (!host)
			host = argv[i];
	}
	if (!host)
	{
		printk("Usage: ping <host> [-c count]\n");
		return (0);
	}
	if (count < 1)
		count = 1;

	net_ip4_t	ip;
	if (dns_resolve(host, &ip) != 0)
	{
		printk("ping: cannot resolve '%s'\n", host);
		return (0);
	}
	char	ipstr[16];
	ip4_to_string(ip, ipstr, sizeof(ipstr));
	printk("PING %s (%s)\n", host, ipstr);

	int		sent = 0, lost = 0;
	for (int i = 0; i < count; i++)
	{
		uint32_t	rtt = 0;
		if (icmp_ping(ip, &rtt) == 0)
			printk("%d bytes from %s: icmp_seq=%d ttl=64 time=%d ticks\n",
				56, ipstr, i, rtt);
		else
		{
			if (cancel_input)
			{
				printk("^C\n");
				break;
			}
			printk("Request timeout for icmp_seq=%d\n", i);
			lost++;
		}
		sent++;
		if (i + 1 < count)
			sleep(1);
	}
	printk("--- %s ping statistics ---\n", host);
	printk("%d packets transmitted, %d received, %d%% packet loss\n",
		sent, sent - lost, sent ? (lost * 100) / sent : 0);
	return (0);
}

int	cmd_curl(int argc, char **argv)
{
	if (argc < 2)
	{
		printk("Usage: curl <url>\n");
		return (0);
	}
	char	body[4096];
	int		status = -1;
	if (http_get(argv[1], body, sizeof(body), &status) < 0)
	{
		printk("curl: request failed\n");
		return (0);
	}
	printk("HTTP/%d\n", status >= 0 ? status : 0);
	printk("%s\n", body);
	return (0);
}

int	cmd_ifconfig(int argc, char **argv)
{
	(void)argc; (void)argv;
	printk("%s: flags=%d<UP BROADCAST RUNNING MULTICAST>\n",
		net_iface.name, (net_iface.up ? 1 : 0));
	printk("        MAC %x:%x:%x:%x:%x:%x\n",
		net_iface.mac.b[0], net_iface.mac.b[1], net_iface.mac.b[2],
		net_iface.mac.b[3], net_iface.mac.b[4], net_iface.mac.b[5]);
	printk("        inet ");
	print_ip(net_iface.ip);
	printk("  netmask ");
	print_ip(net_iface.netmask);
	printk("  gateway ");
	print_ip(net_iface.gateway);
	printk("  dns ");
	print_ip(net_iface.dns);
	printk("\n");
	return (0);
}

int	cmd_netstat(int argc, char **argv)
{
	(void)argc; (void)argv;
	printk("Active Internet connections\n");
	printk("Proto  Local Address        Remote Address       State\n");
	printk("tcp    :%-5u                %-17s ESTABLISHED\n",
		(unsigned)0, "(active)");
	return (0);
}
