/*
 * 	bmi_video.c
 *
 * 	BMI Video module device driver
 *
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*
 *	Include files
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/gpio.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <mach/hardware.h>
#include <linux/i2c.h>
#include <linux/i2c/tsc2004.h>
#include <linux/bmi.h>
#include <linux/bmi/bmi-slot.h>
#include <linux/bmi/bmi_lcd.h>
#include <mach/display.h>
#include <linux/fb.h>
#include <linux/list.h>
#include <linux/slab.h>

#include <linux/bmi/bmi_video.h>

#include <../drivers/video/omap2/displays/ths8200.h>
#include <../drivers/video/omap2/displays/tfp410.h>

//dss display structs
static struct omap_dss_device *dvi_disp;
static struct omap_dss_device *vga_disp;

//private device structure
struct bmi_video
{
  struct bmi_device       *bdev;		      // BMI device
  struct cdev		   cdev;		      // control device
  struct device 	  *class_dev;		      // control class device
  int		 	   open_flag;                 // single open flag
  char	  		   int_name[20];              // interrupt name

  struct i2c_client       *dvi_controller;
  struct i2c_client       *vga_controller;
  struct i2c_client       *ddc_eeprom;
  struct i2c_client       *pca9554;
  struct i2c_client       *pca9543;

  int                      vmode;
  int                      active_width;
  int                      active_height;
};

struct bmi_video bmi_video;
static int major;

static void init_dvi(struct bmi_video *video);
static void resume_dvi(struct bmi_video *video);
static void suspend_dvi(struct bmi_video *video);
static void init_vga(struct bmi_video *video);
static void resume_vga(struct bmi_video *video);
static void suspend_vga(struct bmi_video *video);
static void displays_suspend(struct bmi_video *video);

static int i2c_read(struct i2c_client *client, u8 reg);
static int i2c_write(struct i2c_client *client, u8 reg, u8 value);

/*
 *      sysfs interface
 */

/* vmode */
static ssize_t bmi_video_vmode_show(struct device *dev, 
				    struct device_attribute *attr, char *buf)
{
    struct bmi_video *video = dev_get_drvdata(dev);
    int len = 0;

    if (video->vmode == DVI_ON)
            len += sprintf(buf+len, "dvi\n");
    else if (video->vmode == DVI_SUSPEND)
            len += sprintf(buf+len, "dvi off\n");
    else if (video->vmode == VGA_ON)
            len += sprintf(buf+len, "vga\n");
    else if (video->vmode == VGA_SUSPEND)
            len += sprintf(buf+len, "vga off\n");

    return len;
}
	
static ssize_t bmi_video_vmode_store(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf, size_t len)
{
    struct bmi_video *video = dev_get_drvdata(dev);

    if (strnicmp(buf, "dvi", 3) == 0){
        if (video->vmode == DVI_SUSPEND)
	    resume_dvi(video);
        else
 	    init_dvi(video);
    }
    else if (strnicmp(buf, "vga", 3) == 0){
        if (video->vmode == VGA_SUSPEND)
	    resume_vga(video);
        else
 	    init_vga(video);
    }
    else if (strnicmp(buf, "off", 3) == 0){
        displays_suspend(video);
    }

    return len;
}

static DEVICE_ATTR(vmode, 0664, bmi_video_vmode_show, bmi_video_vmode_store);

/*gled*/
static ssize_t bmi_video_gled_show(struct device *dev, 
				    struct device_attribute *attr, char *buf)
{
    u8 value;
    struct bmi_video *video = dev_get_drvdata(dev);
    value = (i2c_read(video->pca9554, 0x01) & 0x04);

    if (value > 0)
            value = 0;
    else
            value = 1;

    return snprintf(buf, PAGE_SIZE, "%d\n", value);
}
	
static ssize_t bmi_video_gled_store(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf, size_t len)
{
    u8 value;
    struct bmi_video *video = dev_get_drvdata(dev);

    if (strchr(buf, '1') != NULL) {
            value = (i2c_read(video->pca9554, 0x01) & ~(0x04));
	    i2c_write(video->pca9554, 0x01, value);
    }
    else if (strchr(buf, '0') != NULL){
            value = (i2c_read(video->pca9554, 0x01) | 0x04);
	    i2c_write(video->pca9554, 0x01, value);
    }
    return len;
}

