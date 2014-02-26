/*
 * Driver for the Diolan u2c-12 USB-I2C adapter
 *
 * Copyright (c) 2010-2011 Ericsson AB
 *
 * Derived from:
 *  i2c-tiny-usb.c
 *  Copyright (C) 2006-2007 Till Harbaum (Till@Harbaum.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/i2c.h>

#define DRIVER_NAME		"i2c-ch341-u2c"

#define USB_VENDOR_ID_CH341		0x1a86
#define USB_DEVICE_ID_CH341_U2C	0x5512
#define BULK_WRITE_ENDPOINT         0x02
#define BULK_READ_ENDPOINT          0x82
#define DEFAULT_INTERFACE           0x00

#define DEFAULT_CONFIGURATION       0x01
#define DEFAULT_TIMEOUT             100    // 300mS for USB timeouts

// Based on (closed-source) DLL V1.9 for USB by WinChipHead (c) 2005.
// Supports USB chips: CH341, CH341A

#define                mCH341_PACKET_LENGTH        32
#define                mCH341_PKT_LEN_SHORT        8

#define                mCH341_ENDP_INTER_UP        0x81
#define                mCH341_ENDP_INTER_DOWN        0x01
#define                mCH341_ENDP_DATA_UP                0x82
#define                mCH341_ENDP_DATA_DOWN        0x02

#define                mCH341_VENDOR_READ                0xC0
#define                mCH341_VENDOR_WRITE                0x40

#define                mCH341_PARA_INIT                0xB1
#define                mCH341_I2C_STATUS                0x52
#define                mCH341_I2C_COMMAND                0x53

#define                mCH341_PARA_CMD_R0                0xAC
#define                mCH341_PARA_CMD_R1                0xAD
#define                mCH341_PARA_CMD_W0                0xA6
#define                mCH341_PARA_CMD_W1                0xA7
#define                mCH341_PARA_CMD_STS                0xA0

#define                mCH341A_CMD_SET_OUTPUT        0xA1
#define                mCH341A_CMD_IO_ADDR                0xA2
#define                mCH341A_CMD_PRINT_OUT        0xA3
#define                mCH341A_CMD_SPI_STREAM        0xA8
#define                mCH341A_CMD_SIO_STREAM        0xA9
#define                mCH341A_CMD_I2C_STREAM        0xAA
#define                mCH341A_CMD_UIO_STREAM        0xAB

#define                mCH341A_BUF_CLEAR                0xB2
#define                mCH341A_I2C_CMD_X                0x54
#define                mCH341A_DELAY_MS                0x5E
#define                mCH341A_GET_VER                        0x5F

#define                mCH341_EPP_IO_MAX                ( mCH341_PACKET_LENGTH - 1 )
#define                mCH341A_EPP_IO_MAX                0xFF

#define                mCH341A_CMD_IO_ADDR_W        0x00
#define                mCH341A_CMD_IO_ADDR_R        0x80

#define                mCH341A_CMD_I2C_STM_STA        0x74
#define                mCH341A_CMD_I2C_STM_STO        0x75
#define                mCH341A_CMD_I2C_STM_OUT        0x80
#define                mCH341A_CMD_I2C_STM_IN        0xC0
#define                mCH341A_CMD_I2C_STM_MAX        ( min( 0x3F, mCH341_PACKET_LENGTH ) )
#define                mCH341A_CMD_I2C_STM_SET        0x60
#define                mCH341A_CMD_I2C_STM_US        0x40
#define                mCH341A_CMD_I2C_STM_MS        0x50
#define                mCH341A_CMD_I2C_STM_DLY        0x0F
#define                mCH341A_CMD_I2C_STM_END        0x00

#define                mCH341A_CMD_UIO_STM_IN        0x00
#define                mCH341A_CMD_UIO_STM_DIR        0x40
#define                mCH341A_CMD_UIO_STM_OUT        0x80
#define                mCH341A_CMD_UIO_STM_US        0xC0
#define                mCH341A_CMD_UIO_STM_END        0x20

#define                mCH341_PARA_MODE_EPP        0x00
#define                mCH341_PARA_MODE_EPP17        0x00
#define                mCH341_PARA_MODE_EPP19        0x01
#define                mCH341_PARA_MODE_MEM        0x02


#define CH341_I2C_LOW_SPEED 0               // low speed - 20kHz               
#define CH341_I2C_STANDARD_SPEED 1          // standard speed - 100kHz
#define CH341_I2C_FAST_SPEED 2              // fast speed - 400kHz
#define CH341_I2C_HIGH_SPEED 3              // high speed - 750kHz


/* commands via USB, must match command ids in the firmware */
#define CMD_I2C_READ		0x01
#define CMD_I2C_WRITE		0x02
#define CMD_I2C_SCAN		0x03	/* Returns list of detected devices */
#define CMD_I2C_RELEASE_SDA	0x04
#define CMD_I2C_RELEASE_SCL	0x05
#define CMD_I2C_DROP_SDA	0x06
#define CMD_I2C_DROP_SCL	0x07
#define CMD_I2C_READ_SDA	0x08
#define CMD_I2C_READ_SCL	0x09
#define CMD_GET_FW_VERSION	0x0a
#define CMD_GET_SERIAL		0x0b
#define CMD_I2C_START		0x0c
#define CMD_I2C_STOP		0x0d
#define CMD_I2C_REPEATED_START	0x0e
#define CMD_I2C_PUT_BYTE	0x0f
#define CMD_I2C_GET_BYTE	0x10
#define CMD_I2C_PUT_ACK		0x11
#define CMD_I2C_GET_ACK		0x12
#define CMD_I2C_PUT_BYTE_ACK	0x13
#define CMD_I2C_GET_BYTE_ACK	0x14
#define CMD_I2C_SET_SPEED	0x1b
#define CMD_I2C_GET_SPEED	0x1c
#define CMD_I2C_SET_CLK_SYNC	0x24
#define CMD_I2C_GET_CLK_SYNC	0x25
#define CMD_I2C_SET_CLK_SYNC_TO	0x26
#define CMD_I2C_GET_CLK_SYNC_TO	0x27



