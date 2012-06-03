#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>
#include <asm/unaligned.h>

#define DRIVER_VERSION "0.4"
#define DRIVER_AUTHOR "Ondra Havel <ondra.havel@gmail.com>"
#define DRIVER_DESC "USB Hanvon tablet driver"
#define DRIVER_LICENSE "GPL"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);

#define USB_VENDOR_ID_HANVON	0x0b57
#define USB_PRODUCT_ID_AM0806	0x8502
#define USB_PRODUCT_ID_AM0605	0x8503
#define USB_PRODUCT_ID_AM1107	0x8505
#define USB_PRODUCT_ID_AM1209	0x8501
#define USB_PRODUCT_ID_RL0604	0x851f
#define USB_AM_PACKET_LEN	10

static int lbuttons[]={BTN_0,BTN_1,BTN_2,BTN_3};	/* reported on all AMs */
static int rbuttons[]={BTN_4,BTN_5,BTN_6,BTN_7};	/* reported on AM1107+ */

#define AM_WHEEL_THRESHOLD	4

#define AM_MAX_ABS_X	0x27de
#define AM_MAX_ABS_Y	0x1cfe
#define AM_MAX_TILT_X	0x3f
#define AM_MAX_TILT_Y	0x7f
#define AM_MAX_PRESSURE	0x400

struct hanvon {
	unsigned char *data;
	dma_addr_t data_dma;
	struct input_dev *dev;
	struct usb_device *usbdev;
	struct urb *irq;
	int old_wheel_pos;
	char phys[32];
};

static void report_buttons(struct hanvon *hanvon, int buttons[],unsigned char dta)
{
	struct input_dev *dev = hanvon->dev;

	if((dta & 0xf0) == 0xa0) {
		input_report_key(dev, buttons[1], dta & 0x02);
		input_report_key(dev, buttons[2], dta & 0x04);
		input_report_key(dev, buttons[3], dta & 0x08);
	} else {
		if(dta <= 0x3f) {	/* slider area active */
			int diff = dta - hanvon->old_wheel_pos;
			if(abs(diff) < AM_WHEEL_THRESHOLD)
				input_report_rel(dev, REL_WHEEL, diff);

			hanvon->old_wheel_pos = dta;
		}
	}
}

static void hanvon_irq(struct urb *urb)
{
	struct hanvon *hanvon = urb->context;
	unsigned char *data = hanvon->data;
	struct input_dev *dev = hanvon->dev;
	int retval;

	switch (urb->status) {
		case 0:
			/* success */
			break;
		case -ECONNRESET:
		case -ENOENT:
		case -ESHUTDOWN:
			/* this urb is terminated, clean up */
			dbg("%s - urb shutting down with status: %d", __func__, urb->status);
			return;
		default:
			dbg("%s - nonzero urb status received: %d", __func__, urb->status);
			goto exit;
	}

	switch(data[0]) {
		case 0x01:	/* button press */
			if(data[1]==0x55)	/* left side */
				report_buttons(hanvon,lbuttons,data[2]);

			if(data[3]==0xaa)	/* right side (am1107, am1209) */
				report_buttons(hanvon,rbuttons,data[4]);
			break;
			
		case 0x02:	/* position change */
			if((data[1] & 0xf0) != 0) {
				input_report_abs(dev, ABS_X, get_unaligned_be16(&data[2]));
				input_report_abs(dev, ABS_Y, get_unaligned_be16(&data[4]));
				input_report_abs(dev, ABS_TILT_X, data[7] & 0x3f);
				input_report_abs(dev, ABS_TILT_Y, data[8]);
				input_report_abs(dev, ABS_PRESSURE, get_unaligned_be16(&data[6])>>6);
			}

		input_report_key(dev, BTN_LEFT, data[1] & 0x1);
		input_report_key(dev, BTN_RIGHT, data[1] & 0x2);		/* stylus button pressed (right click) */
		input_report_key(dev, lbuttons[0], data[1] & 0x20);
		break;
	}

	input_sync(dev);

exit:
	retval = usb_submit_urb (urb, GFP_ATOMIC);
	if (retval)
		err("%s - usb_submit_urb failed with result %d", __func__, retval);
}

static struct usb_device_id hanvon_ids[] = {
	{ USB_DEVICE(USB_VENDOR_ID_HANVON, USB_PRODUCT_ID_AM1209) },
	{ USB_DEVICE(USB_VENDOR_ID_HANVON, USB_PRODUCT_ID_AM1107) },
	{ USB_DEVICE(USB_VENDOR_ID_HANVON, USB_PRODUCT_ID_AM0806) },
	{ USB_DEVICE(USB_VENDOR_ID_HANVON, USB_PRODUCT_ID_AM0605) },
	{ USB_DEVICE(USB_VENDOR_ID_HANVON, USB_PRODUCT_ID_RL0604) },
	{ }
};

