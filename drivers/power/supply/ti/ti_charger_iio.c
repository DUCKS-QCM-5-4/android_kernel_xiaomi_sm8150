// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025-2026, Aman <duckyduckg65@gmail.com>
 */

#define pr_fmt(fmt) "[TI-IIO-PROXY]: %s: " fmt, __func__

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#include "ti_charger_iio.h"

struct ti_iio_proxy {
	struct device *dev;
	struct iio_dev *indio_dev;
	struct iio_chan_spec *iio_chan;
};

static DEFINE_MUTEX(g_ti_lock);
static struct ti_charger_iio *g_ti_proxy;
static bool g_ti_proxy_ready;

int ti_proxy_iio_register(struct ti_charger_iio *master)
{
	if (!master || !master->dev || !master->indio_dev)
		return -EINVAL;

	mutex_lock(&g_ti_lock);
	g_ti_proxy = master;
	mutex_unlock(&g_ti_lock);

	dev_info(master->dev, "Registered IIO master %s (%d channels)\n",
		 master->indio_dev->name, master->indio_dev->num_channels);
	return 0;
}
EXPORT_SYMBOL(ti_proxy_iio_register);

void ti_proxy_iio_unregister(struct ti_charger_iio *master)
{
	mutex_lock(&g_ti_lock);
	if (g_ti_proxy == master) {
		g_ti_proxy = NULL;
		g_ti_proxy_ready = false;
	}
	mutex_unlock(&g_ti_lock);
}
EXPORT_SYMBOL(ti_proxy_iio_unregister);

bool ti_proxy_iio_available(void)
{
	bool avail;

	mutex_lock(&g_ti_lock);
	avail = (g_ti_proxy != NULL && g_ti_proxy->dev != NULL &&
		 g_ti_proxy_ready);
	mutex_unlock(&g_ti_lock);

	return avail;
}
EXPORT_SYMBOL(ti_proxy_iio_available);

int ti_proxy_iio_read(int channel_num, int *val)
{
	int rc = -ENODEV;

	if (!val)
		return -EINVAL;

	mutex_lock(&g_ti_lock);
	if (g_ti_proxy && g_ti_proxy->dev && g_ti_proxy->ti_iio_read) {
		rc = g_ti_proxy->ti_iio_read(g_ti_proxy->dev, channel_num, val);
		rc = (rc < 0) ? rc : 0;
		pr_debug("master=%s chan=0x%x val=%d rc=%d\n",
			 g_ti_proxy->indio_dev->name, channel_num, *val, rc);
	}
	mutex_unlock(&g_ti_lock);

	return rc;
}
EXPORT_SYMBOL(ti_proxy_iio_read);

int ti_proxy_iio_write(int channel_num, int val)
{
	int rc = -ENODEV;

	mutex_lock(&g_ti_lock);
	if (g_ti_proxy && g_ti_proxy->dev && g_ti_proxy->ti_iio_write) {
		rc = g_ti_proxy->ti_iio_write(g_ti_proxy->dev, channel_num, val);
		rc = (rc < 0) ? rc : 0;
		pr_debug("master=%s chan=0x%x val=%d rc=%d\n",
			 g_ti_proxy->indio_dev->name, channel_num, val, rc);
	}
	mutex_unlock(&g_ti_lock);

	return rc;
}
EXPORT_SYMBOL(ti_proxy_iio_write);

static int ti_proxy_iio_read_raw(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan, int *val1,
				 int *val2, long mask)
{
	int rc;

	*val1 = 0;
	*val2 = 0;
	rc = ti_proxy_iio_read(chan->channel, val1);
	if (rc < 0)
		return rc;

	return IIO_VAL_INT;
}

static int ti_proxy_iio_write_raw(struct iio_dev *indio_dev,
				  struct iio_chan_spec const *chan, int val1,
				  int val2, long mask)
{
	return ti_proxy_iio_write(chan->channel, val1);
}