#define RESP_OK			0x00
#define RESP_FAILED		0x01
#define RESP_BAD_MEMADDR	0x04
#define RESP_DATA_ERR		0x05
#define RESP_NOT_IMPLEMENTED	0x06
#define RESP_NACK		0x07
#define RESP_TIMEOUT		0x09

#define U2C_I2C_SPEED_FAST	0	/* 400 kHz */
#define U2C_I2C_SPEED_STD	1	/* 100 kHz */
#define U2C_I2C_SPEED_2KHZ	242	/* 2 kHz, minimum speed */
#define U2C_I2C_SPEED(f)	((DIV_ROUND_UP(1000000, (f)) - 10) / 2 + 1)

#define U2C_I2C_FREQ_FAST	400000
#define U2C_I2C_FREQ_STD	100000
#define U2C_I2C_FREQ(s)		(1000000 / (2 * (s - 1) + 10))

#define DIOLAN_USB_TIMEOUT	100	/* in ms */
#define DIOLAN_SYNC_TIMEOUT	20	/* in ms */

#define DIOLAN_OUTBUF_LEN	128
#define DIOLAN_FLUSH_LEN	(DIOLAN_OUTBUF_LEN - 4)
#define DIOLAN_INBUF_LEN	256	/* Maximum supported receive length */

/* Structure to hold all of our device specific stuff */
struct i2c_ch341_u2c {
	u8 obuffer[DIOLAN_OUTBUF_LEN];	/* output buffer */
	u8 ibuffer[DIOLAN_INBUF_LEN];	/* input buffer */
	int ep_in, ep_out;              /* Endpoints    */
	struct usb_device *usb_dev;	/* the usb device for this device */
	struct usb_interface *interface;/* the interface for this device */
	struct i2c_adapter adapter;	/* i2c related things */
	int olen;			/* Output buffer length */
	int ocount;			/* Number of enqueued messages */
	int ilen;
	int check_ack;
};

static uint frequency = U2C_I2C_FREQ_STD;	/* I2C clock frequency in Hz */

