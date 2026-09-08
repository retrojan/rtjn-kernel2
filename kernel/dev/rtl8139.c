#include <stdint.h>
#include <stddef.h>
#include "io.h"
#include "irq.h"
#include "memory.h"
#include "pci.h"
#include "net.h"
#include "rtl8139.h"
#include "string.h"
#include "printk.h"

/* RX ring stored in a static (identity-mapped, DMA-safe) region >= 4KB aligned */
static uint8_t	rx_buffer[RTL_RXBUF_SIZE] __attribute__((aligned(4096)));

/* one TX buffer for outbound frames */
static uint8_t	tx_buffer[NET_BUF_SIZE] __attribute__((aligned(16)));

static uint16_t	rtl_io_base = 0;
static volatile uint8_t	*rtl_mem_base = 0;
static uint32_t	rx_cur = 0;
static int		rtl_up = 0;
static int		tx_cur = 0;	/* current TX descriptor slot (0..3) */

void	rtl_write8(uint32_t reg, uint8_t val)
{
	if (rtl_mem_base)
		rtl_mem_base[reg] = val;
	else
		outb((uint16_t)(rtl_io_base + reg), val);
}

uint8_t	rtl_read8(uint32_t reg)
{
	if (rtl_mem_base)
		return (rtl_mem_base[reg]);
	return (inb((uint16_t)(rtl_io_base + reg)));
}

void	rtl_write16(uint32_t reg, uint16_t val)
{
	if (rtl_mem_base)
		*(volatile uint16_t*)(rtl_mem_base + reg) = val;
	else
		outw((uint16_t)(rtl_io_base + reg), val);
}

uint16_t	rtl_read16(uint32_t reg)
{
	if (rtl_mem_base)
		return (*(volatile uint16_t*)(rtl_mem_base + reg));
	return (inw((uint16_t)(rtl_io_base + reg)));
}

void	rtl_write32(uint32_t reg, uint32_t val)
{
	if (rtl_mem_base)
		*(volatile uint32_t*)(rtl_mem_base + reg) = val;
	else
		outl((uint16_t)(rtl_io_base + reg), val);
}

uint32_t	rtl_read32(uint32_t reg)
{
	if (rtl_mem_base)
		return (*(volatile uint32_t*)(rtl_mem_base + reg));
	return (inl((uint16_t)(rtl_io_base + reg)));
}

static void	rtl8139_reset(void)
{
	rtl_write8(RTL_CMD, RTL_CMD_RESET);
	while (rtl_read8(RTL_CMD) & RTL_CMD_RESET)
		;
}

/* ---- receive: parse the 8139 ring and hand frames to the net stack */
static void	rtl8139_rx(void)
{
	while ((rtl_read8(RTL_CMD) & RTL_CMD_RX_BUF_EMPTY) == 0)
	{
		uint32_t	status = *(uint32_t*)(rx_buffer + rx_cur);
		uint32_t	rx_len = (status >> 16) & 0x3FFF;
		uint32_t	offset = (rx_cur + 4) % RTL_RXBUF_SIZE;

		/* a zeroed / invalid slot means the tail of the available data */
		if (rx_len == 0 || rx_len > NET_BUF_SIZE - 4)
			break;

		rx_cur = (uint32_t)((rx_cur + rx_len + 4 + 3) & ~3);
		if (rx_cur >= RTL_RXBUF_SIZE)
			rx_cur -= RTL_RXBUF_SIZE;

		uint32_t	nextcap = rx_cur >= 16 ? rx_cur - 16 :
			(RTL_RXBUF_SIZE + rx_cur - 16);
		rtl_write16(RTL_CAPR, (uint16_t)nextcap);

		/* bit0 (ROK) set means the frame is valid; bit15 (ERR) means bad */
		if ((status & 0x8000) != 0)
			continue;

		uint8_t	frame[NET_BUF_SIZE];
		if (offset + rx_len <= RTL_RXBUF_SIZE)
		{
			memcpy(frame, rx_buffer + offset, rx_len);
		}
		else
		{
			uint32_t	first = RTL_RXBUF_SIZE - offset;
			memcpy(frame, rx_buffer + offset, first);
			memcpy(frame + first, rx_buffer, rx_len - first);
		}
		net_receive(frame, rx_len);
	}
	rtl_write8(RTL_CMD, RTL_CMD_RX_EN | RTL_CMD_TX_EN);
}