static int ti_proxy_iio_of_xlate(struct iio_dev *indio_dev,
				 const struct of_phandle_args *iiospec)
{
	struct ti_iio_proxy *proxy = iio_priv(indio_dev);
	struct iio_chan_spec *iio_chan = proxy->iio_chan;
	int i;

	for (i = 0; i < ARRAY_SIZE(ti_charger_iio_psy_channels); i++, iio_chan++) {
		if (iio_chan->channel == iiospec->args[0]) {
			pr_debug("chan=%s(0x%x) --- idx=%d\n",
				 iio_chan->datasheet_name,
				 iiospec->args[0], i);
			return i;
		}
	}

	return -EINVAL;
}

static const struct iio_info ti_proxy_iio_info = {
	.read_raw	= ti_proxy_iio_read_raw,
	.write_raw	= ti_proxy_iio_write_raw,
	.of_xlate	= ti_proxy_iio_of_xlate,
};

static int ti_iio_proxy_probe(struct platform_device *pdev)
{
	struct ti_iio_proxy *proxy;
	struct iio_dev *indio_dev;
	struct iio_chan_spec *chan;
	int num_chans = ARRAY_SIZE(ti_charger_iio_psy_channels);
	bool master_ready;
	int rc, i;

	mutex_lock(&g_ti_lock);
	master_ready = (g_ti_proxy != NULL && g_ti_proxy->dev != NULL);
	mutex_unlock(&g_ti_lock);

	if (!master_ready) {
		pr_debug("Master Charger not ready yet, deferring probe\n");
		return -EPROBE_DEFER;
	}

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*proxy));
	if (!indio_dev)
		return -ENOMEM;

	proxy = iio_priv(indio_dev);
	proxy->dev = &pdev->dev;
	proxy->indio_dev = indio_dev;
	platform_set_drvdata(pdev, proxy);

	proxy->iio_chan = devm_kcalloc(&pdev->dev, num_chans,
				       sizeof(*proxy->iio_chan),
				       GFP_KERNEL);
	if (!proxy->iio_chan)
		return -ENOMEM;

	indio_dev->dev.parent = &pdev->dev;
	indio_dev->dev.of_node = pdev->dev.of_node;
	indio_dev->info = &ti_proxy_iio_info;
	indio_dev->name = "ti_iio_proxy";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = proxy->iio_chan;
	indio_dev->num_channels = num_chans;

	for (i = 0; i < num_chans; i++) {
		chan = &proxy->iio_chan[i];
		chan->address = i;
		chan->channel = ti_charger_iio_psy_channels[i].channel_num;
		chan->type = ti_charger_iio_psy_channels[i].type;
		chan->datasheet_name =
			ti_charger_iio_psy_channels[i].datasheet_name;
		chan->extend_name =
			ti_charger_iio_psy_channels[i].datasheet_name;
		chan->info_mask_separate =
			ti_charger_iio_psy_channels[i].info_mask;
	}

	rc = devm_iio_device_register(&pdev->dev, indio_dev);
	if (rc) {
		dev_err(&pdev->dev, "Failed to register proxy IIO device, rc=%d\n", rc);
		return rc;
	}

	mutex_lock(&g_ti_lock);
	g_ti_proxy_ready = true;
	mutex_unlock(&g_ti_lock);

	dev_info(&pdev->dev, "Bound to master %s (%d channels)\n",
		 g_ti_proxy->indio_dev->name, num_chans);
	return 0;
}

static const struct of_device_id ti_iio_proxy_match_table[] = {
	{ .compatible = "mi,ti-iio-proxy" },
	{},
};

struct platform_driver ti_iio_proxy_driver = {
	.driver = {
		.name = "ti-iio-proxy",
		.of_match_table = ti_iio_proxy_match_table,
	},
	.probe = ti_iio_proxy_probe,
};