module_param(frequency, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(frequency, "I2C clock frequency in hertz");

/* usb layer */

/* Send command to device, and get response. */
static int ch341_usb_transfer(struct i2c_ch341_u2c *dev)
{
	int ret = 0;
	int actual;
	int i;

	if (!dev->olen || !dev->ocount)
		return -EINVAL;
//dev_info(&dev->interface->dev,		 "olen=%d ilen=%d, ocount=%d, obufffer=%x %x %x",dev->olen,dev->ilen,dev->ocount,dev->obuffer[0],dev->obuffer[1],dev->obuffer[2]);


	ret = usb_bulk_msg(dev->usb_dev,
			   usb_sndbulkpipe(dev->usb_dev, dev->ep_out),
			   dev->obuffer, dev->olen, &actual,
			   DEFAULT_TIMEOUT);
	//print_hex_dump_bytes("obuffer ",DUMP_PREFIX_NONE,dev->obuffer,dev->olen);
	//	dev_info(&dev->interface->dev,"after write=%d act=%d \n",ret,actual);
	if (!ret ) {
	 //usleep_range(30,50);
		dev->ocount=1;
		for (i = 0; i < dev->ocount; i++) {
			int tmpret;
			memset(dev->ibuffer,0,256);
			tmpret = usb_bulk_msg(dev->usb_dev,
					      usb_rcvbulkpipe(dev->usb_dev,
							      dev->ep_in),
					      dev->ibuffer,
					      sizeof(dev->ibuffer), &actual,
					      DEFAULT_TIMEOUT);
			//print_hex_dump_bytes("ibuffer ",DUMP_PREFIX_NONE,dev->ibuffer,actual);
			dev_info(&dev->interface->dev,"after read %d %d (ret was %d) \n",tmpret,actual,ret);

			/*
			 * Stop command processing if a previous command
			 * returned an error.
			 * Note that we still need to retrieve all messages.
			 */
			dev_info(&dev->interface->dev,"obuffer[0]=%02x =%02x (len=%d) ibuffer[0]=%02x",dev->obuffer[0],dev->obuffer[1]&0xf0,dev->olen,dev->ibuffer[0]);
			if (dev->olen>1 && dev->obuffer[0]==0xaa && dev->obuffer[1]==0x60 ) {
				return 0; // set speed should not fail
			}
			//if (actual>0)
				ret = tmpret;
				//dev->ilen=actual;
			if ( dev->check_ack==0 && (dev->ibuffer[0 ] & 0x80) !=0) { //detect chip write len 0
					dev_info(&dev->interface->dev,"_nackonly %d ",-EIO);
				return -EIO;
			}
			if (ret == 0 && actual > 0 ) {
				dev_info(&dev->interface->dev,"__ibuf 0=%02x 1=%02x  ",dev->ibuffer[0],dev->ibuffer[1]);
				ret=actual;
				dev->ilen=actual;
				/*switch (dev->ibuffer[actual - 1]&0x80) {
				
				case RESP_TIMEOUT:
					ret = -ETIMEDOUT;
					break;
				case RESP_OK:
					ret = actual - 1;
					break;
				default:
					//ret = 0;
					dev->ilen=actual;
					break;
				}*/
				
			}
		}
	}
	dev->olen = 0;
	dev->ocount = 0;
	//dev->ilen = 0;
//dev_info(&dev->interface->dev,"ret ret=%d act=%d",ret,actual);
	return ret;
}


/* Send command (no data) */
static int ch341_usb_cmd_msg(struct i2c_ch341_u2c *dev, u8* msg,u8 len)
{
	//dev_info(&dev->interface->dev,"%s",__FUNCTION__);
	memcpy(dev->obuffer,msg,len);
	dev->olen=len;
	dev->ocount++;
	return ch341_usb_transfer(dev);
}


static int ch341_usb_cmd_read_addr(struct i2c_ch341_u2c *dev, u8 addr,u8* data,bool recv_len){
	u8 msg0[256];
	int msgsize=0,ret,ilen=2;//dev->ilen;//*datalen;
	msg0[msgsize++]=	mCH341A_CMD_I2C_STREAM;
	msg0[msgsize++]=	mCH341A_CMD_I2C_STM_STA;
	msg0[msgsize++]=	mCH341A_CMD_I2C_STM_OUT |(recv_len?1:0); // 1 bytE
	msg0[msgsize++]=	(addr ) |0x01 ;//&0xfe;
	
	for (;ilen>0  ;ilen--){
		msg0[msgsize++]=mCH341A_CMD_I2C_STM_IN|(ilen-1);
	}
	//msg0[msgsize++]=mCH341A_CMD_I2C_STM_IN|*datalen;
	msg0[msgsize++]=mCH341A_CMD_I2C_STM_STO;
	msg0[msgsize++]=mCH341A_CMD_I2C_STM_END;

	//dev->ilen=ilen;
		dev->check_ack = recv_len;
		ret=ch341_usb_cmd_msg(dev,msg0,msgsize);
		//ret=dev->ilen;
		//*datalen=1;
		dev_info(&dev->interface->dev,"after read=%d len=%d %d",ret,dev->ilen,recv_len);
		
		//*datalen=dev->ilen;
		dev_info(&dev->interface->dev,"buf1: %02x %02x %d\n",dev->ibuffer[0],dev->ibuffer[1],dev->ilen);
		if (ret>=0){
			memcpy(data,dev->ibuffer+(recv_len?0:1),dev->ilen-(recv_len?1:0));
		}
		return ret;

}
static int ch341_usb_cmd_write_addr(struct i2c_ch341_u2c *dev, u8 addr,u8 *data, u8 datalen ){
	//return -1;
		u8 msg0[256];//={
	//}; 
	int msgsize=0,ret=-5;
	dev_info(&dev->interface->dev," write len=%d",datalen);
	msg0[msgsize++]=mCH341A_CMD_I2C_STREAM,
	msg0[msgsize++]=mCH341A_CMD_I2C_STM_STA,
	msg0[msgsize++]=	mCH341A_CMD_I2C_STM_OUT |(datalen>0?datalen+1:0), // 1 byte
	msg0[msgsize++]=	addr &0xfe,
	dev->check_ack=0;//(datalen>0);
	memcpy(msg0+msgsize,data,datalen);
	msgsize+=datalen;
	
	msg0[msgsize++]=mCH341A_CMD_I2C_STM_STO;
	msg0[msgsize++]=mCH341A_CMD_I2C_STM_END;
	//dev->ilen=0;
	
	//print_hex_dump_bytes("to write ",DUMP_PREFIX_NONE,msg0,msgsize);
	ret=ch341_usb_cmd_msg(dev,msg0,msgsize);
	dev_info(&dev->interface->dev,"after write=%d len=%d",ret,datalen);
	if (datalen>0 && ret ==-ETIMEDOUT){
		ret=0;
	}
	return ret;
	
}
static int ch341_i2c_start(struct i2c_ch341_u2c *dev)
{
	u8 msg[]={
		mCH341A_CMD_I2C_STREAM,
		mCH341A_CMD_I2C_STM_STA,
		mCH341A_CMD_I2C_STM_END
	};
	dev_info(&dev->interface->dev,"%s",__FUNCTION__);
	return ch341_usb_cmd_msg(dev,msg,3);
}
static int ch341_i2c_stop(struct i2c_ch341_u2c *dev)
{
	u8 msg[]={
		mCH341A_CMD_I2C_STREAM,
		mCH341A_CMD_I2C_STM_STO,
		mCH341A_CMD_I2C_STM_END
	};
	dev_info(&dev->interface->dev,"%s",__FUNCTION__);
	return ch341_usb_cmd_msg(dev,msg,3);
}


static int ch341_set_speed(struct i2c_ch341_u2c *dev, u8 speed)
{
	u8 msg[]={
		mCH341A_CMD_I2C_STREAM,
		mCH341A_CMD_I2C_STM_SET | (speed & 0x03),
		mCH341A_CMD_I2C_STM_END
	};
	dev_info(&dev->interface->dev,"%s",__FUNCTION__);
	return ch341_usb_cmd_msg(dev,msg,3);
}


static int ch341_init(struct i2c_ch341_u2c *dev)
{
	int speed, freq,ret;
	dev_info(&dev->interface->dev,"%s",__FUNCTION__);
	if (frequency >= 750000) {
		speed = CH341_I2C_HIGH_SPEED;
		freq=frequency;
	} else if (frequency >= 400000) {
		speed = CH341_I2C_FAST_SPEED;
		freq=frequency;
	} else if (frequency >= 200000 || frequency == 0) {
		speed = CH341_I2C_STANDARD_SPEED;
		freq=frequency;
	} else {
		speed = CH341_I2C_LOW_SPEED;
		freq=frequency;
	}

	dev_info(&dev->interface->dev,
		 "CH341 U2C at USB bus %03d address %03d speed %d Hz\n",
		 dev->usb_dev->bus->busnum, dev->usb_dev->devnum,freq);


	/* Set I2C speed */
	ret = ch341_set_speed(dev, speed);
	dev_info(&dev->interface->dev,"rest set speed=%d",ret);
	if (ret < 0)
		return ret;
	//ch341_flush_input(dev);
	dev_info(&dev->interface->dev,"all done set speed=%d",ret);
	return ret;
}

/* i2c layer */

static int ch341_usb_xfer(struct i2c_adapter *adapter, struct i2c_msg *msgs,
			   int num)
{
	struct i2c_ch341_u2c *dev = i2c_get_adapdata(adapter);
	struct i2c_msg *pmsg;
	int i,ret;
	
	
		//dev_info(&dev->interface->dev,"new read num=%x",num);
	//ch341_i2c_start(dev);
	for (i = 0; i < num; i++) {
		pmsg = &msgs[i];
	dev_info(&dev->interface->dev,"addr=%x num=%d len=%d rd=%d rcv_len=%d ignor_nak=%d m_no_rd_ack=%d %02x %02x %02x \n",pmsg->addr,num,pmsg->len,pmsg->flags & I2C_M_RD,pmsg->flags & I2C_M_RECV_LEN,pmsg->flags & I2C_M_IGNORE_NAK , pmsg->flags & I2C_M_NO_RD_ACK,pmsg->len>0?pmsg->buf[0]:0,pmsg->len>1?pmsg->buf[1]:0,pmsg->len>2?pmsg->buf[2]:0);
//	continue;

		//dev_info(&dev->interface->dev,"read flags=%x",pmsg->flags);
		if (pmsg->flags & I2C_M_RD) {
			int recv_len= pmsg->flags & I2C_M_RECV_LEN;
			dev->ilen=recv_len;
			//dev_info(&dev->interface->dev,"read addr=%x len=%d data0=%x",pmsg->addr,pmsg->len,pmsg->len>0?pmsg->buf[0]:-1);
			ret=ch341_usb_cmd_read_addr(dev,pmsg->addr<<1 ,pmsg->buf,recv_len);
			//dev_info(&dev->interface->dev,"read addr=%x len=%d data0=%x",pmsg->buf,pmsg->len,pmsg->len>0?pmsg->buf);
			//print_hex_dump_bytes("ibuffer ",DUMP_PREFIX_NONE,pmsg->buf,pmsg->len);
			if (recv_len && ret >0 ){
				pmsg->len = ret;
			}
			dev_info(&dev->interface->dev,"ret=%x",ret);
			if (ret < 0)
				goto abort;
			/*for (j = 0; j < pmsg->len; j++) {
				u8 byte;
				
				if (j == 0 && (pmsg->flags & I2C_M_RECV_LEN)) {
					if ( //byte == 0 ||
					    byte > I2C_SMBUS_BLOCK_MAX) {
						ret = -EPROTO;
						goto abort;
					}
					//pmsg->len += byte;
				}
				//pmsg->buf[j] = byte;
			}*/
			//pmsg->buf[0]=0x01;
			//pmsg->len=1;
			dev_info(&dev->interface->dev,"buf: %02x %d\n",pmsg->buf[0],pmsg->len);
		} else {
	//dev_info(&dev->interface->dev,"write addr=%x len=%d data0=%x",pmsg->addr,pmsg->len,pmsg->len>0?pmsg->buf[0]:-1);
			ret = ch341_usb_cmd_write_addr(dev,pmsg->addr <<1,pmsg->buf,pmsg->len);
			//dev_info(&dev->interface->dev,"i2c ret after write=%d",ret);
			if (ret < 0)
				goto abort;
		}
	}
	ret = num;
abort:
	//ch341_i2c_stop(dev);
//dev_info(&dev->interface->dev,"i2c ret=%d",ret);
	return (ret <0 ) ?ret :num;
}

/*
 * Return list of supported functionality.
 */
static u32 ch341_usb_func(struct i2c_adapter *a)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL ;/*|
	       I2C_FUNC_SMBUS_READ_BLOCK_DATA | I2C_FUNC_SMBUS_BLOCK_PROC_CALL;*/
}