static DEVICE_ATTR(gled, 0664, bmi_video_gled_show, bmi_video_gled_store);

/*rled*/
static ssize_t bmi_video_rled_show(struct device *dev, 
				    struct device_attribute *attr, char *buf)
{
    u8 value;
    struct bmi_video *video = dev_get_drvdata(dev);
    value = (i2c_read(video->pca9554, 0x01) & 0x08);

    if (value > 0)
            value = 0;
    else
            value = 1;

    return snprintf(buf, PAGE_SIZE, "%d\n", value);
}
	
static ssize_t bmi_video_rled_store(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf, size_t len)
{
    u8 value;
    struct bmi_video *video = dev_get_drvdata(dev);

    if (strchr(buf, '1') != NULL) {
            value = (i2c_read(video->pca9554, 0x01) & ~(0x08));
	    i2c_write(video->pca9554, 0x01, value);
    }
    else if (strchr(buf, '0') != NULL){
            value = (i2c_read(video->pca9554, 0x01) | 0x08);
	    i2c_write(video->pca9554, 0x01, value);
    }
    return len;
}

static DEVICE_ATTR(rled, 0664, bmi_video_rled_show, bmi_video_rled_store);

static struct attribute *bmi_video_attributes[] = {
	&dev_attr_vmode.attr,
	&dev_attr_gled.attr,
	&dev_attr_rled.attr,
	NULL
};

static const struct attribute_group bmi_video_attr_group = {
	.attrs = bmi_video_attributes,
};


/*
 * 	BMI set up
 */

// BMI device ID table
static struct bmi_device_id bmi_video_tbl[] = 
{ 
	{ 
		.match_flags      = BMI_DEVICE_ID_MATCH_VENDOR | BMI_DEVICE_ID_MATCH_PRODUCT, 
		.vendor           = BMI_VENDOR_BUG_LABS, 
		.product          = BMI_PRODUCT_VIDEO, 
		.revision         = BMI_ANY, 
	}, 
	{ 0, },	  /* terminate list */
};
MODULE_DEVICE_TABLE(bmi, bmi_video_tbl);

int	bmi_video_probe (struct bmi_device *bdev);
void	bmi_video_remove (struct bmi_device *bdev);
int	bmi_video_resume (struct device *dev);
int	bmi_video_suspend (struct device *dev);

static struct dev_pm_ops bmi_video_pm =
{
	.resume = bmi_video_resume,
	.suspend = bmi_video_suspend,
};

// BMI driver structure
static struct bmi_driver bmi_video_driver = 
{
	.name       = "bmi_video", 
	.id_table   = bmi_video_tbl, 
	.probe      = bmi_video_probe, 
	.remove     = bmi_video_remove, 
	.pm         = &bmi_video_pm,
};

/*
 *	PIM functions
 */

// interrupt handler
//static irqreturn_t module_irq_handler(int irq, void *dummy)
//{
//	return IRQ_HANDLED;
//}

/*
 * 	BMI functions
 */

