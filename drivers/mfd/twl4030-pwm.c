/*
 * simple driver for PWM (Pulse Width Modulator) controller
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Derived from pxa PWM driver by eric miao <eric.miao@marvell.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/pwm.h>
#include <linux/i2c/twl4030.h>
#include <mach/hardware.h>



#define PWM_LENGTHB	0x1 << 7
#define PWM_LENGTHA	0x1 << 6
#define LEDBPWM		0x1 << 5
#define LEDAPWM		0x1 << 4
#define LEDBEXT		0x1 << 3
#define LEDAEXT		0x1 << 2
#define LEDBON		0x1 << 1
#define LEDAON		0x1 << 0

#define PWMON		0x0
#define PWMOFF		0x1
#define LED_EN		0X0

//Contants for PWM generator.
#define COUNT_LENGTH	128
#define	CLOCK_TIME		330


struct pwm_device {
	struct list_head	node;
	struct platform_device *pdev;

	const char	*label;
	unsigned int	use_count;
	unsigned int	pwm_id;
};

int pwm_config(struct pwm_device *pwm, int duty_ns, int period_ns)
{
  u8 dat_on = 0;
  u8 dat_off = 0;
  
  if (pwm == NULL || period_ns == 0 || duty_ns > period_ns)
    return -EINVAL;

  dat_on = 0x7f & (duty_ns);
  dat_off = 0x7f & (period_ns - duty_ns) * COUNT_LENGTH * CLOCK_TIME;

  switch(pwm->pwm_id) {
  case 0:
    twl4030_i2c_write_u8(TWL4030_MODULE_PWMA, dat_off, PWMON);
    twl4030_i2c_write_u8(TWL4030_MODULE_PWMA, dat_on, PWMOFF);
    break;
  case 1:
    twl4030_i2c_write_u8(TWL4030_MODULE_PWMB, dat_off, PWMON);
    twl4030_i2c_write_u8(TWL4030_MODULE_PWMB, dat_on, PWMOFF);
    break;
  default:
    return -EINVAL;
  }  
  return 0;
}

EXPORT_SYMBOL(pwm_config);

int pwm_enable(struct pwm_device *pwm)
{	
  int rc = 0;
  u8 dat = 0;

  rc = twl4030_i2c_read_u8(TWL4030_MODULE_LED, &dat, LED_EN);

  switch(pwm->pwm_id) {
  case 0: 
    dat |= LEDAPWM | LEDAON;
    break;
  case 1:
    dat |= LEDBPWM | LEDBON;
    break;
  default:
    return -EINVAL;
  }
    rc = twl4030_i2c_write_u8(TWL4030_MODULE_LED, dat, LED_EN);
    return rc;
}
EXPORT_SYMBOL(pwm_enable);

void pwm_disable(struct pwm_device *pwm)
{
   int rc = 0;
  u8 dat = 0;

  rc = twl4030_i2c_read_u8(TWL4030_MODULE_LED, &dat, LED_EN);
  
  switch(pwm->pwm_id) {
  case 0: 
    dat &= !(LEDAPWM | LEDAON);
    break;
  case 1:
    dat &= !(LEDBPWM | LEDBON);
    break;
  default:
    return;
  }
  rc = twl4030_i2c_write_u8(TWL4030_MODULE_LED, dat, LED_EN);
  return;

}
EXPORT_SYMBOL(pwm_disable);

static DEFINE_MUTEX(pwm_lock);
static LIST_HEAD(pwm_list);

struct pwm_device *pwm_request(int pwm_id, const char *label)
{
	struct pwm_device *pwm;
	int found = 0;

	mutex_lock(&pwm_lock);

	list_for_each_entry(pwm, &pwm_list, node) {
		if (pwm->pwm_id == pwm_id) {
			found = 1;
			break;
		}
	}

	if (found) {
		if (pwm->use_count == 0) {
			pwm->use_count++;
			pwm->label = label;
		} else
			pwm = ERR_PTR(-EBUSY);
	} else
		pwm = ERR_PTR(-ENOENT);

	mutex_unlock(&pwm_lock);
	return pwm;
}
EXPORT_SYMBOL(pwm_request);

void pwm_free(struct pwm_device *pwm)
{
	mutex_lock(&pwm_lock);

	if (pwm->use_count) {
		pwm->use_count--;
		pwm->label = NULL;
	} else
		pr_warning("PWM device already freed\n");

	mutex_unlock(&pwm_lock);
}
EXPORT_SYMBOL(pwm_free);

static int __devinit twl4030_pwm_probe(struct platform_device *pdev)
{
	struct pwm_device *pwm;
	struct resource *r;
	int ret = 0;

	pwm = kzalloc(sizeof(struct pwm_device), GFP_KERNEL);
	if (pwm == NULL) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}


	pwm->use_count = 0;
	pwm->pwm_id = pdev->id;
	pwm->pdev = pdev;


	mutex_lock(&pwm_lock);
	list_add_tail(&pwm->node, &pwm_list);
	mutex_unlock(&pwm_lock);

	platform_set_drvdata(pdev, pwm);
	return 0;

	kfree(pwm);
	return ret;
}

static int __devexit twl4030_pwm_remove(struct platform_device *pdev)
{
	struct pwm_device *pwm;
	struct resource *r;

	pwm = platform_get_drvdata(pdev);
	if (pwm == NULL)
		return -ENODEV;

	mutex_lock(&pwm_lock);
	list_del(&pwm->node);
	mutex_unlock(&pwm_lock);

	kfree(pwm);
	return 0;
}

static struct platform_driver twl4030_pwm_driver = {
	.driver		= {
		.name	= "twl4030_pwm",
	},
	.probe		= twl4030_pwm_probe,
	.remove		= __devexit_p(twl4030_pwm_remove),
};

static int __init twl4030_pwm_init(void)
{
	return platform_driver_register(&twl4030_pwm_driver);
}
arch_initcall(twl4030_pwm_init);

static void __exit twl4030_pwm_exit(void)
{
	platform_driver_unregister(&twl4030_pwm_driver);
}
module_exit(twl4030_pwm_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Izzy <izzy@buglabs.net>");