static const struct i2c_algorithm ch341_usb_algorithm = {
	.master_xfer = ch341_usb_xfer,
	.functionality = ch341_usb_func,
};

/* device layer */

static const struct usb_device_id ch341_u2c_table[] = {
	{ USB_DEVICE(USB_VENDOR_ID_CH341, USB_DEVICE_ID_CH341_U2C) },
	{ }
};

MODULE_DEVICE_TABLE(usb, ch341_u2c_table);

static void ch341_u2c_free(struct i2c_ch341_u2c *dev)
{
	usb_put_dev(dev->usb_dev);
	kfree(dev);
}

static int ch341_u2c_probe(struct usb_interface *interface,
			    const struct usb_device_id *id)
{
	struct usb_device *udev;
	struct usb_host_interface *hostif = interface->cur_altsetting;
	struct i2c_ch341_u2c *dev;
	const int ifnum = interface->altsetting[DEFAULT_CONFIGURATION].desc.bInterfaceNumber;
    char *speed;
	int ret;

	if (hostif->desc.bInterfaceNumber != 0
	    || hostif->desc.bNumEndpoints < 2)
		return -ENODEV;

	/* allocate memory for our device state and initialize it */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		dev_err(&interface->dev, "no memory for device state\n");
		ret = -ENOMEM;
		goto error;
	}
	dev->ep_out = hostif->endpoint[1].desc.bEndpointAddress;
	dev_info(&interface->dev, "ep_out=%x\n",dev->ep_out);
	dev->ep_in = hostif->endpoint[0].desc.bEndpointAddress;
	dev_info(&interface->dev, "ep_in=%x\n",dev->ep_in);

	dev->usb_dev = usb_get_dev(interface_to_usbdev(interface));
	dev->interface = interface;
	
	udev = dev->usb_dev;
	    switch (udev->speed) {
    case USB_SPEED_LOW:
        speed = "1.5";
        break;
    case USB_SPEED_UNKNOWN:
    case USB_SPEED_FULL:
        speed = "12";
        break;
    case USB_SPEED_HIGH:
        speed = "480";
        break;
    default:
        speed = "unknown";
    }
	 printk(KERN_INFO DRIVER_NAME
        ": New device %s %s @ %s Mbps "
        "(%04x:%04x, interface %d, class %d, version %d.%02d)\n",
        udev->manufacturer ? udev->manufacturer : "",
        udev->product ? udev->product : "",
        speed,
        le16_to_cpu(udev->descriptor.idVendor),
        le16_to_cpu(udev->descriptor.idProduct),
        ifnum,
        interface->altsetting->desc.bInterfaceNumber,
				le16_to_cpu(udev->descriptor.bcdDevice%0xff),
				le16_to_cpu(udev->descriptor.bcdDevice>>8%0xff)
 				);

	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, dev);

	/* setup i2c adapter description */
	dev->adapter.owner = THIS_MODULE;
	dev->adapter.class = I2C_CLASS_HWMON;
	dev->adapter.algo = &ch341_usb_algorithm;
	i2c_set_adapdata(&dev->adapter, dev);
	snprintf(dev->adapter.name, sizeof(dev->adapter.name),
		 DRIVER_NAME " at bus %03d device %03d",
		 dev->usb_dev->bus->busnum, dev->usb_dev->devnum);

	dev->adapter.dev.parent = &dev->interface->dev;
	/* initialize ch341 i2c interface */
	ret = ch341_init(dev);
	
	if (ret < 0) {
		dev_err(&interface->dev, "failed to initialize adapter\n");
		goto error_free;
	}
	/* and finally attach to i2c layer */
	ret = i2c_add_adapter(&dev->adapter);
	if (ret < 0) {
		dev_err(&interface->dev, "failed to add I2C adapter\n");
		goto error_free;
	}

	dev_dbg(&interface->dev, "connected " DRIVER_NAME "\n");

	return 0;

error_free:
	usb_set_intfdata(interface, NULL);
	ch341_u2c_free(dev);
error:
	return ret;
}

static void ch341_u2c_disconnect(struct usb_interface *interface)
{
	struct i2c_ch341_u2c *dev = usb_get_intfdata(interface);

	i2c_del_adapter(&dev->adapter);
	usb_set_intfdata(interface, NULL);
	ch341_u2c_free(dev);

	dev_dbg(&interface->dev, "disconnected\n");
}

static struct usb_driver ch341_u2c_driver = {
	.name = DRIVER_NAME,
	.probe = ch341_u2c_probe,
	.disconnect = ch341_u2c_disconnect,
	.id_table = ch341_u2c_table,
};

module_usb_driver(ch341_u2c_driver);

MODULE_AUTHOR("Guenter Roeck <linux@roeck-us.net>");
MODULE_DESCRIPTION(DRIVER_NAME " driver");
MODULE_LICENSE("GPL");
