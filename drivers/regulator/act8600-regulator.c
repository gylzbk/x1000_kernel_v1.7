/*
 * act8600-regulator.c - regulator driver for ACT8600
 *
 * Copyright (C) 2012 Ingenic Semiconductor Co., Ltd.
 * Written by Large Dipper <ykli@ingenic.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/mfd/act8600-private.h>
#include <linux/mfd/pmu-common.h>

#define VSEL_MASK	0x3f
#define REG4_VSET	0x40
#define REG4_VCON	0x41
#define REG4_VOL_VALUE	0x57

#define get_vset_reg(id)	(id * 0x10)
#define get_dcdc_vcon_reg(id)	(id * 0x10 + 2)
#define get_ldo_vcon_reg(id)	(id * 0x10 + 1)

struct act8600_reg {
	struct device		*dev;
	struct act8600		*iodev;
	struct regulator_dev	**rdev;
};

enum {
	ACT8600_OUT1,
	ACT8600_OUT2,
	ACT8600_OUT3,
	ACT8600_OUT4,
	ACT8600_OUT5,
	ACT8600_OUT6,
	ACT8600_OUT7,
	ACT8600_OUT8,
};

static const int act8600_voltage_map[VSEL_MASK + 1][2] = {
	{600000, 0},	{625000, 1},	{650000, 2},	{675000, 3},
	{700000, 4},	{725000, 5},	{750000, 6},	{775000, 7},
	{800000, 8},	{825000, 9},	{850000, 10},	{875000, 11},
	{900000, 12},	{925000, 13},	{950000, 14},	{975000, 15},
	{1000000, 16},	{1025000, 17},	{1050000, 18},	{1075000, 19},
	{1100000, 20},	{1125000, 21},	{1150000, 22},	{1175000, 23},
	{1200000, 24},	{1250000, 25},	{1300000, 26},	{1350000, 27},
	{1400000, 28},	{1450000, 29},	{1500000, 30},	{1550000, 31},
	{1600000, 32},	{1650000, 33},	{1700000, 34},	{1750000, 35},
	{1800000, 36},	{1850000, 37},	{1900000, 38},	{1950000, 39},
	{2000000, 40},	{2050000, 41},	{2100000, 42},	{2150000, 43},
	{2200000, 44},	{2250000, 45},	{2300000, 46},	{2350000, 47},
	{2400000, 48},	{2500000, 49},	{2600000, 50},	{2700000, 51},
	{2800000, 52},	{2900000, 53},	{3000000, 54},	{3100000, 55},
	{3200000, 56},	{3300000, 57},	{3400000, 58},	{3500000, 59},
	{3600000, 60},	{3700000, 61},	{3800000, 62},	{3900000, 63},
};

static int voltages_to_value(int min_uV, int max_uV, unsigned *selector)
{
	unsigned char i;

	for (i = 0; i <= VSEL_MASK; i++) {
		if ((act8600_voltage_map[i][1] >= min_uV)
		    && (act8600_voltage_map[i][1] <= max_uV)) {

			*selector = i;
			return act8600_voltage_map[i][2];
		}
	}

	return -1;
}

static int value_to_voltage(int value)
{
	unsigned char i;

	for (i = 0; i <= VSEL_MASK; i++) {
		if (value == act8600_voltage_map[i][2])
			return act8600_voltage_map[i][1];
	}

	return -1;
}

static int act8600_dcdc_is_enabled(struct regulator_dev *rdev)
{
	struct act8600_reg *act8600_reg = rdev_get_drvdata(rdev);
	struct i2c_client *client = act8600_reg->iodev->client;
	unsigned char value;
	unsigned char reg = get_dcdc_vcon_reg(rdev_get_id(rdev));

	if (act8600_read_reg(client, reg, &value) < 0)
		return -1;

	return value & VCON_ON;
}

static int act8600_dcdc_enable(struct regulator_dev *rdev)
{
	struct act8600_reg *act8600_reg = rdev_get_drvdata(rdev);
	struct i2c_client *client = act8600_reg->iodev->client;
	unsigned char value, timeout;
	unsigned char reg = get_dcdc_vcon_reg(rdev_get_id(rdev));

	act8600_read_reg(client, reg, &value);
	value |= VCON_ON;
	act8600_write_reg(client, reg, value);

	for (timeout = 50; timeout >= 0; timeout--) {
		act8600_read_reg(client, reg, &value);

		if (value & VCON_OK)
			return 0;
		else
			dev_warn(act8600_reg->dev,
				 "not stable, wait for 10 ms\n");
		msleep(10);
	}
	dev_err(act8600_reg->dev, "enable failed!\n");

	return -1;
}

static int act8600_dcdc_disable(struct regulator_dev *rdev)
{
	struct act8600_reg *act8600_reg = rdev_get_drvdata(rdev);
	struct i2c_client *client = act8600_reg->iodev->client;
	unsigned char reg = get_dcdc_vcon_reg(rdev_get_id(rdev));
	unsigned char value;

	act8600_read_reg(client, reg, &value);
	value &= ~VCON_ON;
	act8600_write_reg(client, reg, value);

	return 0;
}

static int act8600_ldo_is_enabled(struct regulator_dev *rdev)
{
	struct act8600_reg *act8600_reg = rdev_get_drvdata(rdev);
	struct i2c_client *client = act8600_reg->iodev->client;
	unsigned char value;
	unsigned char reg = get_ldo_vcon_reg(rdev_get_id(rdev));

	if (act8600_read_reg(client, reg, &value) < 0)
		return -1;

	return value & VCON_ON;
}

static int act8600_ldo_enable(struct regulator_dev *rdev)
{
	struct act8600_reg *act8600_reg = rdev_get_drvdata(rdev);
	struct i2c_client *client = act8600_reg->iodev->client;
	unsigned char reg = get_ldo_vcon_reg(rdev_get_id(rdev));
	unsigned char value, timeout;

	act8600_read_reg(client, reg, &value);
	value |= VCON_ON;
	value &= ~VCON_DIS;
	act8600_write_reg(client, reg, value);

	for (timeout = 50; timeout >= 0; timeout--) {
		act8600_read_reg(client, reg, &value);

		if (value & VCON_OK)
			return 0;
		else
			dev_warn(act8600_reg->dev,
				 "not stable, wait for 10 ms\n");
		msleep(10);
	}
	dev_err(act8600_reg->dev, "enable failed!\n");

	return -1;
}

static int act8600_ldo_disable(struct regulator_dev *rdev)
{
	struct act8600_reg *act8600_reg = rdev_get_drvdata(rdev);
	struct i2c_client *client = act8600_reg->iodev->client;
	unsigned char value;
	unsigned char reg = get_ldo_vcon_reg(rdev_get_id(rdev));

	act8600_read_reg(client, reg, &value);
	value &= ~VCON_ON;
	value |= VCON_DIS;
	act8600_write_reg(client, reg, value);

	return 0;
}

static int act8600_otg_is_enabled(struct regulator_dev *rdev)
{
	struct act8600_reg *act8600_reg = rdev_get_drvdata(rdev);
	struct i2c_client *client = act8600_reg->iodev->client;
	unsigned char value;
	unsigned char reg = REG4_VCON;

	if (act8600_read_reg(client, reg, &value) < 0)
		return -1;

	return value & VCON_ON;
}

static int act8600_otg_enable(struct regulator_dev *rdev)
{
	struct act8600_reg *act8600_reg = rdev_get_drvdata(rdev);
	struct i2c_client *client = act8600_reg->iodev->client;
	unsigned char reg = REG4_VSET;
	unsigned char value = REG4_VOL_VALUE;
	unsigned char timeout;

	act8600_write_reg(client, reg, value);

	reg = REG4_VCON;
	act8600_read_reg(client, reg, &value);
	value |= VCON_ON;
	act8600_write_reg(client, reg, value);

	for (timeout = 50; timeout >= 0; timeout--) {
		act8600_read_reg(client, reg, &value);

		if (value & VCON_OK)
			return 0;
		else
			dev_warn(act8600_reg->dev,
				 "not stable, wait for 10 ms\n");
		msleep(10);
	}
	dev_err(act8600_reg->dev, "enable failed!\n");

	return -1;
}

static int act8600_otg_disable(struct regulator_dev *rdev)
{	
	struct act8600_reg *act8600_reg = rdev_get_drvdata(rdev);
	struct i2c_client *client = act8600_reg->iodev->client;
	unsigned char reg = REG4_VCON;
	unsigned char value;

	act8600_read_reg(client, reg, &value);
	value &= ~VCON_ON;
	act8600_write_reg(client, reg, value);

	return 0;
}

static int act8600_list_voltage(struct regulator_dev *rdev,
				unsigned int selector)
{
	if (selector > VSEL_MASK)
		return -EINVAL;
	
	return act8600_voltage_map[selector][1];
}

static int act8600_get_voltage(struct regulator_dev *rdev)
{
	struct act8600_reg *act8600_reg = rdev_get_drvdata(rdev);
	struct i2c_client *client = act8600_reg->iodev->client;
	unsigned char value;
	unsigned char reg = get_vset_reg(rdev_get_id(rdev));

	act8600_read_reg(client, reg, &value);

	return value_to_voltage(value);
}

static int act8600_set_voltage(struct regulator_dev *rdev,
			       int min_uV, int max_uV, unsigned *selector)
{
	struct act8600_reg *act8600_reg = rdev_get_drvdata(rdev);
	struct i2c_client *client = act8600_reg->iodev->client;
	unsigned char value;
	unsigned char reg = get_vset_reg(rdev_get_id(rdev));

	value = voltages_to_value(min_uV, max_uV, selector);
	act8600_write_reg(client, reg, value);

	return value_to_voltage(value);
}

static struct regulator_ops act8600_dcdc_ops = {
	.is_enabled = act8600_dcdc_is_enabled,
	.enable = act8600_dcdc_enable,
	.disable = act8600_dcdc_disable,
	.list_voltage = act8600_list_voltage,
	.get_voltage = act8600_get_voltage,
	.set_voltage = act8600_set_voltage,
};

static struct regulator_ops act8600_ldo_ops = {
	.is_enabled = act8600_ldo_is_enabled,
	.enable = act8600_ldo_enable,
	.disable = act8600_ldo_disable,
	.list_voltage = act8600_list_voltage,
	.get_voltage = act8600_get_voltage,
	.set_voltage = act8600_set_voltage,
};

static struct regulator_ops act8600_otg_ops = {
	.is_enabled = act8600_otg_is_enabled,
	.enable = act8600_otg_enable,
	.disable = act8600_otg_disable,
};

static struct regulator_desc act8600_regulator[] = {
	{
		.name = "OUT1",
		.id = ACT8600_OUT1,
		.ops = &act8600_dcdc_ops,
		.n_voltages = VSEL_MASK,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "OUT2",
		.id = ACT8600_OUT2,
		.ops = &act8600_dcdc_ops,
		.n_voltages = VSEL_MASK,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "OUT3",
		.id = ACT8600_OUT3,
		.ops = &act8600_dcdc_ops,
		.n_voltages = VSEL_MASK,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "OUT4",
		.id = ACT8600_OUT4,
		.ops = &act8600_otg_ops,
		.n_voltages = VSEL_MASK,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "OUT5",
		.id = ACT8600_OUT5,
		.ops = &act8600_ldo_ops,
		.n_voltages = VSEL_MASK,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "OUT6",
		.id = ACT8600_OUT6,
		.ops = &act8600_ldo_ops,
		.n_voltages = VSEL_MASK,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "OUT7",
		.id = ACT8600_OUT7,
		.ops = &act8600_ldo_ops,
		.n_voltages = VSEL_MASK,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},
	{
		.name = "OUT8",
		.id = ACT8600_OUT8,
		.ops = &act8600_ldo_ops,
		.n_voltages = VSEL_MASK,
		.type = REGULATOR_VOLTAGE,
		.owner = THIS_MODULE,
	},

};

struct regulator_desc *find_desc(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(act8600_regulator); i++) {
		if (strcmp(act8600_regulator[i].name, name))
			return &act8600_regulator[i];
	}

	return NULL;
}

static __devinit int act8600_regulator_probe(struct platform_device *pdev)
{
	struct act8600 *iodev = dev_get_drvdata(pdev->dev.parent);
	struct act8600_reg *act8600_reg;
	struct pmu_platform_data *pdata = dev_get_platdata(iodev->dev);
	struct regulator_dev **rdev;
	int i, ret, size;

	if (!pdata) {
		dev_err(pdev->dev.parent, "No platform init data supplied\n");
		return -ENODEV;
	}

	act8600_reg = kzalloc(sizeof(struct act8600_reg), GFP_KERNEL);
	if (!act8600_reg)
		return -ENOMEM;

	size = sizeof(struct regulator_dev *) * pdata->num_regulators;
	act8600_reg->rdev = kzalloc(size, GFP_KERNEL);
	if (!act8600_reg->rdev) {
		kfree(act8600_reg);
		return -ENOMEM;
	}

	rdev = act8600_reg->rdev;
	act8600_reg->dev = &pdev->dev;
	act8600_reg->iodev = iodev;

	platform_set_drvdata(pdev, act8600_reg);

	for (i = 0; i < pdata->num_regulators; i++) {
		struct regulator_info *reg_info = &pdata->regulators[i];
		struct regulator_desc *desc = find_desc(reg_info->name);

		if (!desc) {
			dev_err(pdev->dev.parent,
				"can't find regulator:%s\n", reg_info->name);
		} else {
			rdev[i] = regulator_register(desc, act8600_reg->dev,
						     reg_info->init_data,
						     act8600_reg);
			if (IS_ERR(rdev[i])) {
				ret = PTR_ERR(rdev[i]);
				dev_err(act8600_reg->dev,
					"regulator init failed\n");
				rdev[i] = NULL;
				goto err;
			}
		}
	}

	return 0;
err:
	for (i = 0; i < pdata->num_regulators; i++)
		if (rdev[i])
			regulator_unregister(rdev[i]);

	kfree(act8600_reg->rdev);
	kfree(act8600_reg);

	return ret;
}

static int __devexit act8600_regulator_remove(struct platform_device *pdev)
{
	struct act8600_reg *act8600_reg = platform_get_drvdata(pdev);
	struct act8600 *iodev = dev_get_drvdata(pdev->dev.parent);
	struct pmu_platform_data *pdata = dev_get_platdata(iodev->dev);
	struct regulator_dev **rdev = act8600_reg->rdev;
	int i;

	for (i = 0; i < pdata->num_regulators; i++)
		if (rdev[i])
			regulator_unregister(rdev[i]);

	kfree(act8600_reg->rdev);
	kfree(act8600_reg);

	return 0;
}

static struct platform_driver act8600_regulator_driver = {
	.driver = {
		.name = "act8600-regulator",
		.owner = THIS_MODULE,
	},
	.probe = act8600_regulator_probe,
	.remove = __devexit_p(act8600_regulator_remove),
};

static int __init act8600_regulator_init(void)
{
	return platform_driver_register(&act8600_regulator_driver);
}
subsys_initcall(act8600_regulator_init);

static void __exit act8600_regulator_cleanup(void)
{
	platform_driver_unregister(&act8600_regulator_driver);
}
module_exit(act8600_regulator_cleanup);

MODULE_DESCRIPTION("act8600 PMU regulator Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Large Dipper <ykli@ingenic.cn>");