// probe - insert PIM
int bmi_video_probe(struct bmi_device *bdev)
{
        int err, slot, irq;
	dev_t dev_id;
	u8 value;

	struct bmi_video *video;
	struct class *bmi_class;
       	struct i2c_adapter *adap;
	struct omap_dss_device *dssdev;
	
	printk (KERN_INFO "bmi_video: probe...\n");	

	err = 0;
	slot = bdev->slot->slotnum;
      	adap = bdev->slot->adap;
	video = &bmi_video;

	video->bdev = 0;
	video->open_flag = 0;

	dssdev = NULL;
	dvi_disp = NULL;
	vga_disp = NULL;

        for_each_dss_dev(dssdev) {
		omap_dss_get_device(dssdev);

		if (dssdev->state)
		        dssdev->disable(dssdev);
		
		if (strnicmp(dssdev->name, "dvi", 3) == 0)
		        dvi_disp = dssdev;
		else if (strnicmp(dssdev->name, "vga", 3) == 0)
		        vga_disp = dssdev;
	}

	// bind driver and bmi_device
	video->bdev = bdev;

	// create 1 minor device
	dev_id = MKDEV(major, slot);

	// create class device
	bmi_class = bmi_get_class ();    

	// bind driver and bmi_device 
	video->bdev = bdev;
	
	err = sysfs_create_group(&bdev->dev.kobj, &bmi_video_attr_group);
	if (err < 0)
	        printk(KERN_ERR "Error creating SYSFS entries...\n");

	// register i2c i/o expander
	video->pca9554 = i2c_new_device(adap, &vid_io_expander_info);
	if (video->pca9554 == NULL) {
		printk(KERN_ERR "PCA9554 NULL...\n");
		goto err1;
	}

	// initialize expander
	value = 0x00;
	i2c_write(video->pca9554, 0x03, 0x00);                 // all outputs
	i2c_write(video->pca9554, 0x01, value);                // all low
	mdelay(500);
	value |= 0x0F;
	i2c_write(video->pca9554, 0x01, value);

	// register i2c switch
	video->pca9543 = i2c_new_device(adap, &vid_i2c_switch_info);
	if (video->pca9543 == NULL) {
		printk(KERN_ERR "PCA9543 NULL...\n");
		goto err1;
	}

	// disable both pca9543 outputs
	i2c_write(video->pca9543, 0x00, 0x00);

	// TODO: check for monitors
	//i2c_write(video->pca9543, 0x00, 0x01);   //point switch to dvi DDC
	// interrogate ddc eeprom
	//i2c_write(video->pca9543, 0x00, 0x02);   //point switch to vga DDC
	// interrogate ddc eeprom
	//i2c_write(video->pca9543, 0x00, 0x00);

	// bring tfp out of reset
	value = i2c_read(video->pca9554, 0x01);
	value |= (1 << 0);
	i2c_write(video->pca9554, 0x01, value);
	mdelay(5);

	// register dvi controller
	video->dvi_controller = i2c_new_device(adap, &tfp_info);
	if (video->dvi_controller == NULL) {
	        printk(KERN_ERR "TFP NULL...\n");
	        goto err1;
	}

	// bring ths out of reset
	gpio_direction_output(10, 0);
	gpio_set_value (10, 1);

	// register vga controller
	video->vga_controller = i2c_new_device(adap, &ths_info);
	if (video->vga_controller == NULL) {
		printk(KERN_ERR "THS NULL...\n");
		goto err1;
	}
	
	// request PIM interrupt
	irq = bdev->slot->status_irq;
	sprintf (video->int_name, "bmi_video%d", slot);

	bmi_device_set_drvdata (bdev, video);

	//default video mode: DVI
	init_dvi(video);

	return 0;

 err1: 
	video->class_dev = 0;

	device_destroy (bmi_class, MKDEV(major, slot));
	bmi_device_set_drvdata (bdev, 0);
	video->bdev = 0;

	if (video->pca9554 != NULL)
	        i2c_unregister_device(video->pca9554);
	if (video->dvi_controller != NULL)
	        i2c_unregister_device(video->dvi_controller);
	if (video->vga_controller != NULL)
	        i2c_unregister_device(video->vga_controller);
	if (video->pca9543 != NULL)
	        i2c_unregister_device(video->pca9543);
	if (video->ddc_eeprom != NULL)
	        i2c_unregister_device(video->ddc_eeprom);

	return -ENODEV;
}