void	rtl8139_irq_handler(regs_t *re)
{
	(void)re;
	uint16_t	status = rtl_read16(RTL_ISR);

	if (status & RTL_ISR_RX_OK)
		rtl8139_rx();
	if (status & RTL_ISR_RX_OVERFLOW)
		rtl8139_rx();
	/* acknowledge */
	rtl_write16(RTL_ISR, status);
}

void	rtl8139_poll(void)
{
	if (rtl_up && (rtl_read16(RTL_ISR) & (RTL_ISR_RX_OK | RTL_ISR_RX_OVERFLOW)))
		rtl8139_irq_handler(0);
}

int	rtl8139_init(pci_device_t *dev)
{
	/* Prefer the I/O BAR (BAR0, bit0 set) */
	rtl_mem_base = 0;
	rtl_io_base = (uint16_t)(dev->bar0 & ~1);

	/* Config1: enable I/O & memory decoding, and leave as is */
	pci_write(dev->bus, dev->slot, dev->func, 0x04, 0x00000007);
	rtl_write8(RTL_CONFIG0, 0x00);
	rtl_write8(RTL_CONFIG1, 0x00);

	rtl8139_reset();

	/* Read MAC */
	if (rtl_mem_base)
	{
		for (int i = 0; i < 6; i++)
			net_iface.mac.b[i] = rtl_mem_base[RTL_MAC0 + i];
	}
	else
	{
		for (int i = 0; i < 6; i++)
			net_iface.mac.b[i] = inb((uint16_t)(rtl_io_base + RTL_MAC0 + i));
	}

	/* disable interrupts, clear, ack pending */
	rtl_write16(RTL_IMR, 0);

	/* Program transmit config: use the single store-and-forward TX buffer */
	rtl_write32(RTL_TXCONF, 0x03000640);

	/* Set RX ring buffer address (physical, identity-mapped) */
	rtl_write32(RTL_RBSTART, (uint32_t)(uintptr_t)rx_buffer);

	/* RX config: 8 KiB ring (matching RTL_RXBUF_SIZE); accept broadcast,
	 * multicast and own-MAC frames; NO error/runt; wrap disabled so a full
	 * ring stalls reception instead of DMA-ing past the buffer. */
	rtl_write32(RTL_RXCONF, RTL_RXCONF_RX_BROADCAST |
		RTL_RXCONF_RX_MULTICAST | RTL_RXCONF_RX_PHYSICAL);

	rx_cur = 0;
	/* NOTE: CAPR is intentionally left at its reset value.  Writing CAPR=0
	 * makes QEMU's model store RxBufPtr = 0 + 16, so "avail" becomes 16 and
	 * every real packet (RX_ALIGN(len+8) >= 16) is dropped as RX overflow. */

	/* Enable RX & TX */
	rtl_write8(RTL_CMD, RTL_CMD_RX_EN | RTL_CMD_TX_EN);

	/* Enable receive interrupt */
	rtl_write16(RTL_IMR, RTL_IMR_RX_OK | RTL_IMR_RX_OVERFLOW | RTL_IMR_RX_ERROR | RTL_IMR_TX_OK | RTL_IMR_TX_ERROR);

	/* hook the IRQ if a valid one was assigned */
	if (dev->irq_line)
		irq_register(dev->irq_line, rtl8139_irq_handler);

	rtl_up = 1;
	return (0);
}

int	rtl8139_tx(uint8_t *data, size_t len)
{
	if (!rtl_up || len == 0 || len > NET_BUF_SIZE)
		return (-1);
	if (len < 60)
		len = 60;

	uint16_t	txs = (uint16_t)(RTL_TXSTATUS0 + 4 * tx_cur);
	uint16_t	txa = (uint16_t)(RTL_TXADDR0 + 4 * tx_cur);

	/* wait until the slot is host-owned (previous frame from it is done) */
	for (int tries = 0; tries < 1000; tries++)
	{
		if ((rtl_read32(txs) & 0x2000) != 0)
			break;
	}
	memcpy(tx_buffer, data, len);
	rtl_write32(txa, (uint32_t)(uintptr_t)tx_buffer);
	rtl_write32(txs, (uint32_t)len);

	/* the emulated/real NIC round-robins the 4 TX descriptors */
	tx_cur = (tx_cur + 1) & 3;
	return (0);
}
