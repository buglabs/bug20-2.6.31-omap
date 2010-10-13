/*
 * File:         include/linux/bmi/bmi_video.h
 *
 * 		This is the application header file for the BMI video module on the Bug2.0 platform.
 */

/* vmodes */
#define DVI_ON           0
#define DVI_SUSPEND      1
#define VGA_ON           2
#define VGA_SUSPEND      3

/* i2c Devices */

// dvi controller
static struct i2c_board_info tfp_info = {
  I2C_BOARD_INFO("tfp410p", 0x38),
};

// vga controller
static struct i2c_board_info ths_info = {
  I2C_BOARD_INFO("ths8200", 0x20),
};

// expander (pca9554)
static struct i2c_board_info vid_io_expander_info = {
  I2C_BOARD_INFO("vid_io_expander", 0x21),
};

// switch (pca9543)
static struct i2c_board_info vid_i2c_switch_info = {
  I2C_BOARD_INFO("vid_i2c_switch", 0x71),
};

