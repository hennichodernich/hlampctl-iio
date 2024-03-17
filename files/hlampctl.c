// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * hlampctl.c - driver for the Microchip hlampctl
 *
 * Copyright (C) 2013, Angelo Compagnucci, 2022 Henning Paul
 * Author:  Angelo Compagnucci <angelo.compagnucci@gmail.com>
 *          Henning Paul <hnch@gmx.net>
 *
 * This driver exports the value of analog input voltage to sysfs, the
 * voltage unit is nV.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <asm/unaligned.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define HLAMPCTL_ADCCHAN(_index) \
	{ \
		.type = IIO_VOLTAGE, \
		.indexed = 1, \
		.channel = _index, \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) \
				| BIT(IIO_CHAN_INFO_SCALE), \
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	}
#define HLAMPCTL_TEMPCHAN(_index) \
	{ \
		.type = IIO_TEMP, \
		.indexed = 1, \
		.channel = _index, \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	}
#define HLAMPCTL_ENABLECHAN(_index) \
	{ \
		.type = IIO_VOLTAGE, \
		.indexed = 1, \
		.channel = _index, \
		.output = 1, \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	}

/* Client data (each client gets its own) */
struct hlampctl {
	struct i2c_client *i2c;
	u8 id;
	struct mutex lock;
};

static int hlampctl_read(struct hlampctl *adc, u8 channel, int *value)
{
	u32 temp;
	s32 readval;

	switch(channel){
	case 0:
		readval = i2c_smbus_read_byte_data(adc->i2c, 2);
		break;
	case 1:
		readval = i2c_smbus_read_byte_data(adc->i2c, 1);
		break;
	case 2:
		readval = i2c_smbus_read_byte_data(adc->i2c, 3);
		break;
	default:
		readval = -1;
	}
	if (readval<0)
		return -EINVAL;
	else
		temp = (u32)readval;
	
	if (channel==2)
		temp = temp & 0x00000001;
	
	if (channel==0)
		*value = sign_extend32(temp, 11);
	else
		*value = temp;

	return 0;
}

static int hlampctl_write(struct hlampctl *adc, u8 channel, int value)
{
	s32 retval;

	if (channel==2){
		retval = i2c_smbus_write_byte_data(adc->i2c, 3, value > 0 ? 1 : 0);
	}
	else
		return -EINVAL;

	return retval;
}

static int hlampctl_read_channel(struct hlampctl *adc,
				struct iio_chan_spec const *channel, int *value)
{
	int ret;
	u8 req_channel = channel->channel;

	mutex_lock(&adc->lock);
	ret = hlampctl_read(adc, req_channel, value);
	mutex_unlock(&adc->lock);

	return ret;
}

static int hlampctl_write_channel(struct hlampctl *adc,
				struct iio_chan_spec const *channel, int value)
{
	int ret;
	u8 req_channel = channel->channel;

	mutex_lock(&adc->lock);
	ret = hlampctl_write(adc, req_channel, value);
	mutex_unlock(&adc->lock);

	return ret;
}

static int hlampctl_read_raw(struct iio_dev *iio,
			struct iio_chan_spec const *channel, int *val1,
			int *val2, long mask)
{
	struct hlampctl *adc = iio_priv(iio);
	int err;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		err = hlampctl_read_channel(adc, channel, val1);
		if (err < 0)
			return -EINVAL;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		if (channel->channel==0)
		{
			*val1 = 0;
			*val2 = (int)(3300000000/256); //nV per LSB
			return IIO_VAL_INT_PLUS_NANO;
		}
		else 
		{
			*val1 = 1;
			return IIO_VAL_INT;
		}
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val1 = 10;
		return IIO_VAL_INT;

	default:
		break;
	}

	return -EINVAL;
}

static int hlampctl_write_raw(struct iio_dev *iio,
			struct iio_chan_spec const *channel, int val1,
			int val2, long mask)
{
	struct hlampctl *adc = iio_priv(iio);
	int err;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (channel->channel==2){
			err = hlampctl_write_channel(adc, channel, val1);
			if (err < 0)
				return -EINVAL;
			else
				return 0;
		}
		else
			return -EINVAL;
	case IIO_CHAN_INFO_SCALE:
		return -EINVAL;

	case IIO_CHAN_INFO_SAMP_FREQ:
		return -EINVAL;

	default:
		break;
	}

	return -EINVAL;
}

static int hlampctl_write_raw_get_fmt(struct iio_dev *indio_dev,
		struct iio_chan_spec const *chan, long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		if (chan->channel==0)
			return IIO_VAL_INT_PLUS_NANO;
		else 
			return IIO_VAL_INT;
	case IIO_CHAN_INFO_SAMP_FREQ:
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static ssize_t hlampctl_show_samp_freqs(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "10\n");
}

static ssize_t hlampctl_show_scales(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "0.%09lu\n",	3300000000/256);
}

static IIO_DEVICE_ATTR(sampling_frequency_available, S_IRUGO,
		hlampctl_show_samp_freqs, NULL, 0);
static IIO_DEVICE_ATTR(in_voltage_scale_available, S_IRUGO,
		hlampctl_show_scales, NULL, 0);

static struct attribute *hlampctl_attributes[] = {
	&iio_dev_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_voltage_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group hlampctl_attribute_group = {
	.attrs = hlampctl_attributes,
};

static const struct iio_chan_spec hlampctl_channels[] = {
	HLAMPCTL_ADCCHAN(0),
	HLAMPCTL_TEMPCHAN(1),
	HLAMPCTL_ENABLECHAN(2),
};

static const struct iio_info hlampctl_info = {
	.read_raw = hlampctl_read_raw,
	.write_raw = hlampctl_write_raw,
	.write_raw_get_fmt = hlampctl_write_raw_get_fmt,
	.attrs = &hlampctl_attribute_group,
};

static int hlampctl_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct iio_dev *indio_dev;
	struct hlampctl *adc;
	u8 dummy;
	int err, ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EOPNOTSUPP;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);
	adc->i2c = client;

	mutex_init(&adc->lock);

	mutex_lock(&adc->lock);
        ret = i2c_master_recv(adc->i2c, &dummy, 1);
        mutex_unlock(&adc->lock);
	if (ret < 0)
	{
		dev_err(&client->dev, "I2C device not found\n");
                return -ENODEV;
	}

	indio_dev->name = client->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &hlampctl_info;

	indio_dev->channels = hlampctl_channels;
	indio_dev->num_channels = 3;

	err = devm_iio_device_register(&client->dev, indio_dev);
	if (err < 0)
		return err;

	i2c_set_clientdata(client, indio_dev);

	return 0;
}

static const struct i2c_device_id hlampctl_id[] = {
	{ "hlampctl", 1 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, hlampctl_id);

static const struct of_device_id hlampctl_of_match[] = {
	{ .compatible = "hlampctl" },
	{ }
};
MODULE_DEVICE_TABLE(of, hlampctl_of_match);

static struct i2c_driver hlampctl_driver = {
	.driver = {
		.name = "hlampctl",
		.of_match_table = hlampctl_of_match,
	},
	.probe = hlampctl_probe,
	.id_table = hlampctl_id,
};
module_i2c_driver(hlampctl_driver);

MODULE_AUTHOR("Henning Paul <hnch@gmx.net>");
MODULE_DESCRIPTION("hlamp control driver");
MODULE_LICENSE("GPL v2");
