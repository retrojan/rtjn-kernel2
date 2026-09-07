#ifndef USB_H
# define USB_H

# include <stdint.h>
# include <stddef.h>
# include "pci.h"

/* USB descriptor types */
# define USB_DT_DEVICE		0x01
# define USB_DT_CONFIG		0x02
# define USB_DT_STRING		0x03
# define USB_DT_INTERFACE	0x04
# define USB_DT_ENDPOINT	0x05

/* USB device classes */
# define USB_CLASS_PER_INTERFACE	0x00
# define USB_CLASS_HID				0x03
# define USB_CLASS_MASS_STORAGE	0x08
# define USB_CLASS_HUB				0x09

/* USB request types */
# define USB_REQ_GET_STATUS		0x00
# define USB_REQ_CLEAR_FEATURE	0x01
# define USB_REQ_SET_FEATURE	0x03
# define USB_REQ_SET_ADDRESS	0x05
# define USB_REQ_GET_DESCRIPTOR	0x06
# define USB_REQ_SET_DESCRIPTOR	0x07
# define USB_REQ_SET_CONFIGURATION	0x09

/* Control transfer */
# define USB_REQTYPE_DEV_TO_HOST	0x80
# define USB_REQTYPE_HOST_TO_DEV	0x00
# define USB_REQTYPE_IFACE_TO_HOST	0x81
# define USB_REQTYPE_CLASS			0x20

/* USB speed */
# define USB_SPEED_LOW		0
# define USB_SPEED_FULL		1
# define USB_SPEED_HIGH		2

# define USB_MAX_DEVICES		16
# define USB_MAX_ENDPOINTS		4

typedef struct usb_device_desc
{
	uint8_t		length;
	uint8_t		descriptor_type;
	uint16_t	bcd_usb;
	uint8_t		device_class;
	uint8_t		device_subclass;
	uint8_t		device_protocol;
	uint8_t		max_packet_size;
	uint16_t	vendor_id;
	uint16_t	product_id;
	uint16_t	bcd_device;
	uint8_t		manufacturer_index;
	uint8_t		product_index;
	uint8_t		serial_index;
	uint8_t		num_configurations;
} __attribute__((packed)) usb_device_desc_t;

typedef struct usb_endpoint_desc
{
	uint8_t		length;
	uint8_t		descriptor_type;
	uint8_t		endpoint_addr;
	uint8_t		attributes;
	uint16_t	max_packet_size;
	uint8_t		interval;
} __attribute__((packed)) usb_endpoint_desc_t;

typedef struct usb_interface_desc
{
	uint8_t		length;
	uint8_t		descriptor_type;
	uint8_t		interface_number;
	uint8_t		alt_setting;
	uint8_t		num_endpoints;
	uint8_t		interface_class;
	uint8_t		interface_subclass;
	uint8_t		interface_protocol;
	uint8_t		interface_index;
} __attribute__((packed)) usb_interface_desc_t;

typedef struct usb_config_desc
{
	uint8_t		length;
	uint8_t		descriptor_type;
	uint16_t	total_length;
	uint8_t		num_interfaces;
	uint8_t		configuration_value;
	uint8_t		configuration;
	uint8_t		attributes;
	uint8_t		max_power;
} __attribute__((packed)) usb_config_desc_t;

typedef struct usb_endpoint
{
	uint8_t		addr;
	uint8_t		attributes;
	uint16_t	max_packet;
	uint8_t		interval;
	int			in_use;
} usb_endpoint_t;

typedef struct usb_interface
{
	uint8_t		class;
	uint8_t		subclass;
	uint8_t		protocol;
	uint8_t		num_endpoints;
	usb_endpoint_t	endpoints[USB_MAX_ENDPOINTS];
} usb_interface_t;

typedef struct usb_device
{
	int			in_use;
	uint8_t		address;
	uint8_t		port;
	uint8_t		speed;
	uint8_t		slot;		/* UHCI slot */
	uint8_t		func;
	uint8_t		ep0_max_packet;
	uint16_t	vendor_id;
	uint16_t	product_id;
	uint8_t		device_class;
	uint8_t		num_configs;
	uint8_t		num_interfaces;
	usb_interface_t	interfaces[4];
} usb_device_t;

/* UHCI controller */
typedef struct uhci_controller
{
	uint16_t	io_base;
	uint8_t		irq;
	int			initialized;
} uhci_controller_t;

/* EHCI controller */
typedef struct ehci_controller
{
	volatile uint8_t	*mmio_base;
	uint32_t		phys_base;
	uint8_t			irq;
	int				initialized;
} ehci_controller_t;

/* Core USB functions */
void		usb_init(void);
int			usb_enumerate_device(uint8_t hub_port, uint8_t speed);
usb_device_t	*usb_get_device(uint8_t index);
int			usb_get_device_count(void);

/* USB control transfer */
int			usb_control_msg(uint8_t dev_addr, uint8_t ep,
				uint8_t req_type, uint8_t req, uint16_t val, uint16_t idx,
				void *data, uint16_t len, uint16_t timeout);

/* UHCI driver */
int			uhci_init(pci_device_t *dev);
int			uhci_submit_control(uint8_t dev_addr, uint8_t ep,
				uint8_t req_type, uint8_t req, uint16_t val, uint16_t idx,
				void *data, uint16_t len);

/* EHCI driver */
int			ehci_init(pci_device_t *dev);
int			ehci_submit_control(uint8_t dev_addr, uint8_t ep,
				uint8_t req_type, uint8_t req, uint16_t val, uint16_t idx,
				void *data, uint16_t len);

#endif