static int ti_iio_proxy_init(void)
{
	return platform_driver_register(&ti_iio_proxy_driver);
}

static void ti_iio_proxy_exit(void)
{
	platform_driver_unregister(&ti_iio_proxy_driver);
}

struct ti_charger_submodule_component {
	int (*init)(void);
	void (*exit)(void);
};

struct ti_charger_submodule {
	char *name;
	uint num_component;
	const struct ti_charger_submodule_component *component;
};

static const struct ti_charger_submodule_component ti_master_ic[] = {
	{&bq2597x_charger_init, &bq2597x_charger_exit},
#ifdef CONFIG_CHARGER_LN8000
	{&ln8000_charger_init, &ln8000_charger_exit},
#endif
};

static const struct ti_charger_submodule_component ti_proxy_driver[] = {
	{&ti_iio_proxy_init, &ti_iio_proxy_exit},
};

static const struct ti_charger_submodule_component ti_policy_driver[] = {
	{&usbpd_pm_init, &usbpd_pm_exit},
	{&cp_qc30_init, &cp_qc30_exit},
};

static const struct ti_charger_submodule submodule_table[] = {
	{
		.name = "TI Charge Pump IC",
		.num_component = ARRAY_SIZE(ti_master_ic),
		.component = ti_master_ic,
	},
	{
		.name = "TI IIO Proxy",
		.num_component = ARRAY_SIZE(ti_proxy_driver),
		.component = ti_proxy_driver,
	},
	{
		.name = "TI Policy Manager",
		.num_component = ARRAY_SIZE(ti_policy_driver),
		.component = ti_policy_driver,
	},
};

static int ti_charger_verify_submodules(void)
{
	int rc = 0;
	int i, j, num_components;

	for (i = 0; i < ARRAY_SIZE(submodule_table); i++) {
		num_components = submodule_table[i].num_component;
		for (j = 0; j < num_components; j++) {
			if (!submodule_table[i].component[j].init ||
			    !submodule_table[i].component[j].exit) {
				pr_err("%s module has init = %ps, exit = %ps\n",
				       submodule_table[i].name,
				       submodule_table[i].component[j].init,
				       submodule_table[i].component[j].exit);
				rc = -EINVAL;
				goto end;
			}
		}
	}

end:
	return rc;
}

static void __ti_charger_exit(int i, int j)
{
	uint num_exits;

	for (j -= 1; j >= 0; j--)
		submodule_table[i].component[j].exit();

	for (i -= 1; i >= 0; i--) {
		num_exits = submodule_table[i].num_component;
		for (j = num_exits - 1; j >= 0; j--)
			submodule_table[i].component[j].exit();
	}
}

static int __init ti_charger_main_init(void)
{
	int rc;
	uint i, j, num_inits;

	rc = ti_charger_verify_submodules();
	if (rc)
		goto end_init;

	for (i = 0; i < ARRAY_SIZE(submodule_table); i++) {
		num_inits = submodule_table[i].num_component;
		pr_debug("Number of %s components: %u\n",
			 submodule_table[i].name, num_inits);
		for (j = 0; j < num_inits; j++) {
			rc = submodule_table[i].component[j].init();
			if (rc) {
				pr_err("%s module failure at %ps rc = %d\n",
				       submodule_table[i].name,
				       submodule_table[i].component[j].init,
				       rc);
				__ti_charger_exit(i, j);
				goto end_init;
			}
		}
	}

	pr_info("TI charger initcalls done\n");

end_init:
	return rc;
}

static void __exit ti_charger_main_exit(void)
{
	__ti_charger_exit(ARRAY_SIZE(submodule_table), 0);
	pr_info("TI charger driver exited!\n");
}

module_init(ti_charger_main_init);
module_exit(ti_charger_main_exit);

MODULE_DESCRIPTION("wip");
MODULE_AUTHOR("Aman <duckyduckg65@gmail.com>");
MODULE_LICENSE("GPL v2");
