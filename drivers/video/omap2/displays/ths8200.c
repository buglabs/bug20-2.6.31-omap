/*
 * 	ths8200.c
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <mach/gpio.h>
#include <linux/i2c.h>
#include "ths8200.h"

/*
 * 	THS8200 init sequence
 */

u8 ths8200_init_seq [][2] = {
    {0x03, 0xC1},     //chip_ctl           
    {0x19, 0x03},     //csc_offset3  CSC bypassed
    {0x1D, 0x00},     //dtg_y_sync1        
    {0x1E, 0x00},     //dtg_y_sync2        
    {0x1F, 0x00},     //dtg_y_sync3        
    {0x20, 0x00},     //dtg_cbcr_sync1     
    {0x21, 0x00},     //dtg_cbcr_sync2     
    {0x22, 0x00},     //dtg_cbcr_sync3     
    {0x23, 0x00},     //dtg_y_sync_upper   
    {0x24, 0x00},     //dtg_cbcr_sync_upper    
    {0x1C, 0x70},     //dman_cntl   30bit 4:4:4 input  
     
    /*
    //   800x600   //
    {0x34, 0x04},     //dtg_total_pixel_msb 1056 pixels per line
    {0x35, 0x20},     //dtg_total_pixel_lsb
    {0x39, 0x22},     //dtg_frame_field_msb 628 line per frame/field
    {0x3A, 0x74},     //dtg_frame_size_lsb
    */

    //   1024x768   //
    {0x34, 0x05},     //dtg_total_pixel_msb 1056 pixels per line
    {0x35, 0x40},     //dtg_total_pixel_lsb
    {0x39, 0x30},     //dtg_frame_field_msb 628 line per frame/field
    {0x3A, 0x26},     //dtg_frame_size_lsb

    
    {0x36, 0x80},     //dtg_linecnt_msb    
    {0x37, 0x01},     //dtg_linecnt_lsb    
    {0x38, 0x87},     //dtg_mode      VESA slave    
    {0x3B, 0x74},     //dtg_field_size_lsb 
    {0x4A, 0x8C},     //csm_mult_gy_msb    
    {0x4B, 0x44},     //csm_mult_bcb_rcr_msb
    {0x4C, 0x00},     //csm_mult_gy_lsb    
    {0x4D, 0x00},     //csm_mult_bcb_lsb   
    {0x4E, 0x00},     //csm_mult_rcr_lsb   
    {0x4F, 0xC0},     //csm_mode           
    {0x70, 0x80},     //dtg_hlength_lsb   HSOUT, 128 pixels 
    {0x71, 0x00},     //dtg_hdly_msb       
    {0x72, 0x01},     //dtg_hdly_lsb       
    {0x73, 0x05},     //dtg_vlength_lsb  VSOUT=4 lines ( use 1 more than desired width)  
    {0x74, 0x00},     //dtg_vdly_msb       
    {0x75, 0x01},     //dtg_vdly_lsb       
    {0x76, 0x00},     //dtg_vlength2_lsb   
    {0x77, 0x07},     //dtg_vdly2_msb      
    {0x78, 0xFF},     //dtg_vdly2_lsb      
    {0x79, 0x00},     //dtg_hs_in_dly_msb  
    {0x7A, 0x00},     //dtg_hs_in_dly_lsb , use to adjust horizontal alignment
    {0x7B, 0x00},     //dtg_vs_in_dly_msb  
    {0x7C, 0x00},     //dtg_vs_in_dly_lsb  
    {0x82, 0xDB},     //pol_cntl  HSync/VSync polarities, DE enabled in VESA mode, set to 0x5B  to disable DE 
    
    //colorspace conversion off
    /*
      {0x04, 0x00}, {0x05, 0x00}, {0x06, 0x00}, {0x07, 0x00}, {0x08, 0x00},
      {0x09, 0x00}, {0x0a, 0x00}, {0x0b, 0x00}, {0x0c, 0x00}, {0x0d, 0x00},
      {0x0e, 0x00}, {0x0f, 0x00}, {0x10, 0x00}, {0x11, 0x00}, {0x12, 0x00},
      {0x13, 0x00}, {0x14, 0x00}, {0x15, 0x00}, {0x16, 0x00}, {0x17, 0x00},
      {0x18, 0x00}, {0x19, 0x00},
    */

    //color bars
    //{0x03, 0xa3},
};

static inline int ths8200_read(struct i2c_client *client, u8 reg)
{
    return i2c_smbus_read_byte_data(client, reg);
}

static inline int ths8200_write(struct i2c_client *client, u8 reg, u8 value)
{
    return i2c_smbus_write_byte_data(client, reg, value);
}

int ths8200_enable(struct i2c_client *client)
{
    //ths8200_enable should only be used following ths8200_disable if THS has been initialized
    int err = 0;

    //clear chip_pwdn bit (exit PD mode)
    err |= ths8200_write(client, 0x03, 0x01);
    if (err < 0) {
        dev_err(&client->dev, "%s: Unable to communicate with THS8200...\n", __func__);
	return -EINVAL;
    }

    mdelay(1);
    return 0;
}
EXPORT_SYMBOL(ths8200_enable);

int ths8200_test(struct i2c_client *client, u8 reg, u8 value)
{
    int err;
    err |= ths8200_write(client, reg, value);
    if (err < 0) {
        dev_err(&client->dev, "%s: Write Error...\n", __func__);
	return -EINVAL;
    }
}
EXPORT_SYMBOL(ths8200_test);

int ths8200_disable(struct i2c_client *client)
{
    int err = 0;

    //attempt graceful powerdown: set chip_pwdn bit (enter PD mode)
    err |= ths8200_write(client, 0x03, 0x04);
    if (err < 0) {
        dev_err(&client->dev, "%s: Unable to place THS8200 into power down state...\n", __func__);
	return -EINVAL;
    }

    mdelay(1);
    return 0;
}
EXPORT_SYMBOL(ths8200_disable);

int ths8200_init(struct i2c_client *client)
{
    int i;
    int err = 0;

    //execute init sequence
    for (i = 0; i < (sizeof(ths8200_init_seq)/2); i++)
        {
	    err |= ths8200_write(client,
	        ths8200_init_seq[i][0],
		ths8200_init_seq[i][1]);	     
	}
		
        if (err < 0) {
	        dev_err(&client->dev, "%s: Error during THS8200 init\n", __func__);
		return -EINVAL;
        }

    return 0;
}
EXPORT_SYMBOL(ths8200_init);

static int ths8200_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
        
    return 0;
}

static int ths8200_remove(struct i2c_client *client)
{

    return 0;
}

static const struct i2c_device_id ths8200_id[] = {
    {"ths8200", 0},
    {},
};

MODULE_DEVICE_TABLE(i2c, ths8200_id);

static struct i2c_driver ths8200_driver = {
    .driver = {
	.owner	= THIS_MODULE,
	.name	= "ths8200",
    },
	.probe		= ths8200_probe,
	.remove		= ths8200_remove,
	.id_table	= ths8200_id,
};

static __init int init_ths8200(void)
{
    return i2c_add_driver(&ths8200_driver);
}

static __exit void exit_ths8200(void)
{
    i2c_del_driver(&ths8200_driver);
}

module_init(init_ths8200);
module_exit(exit_ths8200);

MODULE_LICENSE("GPL");
