#ifndef RTL8139_H
# define RTL8139_H

# include <stdint.h>
# include "io.h"
# include "net.h"

/* RTL8139 register offsets (I/O ports relative to BAR) */
# define RTL_MAC0		0x00	/* MAC address (6 bytes) */
# define RTL_MAR0		0x08
# define RTL_TXSTATUS0	0x10
# define RTL_TXADDR0	0x20
# define RTL_RBSTART	0x30	/* receive buffer start address */
# define RTL_CMD		0x37	/* command register */
# define RTL_CAPR		0x38	/* current address of packet read */
# define RTL_IMR		0x3C	/* interrupt mask */
# define RTL_ISR		0x3E	/* interrupt status */
# define RTL_TXCONF		0x40
# define RTL_RXCONF		0x44
# define RTL_CONFIG1	0x52
# define RTL_CONFIG0	0x51

# define RTL_CMD_RESET		0x10
# define RTL_CMD_RX_EN		0x08
# define RTL_CMD_TX_EN		0x04
# define RTL_CMD_RX_BUF_EMPTY	0x01

# define RTL_IMR_RX_OK		0x0001
# define RTL_IMR_TX_OK		0x0004
# define RTL_IMR_RX_ERROR	0x0020
# define RTL_IMR_TX_ERROR	0x0040
# define RTL_IMR_RX_OVERFLOW	0x0010

# define RTL_ISR_RX_OK		0x0001
# define RTL_ISR_RX_ERROR	0x0020
# define RTL_ISR_RX_OVERFLOW	0x0010

# define RTL_RXCONF_DRTH	0
# define RTL_RXCONF_RXFIFO_8K	0x0000
# define RTL_RXCONF_RXFIFO_NONE	0x3000
# define RTL_RXCONF_MULTI_ERINT	0x8000
# define RTL_RXCONF_RX_64K	0x1800	/* RX buffer size bits 12:11 (64 KiB) */
# define RTL_RXCONF_RX_BROADCAST	0x0008	/* accept broadcast (RCR bit 3) */
# define RTL_RXCONF_RX_MULTICAST	0x0004	/* accept multicast (RCR bit 2) */
# define RTL_RXCONF_RX_PHYSICAL	0x0002	/* accept own-MAC match (RCR bit 1) */
# define RTL_RXCONF_RX_RUNT	0x0020
# define RTL_RXCONF_RX_ERROR	0x0040
# define RTL_RXCONF_WRAP		0x0001

# define RTL_RXBUF_SIZE	8192

int		rtl8139_init(pci_device_t *dev);
void	rtl8139_irq_handler(regs_t *re);
int		rtl8139_tx(uint8_t *data, size_t len);
void	rtl8139_poll(void);

/* hooks into the net stack rx path */
extern int	net_handle_frame(uint8_t *frame, size_t len);

#endif
