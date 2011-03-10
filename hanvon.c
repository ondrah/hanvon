#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>
#include <asm/unaligned.h>

#define DRIVER_VERSION "v0.0.1"
#define DRIVER_AUTHOR "Ondra Havel <ondra.havel@gmail.com>"
#define DRIVER_DESC "USB Hanvon AM0806 tablet driver"
#define DRIVER_LICENSE "GPL"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);

#define USB_VENDOR_ID_HANVON	0x0b57

#define B0	BTN_TOOL_RUBBER
#define B1	BTN_TOOL_FINGER
#define B2	BTN_TOOL_PENCIL
#define B3	BTN_TOOL_AIRBRUSH
#define WHEEL_THRESHOLD	10

struct hanvon {
	unsigned char *data;
	dma_addr_t data_dma;
	struct input_dev *dev;
	struct usb_device *usbdev;
	struct urb *irq;
	int x, y;
	unsigned b0:1;
	unsigned b1:1;
	unsigned b2:1;
	unsigned b3:1;
	int old_wheel_pos;
	int pressure;
	char phys[32];
};

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
		case 0x01:	// button press
			if((data[2] & 0xf0) == 0xa0) {
				hanvon->b1 = data[2] & 0x02;
				hanvon->b2 = data[2] & 0x04;
				hanvon->b3 = data[2] & 0x08;

				input_report_key(dev, B1, hanvon->b1);
				input_report_key(dev, B2, hanvon->b2);
				input_report_key(dev, B3, hanvon->b3);
			} else {
				if(data[2] <= 0x3f) {	// slider area active
					int diff = data[2] - hanvon->old_wheel_pos;
					if(abs(diff) < WHEEL_THRESHOLD)
						input_report_key(dev, REL_WHEEL, diff);

					hanvon->old_wheel_pos = data[2];
				}
			}

			break;
			
		case 0x02:	// position change
			if((data[1] & 0xf0) != 0) {
				hanvon->x = get_unaligned_be16(&data[2]);
				hanvon->y = get_unaligned_be16(&data[4]);
				hanvon->pressure = get_unaligned_be16(&data[6]);
			} else {
				hanvon->pressure = 0;
			}

			hanvon->b0 = data[1] & 0x20;

			//input_report_key(dev, BTN_TOUCH, data[1] & 0x1);

			input_report_key(dev, BTN_LEFT, data[1] & 0x1);
			input_report_key(dev, BTN_RIGHT, data[1] & 0x2);		// stylus button pressed (right click)

			input_report_abs(dev, ABS_X, hanvon->x);
			input_report_abs(dev, ABS_Y, hanvon->y);
			input_report_abs(dev, ABS_PRESSURE, hanvon->pressure);
			input_report_key(dev, B0, hanvon->b0);

			break;
	}

	input_sync(dev);

exit:
	retval = usb_submit_urb (urb, GFP_ATOMIC);
	if (retval)
		err("%s - usb_submit_urb failed with result %d", __func__, retval);
}

static struct usb_device_id hanvon_ids[] = {
	{ USB_DEVICE(USB_VENDOR_ID_HANVON, 0x8502), .driver_info = 0 },
	{ }
};

MODULE_DEVICE_TABLE(usb, hanvon_ids);

static int hanvon_open(struct input_dev *dev)
{
	struct hanvon *hanvon = input_get_drvdata(dev);

	hanvon->old_wheel_pos = -WHEEL_THRESHOLD-1;
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
	int error = -ENOMEM;

	hanvon = kzalloc(sizeof(struct hanvon), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!hanvon || !input_dev)
		goto fail1;

	//hanvon->data = (unsigned char *)usb_buffer_alloc(dev, 8, GFP_KERNEL, &hanvon->data_dma);
	hanvon->data = (unsigned char *)usb_alloc_coherent(dev, 10, GFP_KERNEL, &hanvon->data_dma);
	if (!hanvon->data)
		goto fail1;

	hanvon->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!hanvon->irq)
		goto fail2;

	hanvon->usbdev = dev;
	hanvon->dev = input_dev;

	usb_make_path(dev, hanvon->phys, sizeof(hanvon->phys));
	strlcat(hanvon->phys, "/input0", sizeof(hanvon->phys));

	input_dev->name = "Hanvon AM0806 Tablet";
	input_dev->phys = hanvon->phys;
	usb_to_input_id(dev, &input_dev->id);
	input_dev->dev.parent = &intf->dev;

	input_set_drvdata(input_dev, hanvon);

	input_dev->open = hanvon_open;
	input_dev->close = hanvon_close;

	input_dev->evbit[0] |= BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS) |
		BIT_MASK(EV_MSC);
	input_dev->keybit[BIT_WORD(BTN_LEFT)] |= BIT_MASK(BTN_LEFT) |
		BIT_MASK(BTN_RIGHT) | BIT_MASK(BTN_MIDDLE);
	input_dev->keybit[BIT_WORD(BTN_DIGI)] |= BIT_MASK(BTN_TOOL_PEN) |
		BIT_MASK(BTN_TOUCH);
	input_dev->mscbit[0] |= BIT_MASK(MSC_SERIAL);
	input_set_abs_params(input_dev, ABS_X, 0, 0x27de, 4, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, 0x1cfe, 4, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, 0xffffffff, 4, 0);

	endpoint = &intf->cur_altsetting->endpoint[0].desc;

	usb_fill_int_urb(hanvon->irq, dev,
			 usb_rcvintpipe(dev, endpoint->bEndpointAddress),
			 hanvon->data, 10,
			 hanvon_irq, hanvon, endpoint->bInterval);
	hanvon->irq->transfer_dma = hanvon->data_dma;
	hanvon->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	error = input_register_device(hanvon->dev);
	if (error)
		goto fail3;

	usb_set_intfdata(intf, hanvon);

	return 0;

 fail3:	usb_free_urb(hanvon->irq);
 //fail2:	usb_buffer_free(dev, 10, hanvon->data, hanvon->data_dma);
 fail2:	usb_free_coherent(dev, 10, hanvon->data, hanvon->data_dma);
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
		//usb_buffer_free(interface_to_usbdev(intf), 10, hanvon->data, hanvon->data_dma);
		usb_free_coherent(interface_to_usbdev(intf), 10, hanvon->data, hanvon->data_dma);
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

	printk(DRIVER_VERSION ":" DRIVER_DESC "\n");

	return 0;
}

static void __exit hanvon_exit(void)
{
	usb_deregister(&hanvon_driver);
}

module_init(hanvon_init);
module_exit(hanvon_exit);