// remove PIM
void bmi_video_remove(struct bmi_device *bdev)
{	
	int irq;
	int i;
	int slot;

	struct bmi_video *video;
	struct class *bmi_class;

	printk(KERN_INFO "bmi_video: Module Removed...\n");

	slot = bdev->slot->slotnum;
	video = &bmi_video;

	// disable dss
	if (dvi_disp->state)
	        dvi_disp->disable(dvi_disp);
	if (vga_disp->state)
	        vga_disp->disable(vga_disp);
	
	if (video->pca9554 != NULL)
	        i2c_unregister_device(video->pca9554);
	if (video->dvi_controller != NULL)
	        i2c_unregister_device(video->dvi_controller);
	if (video->vga_controller != NULL)
	        i2c_unregister_device(video->vga_controller);
	if (video->pca9543 != NULL)
	        i2c_unregister_device(video->pca9543);
	if (video->ddc_eeprom != NULL)
	        i2c_unregister_device(video->ddc_eeprom);

	irq = bdev->slot->status_irq;

	for (i = 0; i < 4; i++)
	  bmi_slot_gpio_direction_in(slot, i);

	sysfs_remove_group(&bdev->dev.kobj, &bmi_video_attr_group);

	bmi_class = bmi_get_class ();
	device_destroy (bmi_class, MKDEV(major, slot));

	video->class_dev = 0;

	// de-attach driver-specific struct from bmi_device structure 
	bmi_device_set_drvdata (bdev, 0);
	video->bdev = 0;

	return;
}


/*
 *	PM routines
 */

int bmi_video_resume(struct device *dev)
{
	struct bmi_device *bmi_dev;
	struct bmi_video *video;

	bmi_dev = to_bmi_device(dev);
	video = dev_get_drvdata(dev);

	printk(KERN_INFO "bmi_video: resume...\n");

	video->dvi_controller->dev.bus->resume(&video->dvi_controller->dev);
	video->vga_controller->dev.bus->resume(&video->vga_controller->dev);
	video->pca9554->dev.bus->resume(&video->pca9554->dev);
	video->pca9543->dev.bus->resume(&video->pca9543->dev);

	if (video->vmode == DVI_SUSPEND)
	  init_dvi(video);
	else if (video->vmode == VGA_SUSPEND)
	  init_vga(video);

	//TODO: Remember "off" state

	return 0;
}

int bmi_video_suspend(struct device *dev)
{
	struct bmi_device *bmi_dev;
	struct bmi_video *video;

	bmi_dev = to_bmi_device(dev);
	video = dev_get_drvdata(dev);

	printk(KERN_INFO "bmi_video: suspend...\n");
	
	video->dvi_controller->dev.bus->suspend(&video->dvi_controller->dev, PMSG_SUSPEND);
	video->vga_controller->dev.bus->suspend(&video->vga_controller->dev, PMSG_SUSPEND);
	video->pca9554->dev.bus->suspend(&video->pca9554->dev, PMSG_SUSPEND);
	video->pca9543->dev.bus->suspend(&video->pca9543->dev, PMSG_SUSPEND);

	displays_suspend(video);

	bmi_slot_spi_disable(bmi_dev->slot->slotnum);

	return 0;
}

/*
 *	module routines
 */

static void init_vga(struct bmi_video *video)
{
        int err;
        struct fb_info *info;
	struct fb_var_screeninfo var;

	printk (KERN_INFO "bmi_video: initializing VGA output...\n");

	// disable dvi (tfp)
	suspend_dvi(video);

	// disable dvi (dss)
	if (dvi_disp->state)
	    dvi_disp->disable(dvi_disp);

	// setup omapfb
	info = registered_fb[0];
	var = info->var;

	var.xres = video->active_width = 1024;
	var.yres = video->active_height = 768;
	var.xres_virtual = 1024;
	var.yres_virtual = 768;
	var.activate = 128;             //Force update	  

        err = fb_set_var(info, &var);
	if (err)
	        printk(KERN_ERR "bmi_video: init_vga: error resizing omapfb\n");

	// enable vga (dss)
	if (vga_disp->state != 1)
	        vga_disp->enable(vga_disp);

	// issue hardware reset (ths)
	gpio_set_value (10, 0);
	mdelay(1);
	gpio_set_value (10, 1);

	//init vga (ths)
	ths8200_init(video->vga_controller);

        video->vmode = VGA_ON;
}

static void resume_vga(struct bmi_video *video)
{
	ths8200_enable(video->vga_controller);
	video->vmode = VGA_ON;
}

static void suspend_vga(struct bmi_video *video)
{
	ths8200_disable(video->vga_controller);
	video->vmode = VGA_SUSPEND;
}