MODULE_DEVICE_TABLE(usb, hanvon_ids);

static int hanvon_open(struct input_dev *dev)
{
	struct hanvon *hanvon = input_get_drvdata(dev);

	hanvon->old_wheel_pos = -AM_WHEEL_THRESHOLD-1;
	hanvon->irq->dev = hanvon->usbdev;
	if (usb_submit_urb(hanvon->irq, GFP_KERNEL))
		return -EIO;

	return 0;
}

static void hanvon_close(struct input_dev *dev)
{
	struct hanvon *hanvon = input_get_drvdata(dev);

	usb_kill_urb(hanvon->irq);
}

static int hanvon_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *endpoint;
	struct hanvon *hanvon;
	struct input_dev *input_dev;
	int error = -ENOMEM, i;

	hanvon = kzalloc(sizeof(struct hanvon), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!hanvon || !input_dev)
		goto fail1;

	hanvon->data = (unsigned char *)usb_alloc_coherent(dev, USB_AM_PACKET_LEN, GFP_KERNEL, &hanvon->data_dma);
	if (!hanvon->data)
		goto fail1;

	hanvon->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!hanvon->irq)
		goto fail2;

	hanvon->usbdev = dev;
	hanvon->dev = input_dev;

	usb_make_path(dev, hanvon->phys, sizeof(hanvon->phys));
	strlcat(hanvon->phys, "/input0", sizeof(hanvon->phys));

	input_dev->name = "Hanvon Artmaster I tablet";
	input_dev->phys = hanvon->phys;
	usb_to_input_id(dev, &input_dev->id);
	input_dev->dev.parent = &intf->dev;

	input_set_drvdata(input_dev, hanvon);

	input_dev->open = hanvon_open;
	input_dev->close = hanvon_close;

	input_dev->evbit[0] |= BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS) | BIT_MASK(EV_REL);
	input_dev->keybit[BIT_WORD(BTN_DIGI)] |= BIT_MASK(BTN_TOOL_PEN) | BIT_MASK(BTN_TOUCH);
	input_dev->keybit[BIT_WORD(BTN_LEFT)] |= BIT_MASK(BTN_LEFT) | BIT_MASK(BTN_RIGHT);
	for(i=0;i<sizeof(lbuttons)/sizeof(lbuttons[0]);i++)
		__set_bit(lbuttons[i], input_dev->keybit);
	for(i=0;i<sizeof(rbuttons)/sizeof(rbuttons[0]);i++)
		__set_bit(rbuttons[i], input_dev->keybit);

	input_set_abs_params(input_dev, ABS_X, 0, AM_MAX_ABS_X, 4, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, AM_MAX_ABS_Y, 4, 0);
	input_set_abs_params(input_dev, ABS_TILT_X, 0, AM_MAX_TILT_X, 0, 0);
	input_set_abs_params(input_dev, ABS_TILT_Y, 0, AM_MAX_TILT_Y, 0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, AM_MAX_PRESSURE, 0, 0);
	input_set_capability(input_dev, EV_REL, REL_WHEEL);

	endpoint = &intf->cur_altsetting->endpoint[0].desc;

	usb_fill_int_urb(hanvon->irq, dev,
			 usb_rcvintpipe(dev, endpoint->bEndpointAddress),
			 hanvon->data, USB_AM_PACKET_LEN,
			 hanvon_irq, hanvon, endpoint->bInterval);
	hanvon->irq->transfer_dma = hanvon->data_dma;
	hanvon->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	error = input_register_device(hanvon->dev);
	if (error)
		goto fail3;

	usb_set_intfdata(intf, hanvon);
	return 0;

fail3:	usb_free_urb(hanvon->irq);
fail2:	usb_free_coherent(dev, USB_AM_PACKET_LEN, hanvon->data, hanvon->data_dma);
fail1:	input_free_device(input_dev);
	kfree(hanvon);
	return error;
}

static void hanvon_disconnect(struct usb_interface *intf)
{
	struct hanvon *hanvon = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);
	if (hanvon) {
		usb_kill_urb(hanvon->irq);
		input_unregister_device(hanvon->dev);
		usb_free_urb(hanvon->irq);
		usb_free_coherent(interface_to_usbdev(intf), USB_AM_PACKET_LEN, hanvon->data, hanvon->data_dma);
		kfree(hanvon);
	}
}

static struct usb_driver hanvon_driver = {
	.name =	"hanvon",
	.probe = hanvon_probe,
	.disconnect = hanvon_disconnect,
	.id_table =	hanvon_ids,
};

static int __init hanvon_init(void)
{
	int rv;

	if((rv = usb_register(&hanvon_driver)) != 0)
		return rv;

	printk(DRIVER_DESC " " DRIVER_VERSION "\n");

	return 0;
}

static void __exit hanvon_exit(void)
{
	usb_deregister(&hanvon_driver);
}

module_init(hanvon_init);
module_exit(hanvon_exit);
