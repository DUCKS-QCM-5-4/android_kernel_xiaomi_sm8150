/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025-2026, Aman <duckyduckg65@gmail.com>
 */

#ifndef __TI_CHARGER_IIO_H
#define __TI_CHARGER_IIO_H

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/iio/consumer.h>
#include <linux/iio/iio.h>
#include <dt-bindings/iio/qti_power_supply_iio.h>

struct ti_charger_iio {
	struct device *dev;
	struct iio_dev *indio_dev;
	int (*ti_iio_read)(struct device *dev, int iio_chan, int *val);
	int (*ti_iio_write)(struct device *dev, int iio_chan, int val);
};

struct ti_charger_iio_channels {
	const char *datasheet_name;
	int channel_num;
	enum iio_chan_type type;
	long info_mask;
};

#define TI_CHARGER_IIO_CHAN(_name, _num, _type, _mask)		\
	{						\
		.datasheet_name = _name,		\
		.channel_num = _num,			\
		.type = _type,				\
		.info_mask = _mask,			\
	},

#define TI_CHARGER_CHAN_VOLT(_name, _num)			\
	TI_CHARGER_IIO_CHAN(_name, _num, IIO_VOLTAGE,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

#define TI_CHARGER_CHAN_CUR(_name, _num)			\
	TI_CHARGER_IIO_CHAN(_name, _num, IIO_CURRENT,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

#define TI_CHARGER_CHAN_TEMP(_name, _num)			\
	TI_CHARGER_IIO_CHAN(_name, _num, IIO_TEMP,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

#define TI_CHARGER_CHAN_POW(_name, _num)			\
	TI_CHARGER_IIO_CHAN(_name, _num, IIO_POWER,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

#define TI_CHARGER_CHAN_INDEX(_name, _num)			\
	TI_CHARGER_IIO_CHAN(_name, _num, IIO_INDEX,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

#define TI_CHARGER_CHAN_ACT(_name, _num)			\
	TI_CHARGER_IIO_CHAN(_name, _num, IIO_ACTIVITY,		\
		BIT(IIO_CHAN_INFO_PROCESSED))

static const struct ti_charger_iio_channels ti_charger_iio_psy_channels[] = {
	TI_CHARGER_CHAN_ACT("ti_battery_present", PSY_IIO_TI_BATTERY_PRESENT)
	TI_CHARGER_CHAN_ACT("ti_vbus_present", PSY_IIO_TI_VBUS_PRESENT)
	TI_CHARGER_CHAN_VOLT("ti_battery_voltage", PSY_IIO_TI_BATTERY_VOLTAGE)
	TI_CHARGER_CHAN_CUR("ti_battery_current", PSY_IIO_TI_BATTERY_CURRENT)
	TI_CHARGER_CHAN_TEMP("ti_battery_temperature", PSY_IIO_TI_BATTERY_TEMPERATURE)
	TI_CHARGER_CHAN_VOLT("ti_bus_voltage", PSY_IIO_TI_BUS_VOLTAGE)
	TI_CHARGER_CHAN_CUR("ti_bus_current", PSY_IIO_TI_BUS_CURRENT)
	TI_CHARGER_CHAN_TEMP("ti_bus_temperature", PSY_IIO_TI_BUS_TEMPERATURE)
	TI_CHARGER_CHAN_TEMP("ti_die_temperature", PSY_IIO_TI_DIE_TEMPERATURE)
	TI_CHARGER_CHAN_INDEX("ti_alarm_status", PSY_IIO_TI_ALARM_STATUS)
	TI_CHARGER_CHAN_INDEX("ti_fault_status", PSY_IIO_TI_FAULT_STATUS)
	TI_CHARGER_CHAN_INDEX("ti_reg_status", PSY_IIO_TI_REG_STATUS)
    TI_CHARGER_CHAN_INDEX("ti_set_bus_protection_for_qc3", PSY_IIO_TI_SET_BUS_PROTECTION_FOR_QC3)
    TI_CHARGER_CHAN_ACT("ti_bus_error_status", PSY_IIO_TI_BUS_ERROR_STATUS)
    TI_CHARGER_CHAN_ACT("ti_charging_enabled", PSY_IIO_TI_CHARGING_ENABLED)
};

static inline struct iio_channel **get_ti_ext_channels(struct device *dev,
		 const char *const *channel_map, int size)
{
	int i, rc = 0;
	struct iio_channel **iio_ch_ext;

	iio_ch_ext = devm_kcalloc(dev, size, sizeof(*iio_ch_ext), GFP_KERNEL);
	if (!iio_ch_ext)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < size; i++) {
		iio_ch_ext[i] = devm_iio_channel_get(dev, channel_map[i]);

		if (IS_ERR(iio_ch_ext[i])) {
			rc = PTR_ERR(iio_ch_ext[i]);
			if (rc != -EPROBE_DEFER)
				dev_err(dev, "%s channel unavailable, %d\n",
						channel_map[i], rc);
			return ERR_PTR(rc);
		}
	}

	return iio_ch_ext;
}

////wip
/* called by bq2597x / ln800 once they register as IIO device */
int ti_proxy_iio_register(struct ti_charger_iio *master);
void ti_proxy_iio_unregister(struct ti_charger_iio *master);

/* direct calls for policy drivrs*/
bool ti_proxy_iio_available(void);
int ti_proxy_iio_read(int channel_num, int *val);
int ti_proxy_iio_write(int channel_num, int val);

////wip
#endif