static void init_dvi(struct bmi_video *video)
{
        int err;
	u8 value;
	struct fb_info *info;
	struct fb_var_screeninfo var;
	//struct omap_video_timings dvi_timings;

       	printk (KERN_INFO "bmi_video: initializing DVI output...\n");

	// disable vga (ths)
	suspend_vga(video);

	// disable vga (dss)
	if (vga_disp->state)
	    vga_disp->disable(vga_disp);

	// setup omapfb
	info = registered_fb[0];
	var = info->var;

	var.xres = video->active_width = 1024;
	var.yres = video->active_height = 768;
	var.xres_virtual = 1024;
	var.yres_virtual = 768;
	var.activate = 128;             //Force update	  
	
        err = fb_set_var(info, &var);
	if (err)
	        printk(KERN_ERR "bmi_video: init_dvi: error resizing omapfb\n");

	// set dss to agree with omapfb
	//dvi_disp->panel.timings = dvi_timings;
	
	// enable dvi (dss)
	if (dvi_disp->state != 1)
	        dvi_disp->enable(dvi_disp);

	// issue hardware reset (tfp)
	value = i2c_read(video->pca9554, 0x01);
	value &= ~(1 << 0);
	i2c_write(video->pca9554, 0x01, value);	
	mdelay(5);
	value |= (1 << 0);
	i2c_write(video->pca9554, 0x01, value);
	mdelay(5);

	// init dvi (tfp)
	tfp410_init(video->dvi_controller);

        video->vmode = DVI_ON;
}

static void resume_dvi(struct bmi_video *video)
{
        u8 value;
        // remove tfp from reset
        value = i2c_read(video->pca9554, 0x01);
	value |= (1 << 0);
	i2c_write(video->pca9554, 0x01, value);	
	mdelay(5);
	tfp410_enable(video->dvi_controller);
	video->vmode = DVI_ON;
}

static void suspend_dvi(struct bmi_video *video)
{
        u8 value;
	tfp410_disable(video->dvi_controller);
	// place tfp into reset
	mdelay(5);
	value = i2c_read(video->pca9554, 0x01);
	value &= ~(1 << 0);
	i2c_write(video->pca9554, 0x01, value);	
	video->vmode = DVI_SUSPEND;
}

//puts controllers into reset
static void displays_suspend(struct bmi_video *video)
{
	if (video->vmode == DVI_SUSPEND || video->vmode == VGA_SUSPEND) {
	        printk(KERN_INFO "bmi_video: video output is off...\n");
	        return;
	}
	printk (KERN_INFO "bmi_video: turning video output off...\n");

	//disable displays
	if (video->vmode == DVI_ON)
	        suspend_dvi(video);
	else if (video->vmode == VGA_ON)
	        suspend_vga(video);
}

static int i2c_read(struct i2c_client *client, u8 reg)
{
        return i2c_smbus_read_byte_data(client, reg);
}

static int i2c_write(struct i2c_client *client, u8 reg, u8 value)
{
        return i2c_smbus_write_byte_data(client, reg, value);
}

static void __exit bmi_video_cleanup(void)
{
	dev_t dev_id;

	bmi_unregister_driver (&bmi_video_driver);

	dev_id = MKDEV(major, 0);
	unregister_chrdev_region (dev_id, 4);
	return;
}

static int __init bmi_video_init(void)
{
	dev_t	dev_id;
	int	retval;

	// alloc char driver with 4 minor numbers
	retval = alloc_chrdev_region (&dev_id, 0, 4, "BMI VIDEO Driver"); 
	if (retval) {
		return -ENODEV;
	}

	major = MAJOR(dev_id);
	retval = bmi_register_driver (&bmi_video_driver);   
	if (retval) {
		unregister_chrdev_region(dev_id, 4);
		return -ENODEV;  
	}

	printk (KERN_INFO "bmi_video: BMI_VIDEO Driver...\n");

	return 0;
}


module_init(bmi_video_init);
module_exit(bmi_video_cleanup);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Buglabs Inc.");
MODULE_DESCRIPTION("BMI video module device driver");
