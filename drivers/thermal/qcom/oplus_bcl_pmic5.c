// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#define pr_fmt(fmt) "%s:%s " fmt, KBUILD_MODNAME, __func__

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/kernel.h>
#include <linux/regmap.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/spmi.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/thermal.h>
#include <linux/slab.h>
#include <linux/nvmem-consumer.h>
#include <linux/ipc_logging.h>
#include <linux/power_supply.h>
#include <linux/proc_fs.h>
/* Note: CREATE_TRACE_POINTS is defined in bcl_pmic5.c, not here to avoid duplicate symbols */
#include "trace.h"
#include <linux/rtc.h>
#include <linux/time.h>
#include <linux/delay.h>
#include "thermal_zone_internal.h"
#include "bcl_pmic5_internal.h"
#include "oplus_bcl_pmic5.h"

/* Function declarations are now in bcl_pmic5_internal.h */

/* Constants from bcl_pmic5.c */
#define BCL_VBAT_BASE_MV      2000
#define BCL_VBAT_THRESH_BASE  0x8CA
#define BCL_VBAT_MAX_MV       3600
#define SUBTYPE_ADDR 0x105
#define SUBTYPE_NUKU 0x5D    /* pmh0101 */
#define SUBTYPE_PMIH 0x56    /* pmih010x */
#define SUBTYPE_PM8550 0x49    /* pm8550 */

#define BCL_VBAT_CYCLE_THRESH_LOW	200
#define BCL_VBAT_CYCLE_THRESH_MID	500
#define BCL_VBAT_CYCLE_THRESH_HIGH	1000

/* Native structures and enums are now in bcl_pmic5_internal.h */
/* struct timeval is also defined in bcl_pmic5_internal.h */

/* Global variables */
int BCL_LEVEL0_COUNT;
int BCL_LEVEL1_COUNT;
int BCL_LEVEL2_COUNT;
struct proc_dir_entry *oplus_bcl_stat;

/* Function implementations */
int get_pmic_subtype(struct bcl_device *bcl_perph)
{
	unsigned int data = 0;
	int ret = 0;
	ret = regmap_read(bcl_perph->regmap, SUBTYPE_ADDR, &data);
	if (ret < 0) {
		dev_err(bcl_perph->dev, "Error reading PMIC SUBTYPE, err:%d\n", ret);
		return PMIC_SUBTYPE_MAX;
	}

	if (data == SUBTYPE_NUKU) { /* pmh0101 */
		return PMIC_SUBTYPE_PMH0101;
	} else if (data == SUBTYPE_PMIH) { /* pmih010x */
		return PMIC_SUBTYPE_PMIH010X;
	} else if (data == SUBTYPE_PM8550) { /* pm8550 */
		return PMIC_SUBTYPE_PM8550;
	} else {
		dev_err(bcl_perph->dev, "invalid subtype = 0x%x\n", data);
		return PMIC_SUBTYPE_MAX;
	}
}

static ssize_t bcl_count_show(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	char buffer[256];
	size_t len = 0;

	len = snprintf(buffer, sizeof(buffer), "%d  %d  %d\n", BCL_LEVEL0_COUNT, BCL_LEVEL1_COUNT, BCL_LEVEL2_COUNT);
	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

const struct proc_ops proc_bcl_count = {
	.proc_read		= bcl_count_show,
};

void do_gettimeofday(struct timeval *tv)
{
	struct timespec64 now;

	ktime_get_real_ts64(&now);
	tv->tv_sec = now.tv_sec;
	tv->tv_usec = now.tv_nsec/1000;
}

static int oplus_bcl_write_vbat_tz(struct bcl_device *bcl_perph, int trip_id, int temp)
{
	int ret = 0;
	int val = 0;
	int16_t addr;
	if (temp <= 0) {
		dev_err(bcl_perph->dev, "Invalid input temp\n");
		return -EINVAL;
	} else if (temp < BCL_VBAT_THRESH_BASE) {
		dev_err(bcl_perph->dev, "input temp is %d, lower than MIN\n", temp);
		return -EINVAL;
	} else if (temp > BCL_VBAT_MAX_MV) {
		dev_err(bcl_perph->dev, "input temp is %d, higher than MAX\n", temp);
		return -EINVAL;
	}
	addr = bcl_perph->desc->vbat_regs[trip_id];
	convert_vbat_to_vcmp_val(bcl_perph->desc, temp, &val);
	ret = bcl_write_register(bcl_perph, addr, val);
	if (ret < 0) {
		dev_err(bcl_perph->dev, "oplus_bcl_write_vbat_tz fail to set vbat regs, err: %d\n", ret);
		goto exit;
	}
	if (bcl_perph->desc->vbat_zone_enabled)
		blocking_notifier_call_chain(&bcl_pmic5_notifier, trip_id, (void *)&temp);

	dev_info(bcl_perph->dev, "oplus_bcl_write_vbat_tz trip_id: %d, vbat:%d mV\n", trip_id, temp);

exit:
	return ret;
}

#define DEFAULT_BATT_TEMP 250
#define DEFAULT_BATT_CYCLE_COUNT 0
static int bcl_read_battery_temp(struct bcl_device *bcl_perph, int *val)
{
	static struct power_supply *batt_psy;
	union power_supply_propval prop_val = {0};
	int rc;

	*val = DEFAULT_BATT_TEMP;

	if (!batt_psy)
		batt_psy = power_supply_get_by_name("battery");

	if (!batt_psy) {
		dev_err(bcl_perph->dev, "Failed to get battery power supply\n");
		return -ENODEV;
	}

	rc = power_supply_get_property(batt_psy, POWER_SUPPLY_PROP_TEMP, &prop_val);
	if (rc) {
		dev_err(bcl_perph->dev, "Battery temp read error: %d\n", rc);
		return rc;
	}

	*val = prop_val.intval;
	return 0;
}

static int bcl_read_battery_cycle_count(struct bcl_device *bcl_perph, int *val)
{
	static struct power_supply *batt_psy;
	union power_supply_propval prop_val = {0};
	int rc;

	*val = DEFAULT_BATT_CYCLE_COUNT;
	if (!batt_psy)
		batt_psy = power_supply_get_by_name("battery");
	if (batt_psy) {
		rc = power_supply_get_property(batt_psy,
				POWER_SUPPLY_PROP_CYCLE_COUNT, &prop_val);
		if (rc) {
			dev_err(bcl_perph->dev, "battery cycle count read error:%d\n", rc);
			return rc;
		}
		*val = prop_val.intval;
	} else {
		dev_err(bcl_perph->dev, "get battery psy failed\n");
		return -ENODEV;
	}

	return rc;
}

int battery_supply_callback(struct notifier_block *nb,
			unsigned long event, void *data)
{
	struct power_supply *psy = data;
	struct bcl_device *bcl_perph =
			container_of(nb, struct bcl_device, psy_nb);

	if (strncmp(psy->desc->name, "battery", strlen("battery")) != 0)
		return NOTIFY_OK;
	if (bcl_perph->support_dynamic_vbat)
		schedule_work(&bcl_perph->vbat_check_work);

	return NOTIFY_OK;
}

/**
 * bcl_get_temp_compensation_range - Get temperature compensation range index based on temperature
 * @bcl_perph: BCL device pointer
 * @batt_temp: Current battery temperature
 *
 * Returns: Temperature compensation range index, or 0 if no valid range found
 */
static int bcl_get_temp_compensation_range(struct bcl_device *bcl_perph, int batt_temp)
{
	int i;
	int temp_compensation_range = 0;

	if (bcl_perph->dynamic_vbat_config_compensation_count > 0 &&
	    bcl_perph->dynamic_vbat_compensation_config != NULL) {
		for (i = 0; i < bcl_perph->dynamic_vbat_config_compensation_count; i++) {
			if (batt_temp <= bcl_perph->dynamic_vbat_compensation_config[i].temp) {
				temp_compensation_range = i;
				break;
			}
		}

		if (i == bcl_perph->dynamic_vbat_config_compensation_count)
		temp_compensation_range = bcl_perph->dynamic_vbat_config_compensation_count - 1;
	} else {
		if (bcl_perph->dynamic_vbat_config_compensation_count > 0 &&
		    bcl_perph->dynamic_vbat_compensation_config == NULL) {
			dev_err(bcl_perph->dev, "dynamic_vbat_compensation_config is NULL but count=%d, using default cycle range\n",
				bcl_perph->dynamic_vbat_config_compensation_count);
		} else {
			dev_warn(bcl_perph->dev, "dynamic_vbat_config_compensation_count is 0, using default cycle range\n");
		}
	}

	return temp_compensation_range;
}


/**
 * bcl_get_cycle_compensation_range - Get cycle compensation range index based on cycle count
 * @cycle_count: Current battery cycle count
 *
 * Returns: Cycle compensation range index
 *   BCL_CYCLE_COMPENSATION_RANGE_LOW: cycle < 200
 *   BCL_CYCLE_COMPENSATION_RANGE_MID_LOW: 200 <= cycle < 500
 *   BCL_CYCLE_COMPENSATION_RANGE_MID_HIGH: 500 <= cycle < 1000
 *   BCL_CYCLE_COMPENSATION_RANGE_HIGH: cycle >= 1000
 */
 static int bcl_get_cycle_compensation_range(unsigned int cycle_count)
 {
	 if (cycle_count >= BCL_VBAT_CYCLE_THRESH_HIGH) {
		 return BCL_CYCLE_COMPENSATION_RANGE_HIGH;
	 } else if (cycle_count >= BCL_VBAT_CYCLE_THRESH_MID) {
		 return BCL_CYCLE_COMPENSATION_RANGE_MID_HIGH;
	 } else if (cycle_count >= BCL_VBAT_CYCLE_THRESH_LOW) {
		 return BCL_CYCLE_COMPENSATION_RANGE_MID_LOW;
	 } else {
		 return BCL_CYCLE_COMPENSATION_RANGE_LOW;
	 }
 }

/**
 * bcl_calculate_adjusted_vbat - Calculate adjusted vbat values based on cycle count and temperature
 * @bcl_perph: BCL device pointer
 * @dynamic_vbat_current_range: Current temperature range index (for base vbat values)
 * @temp_compensation_range: Current temperature compensation range index (for compensation values)
 * @cycle_count: Current battery cycle count
 * @adjust_lv0: Output parameter for adjusted lv0 value
 * @adjust_lv1: Output parameter for adjusted lv1 value
 * @adjust_lv2: Output parameter for adjusted lv2 value
 */
static int bcl_calculate_adjusted_vbat_threshold(struct bcl_device *bcl_perph,
				int dynamic_vbat_current_range,
				int temp_compensation_range,
				unsigned int cycle_count,
				unsigned int *adjust_lv0,
				unsigned int *adjust_lv1,
				unsigned int *adjust_lv2)
{
	unsigned int compensation_value = 0;

	if (bcl_perph->dynamic_vbat_compensation_config != NULL) {
		/* Validate temp_compensation_range to prevent array out-of-bounds access */
		if (temp_compensation_range < 0 ||
		    temp_compensation_range >= bcl_perph->dynamic_vbat_config_compensation_count) {
			dev_err(bcl_perph->dev, "Invalid temp_compensation_range=%d, compent_count=%d, using base values\n",
				temp_compensation_range, bcl_perph->dynamic_vbat_config_compensation_count);
			return -EINVAL;
		}

		/* Determine compent value based on cycle_count */
		dev_err(bcl_perph->dev, "bcl_vbat_check get temp_compensation_range=%d\n", temp_compensation_range);
		if (cycle_count >= BCL_VBAT_CYCLE_THRESH_HIGH) {
			compensation_value = bcl_perph->dynamic_vbat_compensation_config[temp_compensation_range].vbat_mv_over_1000_compensation;
		} else if (cycle_count >= BCL_VBAT_CYCLE_THRESH_MID) {
			compensation_value = bcl_perph->dynamic_vbat_compensation_config[temp_compensation_range].vbat_mv_500_1000_compensation;
		} else if (cycle_count >= BCL_VBAT_CYCLE_THRESH_LOW) {
			compensation_value = bcl_perph->dynamic_vbat_compensation_config[temp_compensation_range].vbat_mv_200_500_compensation;
		}
	}

	/* Apply compent value to base values */
	*adjust_lv0 = bcl_perph->dynamic_vbat_config[dynamic_vbat_current_range].vbat_mv_lv0 + compensation_value;
	*adjust_lv1 = bcl_perph->dynamic_vbat_config[dynamic_vbat_current_range].vbat_mv_lv1 + compensation_value;
	*adjust_lv2 = bcl_perph->dynamic_vbat_config[dynamic_vbat_current_range].vbat_mv_lv2 + compensation_value;

	return 0;
}


static inline int write_vbat_trip(struct bcl_device *bcl_perph, int trip_id, int mv)
{
	struct thermal_zone_device *tz = NULL;
	const struct thermal_trip *trip = NULL;

	if (bcl_perph->pmic_type == PMIC_SUBTYPE_PMH0101 || bcl_perph->pmic_type == PMIC_SUBTYPE_PM8550)
		return oplus_bcl_write_vbat_tz(bcl_perph, trip_id, mv);
	else {
		tz = bcl_perph->param[BCL_VBAT_LVL0].tz_dev;
		if (!tz || trip_id >= tz->num_trips) {
			dev_err(bcl_perph->dev, "tz_dev is NULL or trip_id is out of range\n");
			return -EINVAL;
		}
		trip = &tz->trips[trip_id].trip;
		return bcl_write_vbat_tz(tz, trip, mv);
	}
}


/**
 * bcl_process_cycle_count_and_adjust_vbat - Process cycle count and calculate adjusted vbat values
 * @bcl_perph: BCL device pointer
 * @dynamic_vbat_current_range: Current temperature range index
 * @current_cycle_range: Output parameter for current cycle range
 * @adjust_lv0: Output parameter for adjusted lv0 value
 * @adjust_lv1: Output parameter for adjusted lv1 value
 * @adjust_lv2: Output parameter for adjusted lv2 value
 *
 * This function encapsulates the three-step process:
 * 1. Read battery cycle count
 * 2. Get cycle range based on cycle count (only if compent config is available)
 * 3. Calculate adjusted vbat values
 */
static void bcl_process_cycle_count_and_adjust_vbat(struct bcl_device *bcl_perph,
					     int dynamic_vbat_current_range,
						 int batt_temp,
						 int *temp_compensation_range,
						 int *cycle_compensation_range,
					     unsigned int *adjust_lv0,
					     unsigned int *adjust_lv1,
					     unsigned int *adjust_lv2)
{
	unsigned int cycle_count = 0;
	if (!bcl_perph) {
		pr_err("BCL device is NULL in %s\n", __func__);
		goto default_config;
	}
	if (!bcl_perph->support_dynamic_vbat_compensation) {
		dev_err(bcl_perph->dev, "Invalid parameters or not support_dynamic_vbat_compensation\n");
		goto default_config;
	}

	/* Step 1: Read battery cycle count */
	if (bcl_read_battery_cycle_count(bcl_perph, &cycle_count) < 0) {
		dev_warn(bcl_perph->dev, "Failed to read cycle count, using default config\n");
		goto default_config;
	}

	/* Get cycle compensation range based on cycle count */
	*cycle_compensation_range = bcl_get_cycle_compensation_range(cycle_count);

	/* Get temperature compensation range based on temperature */
	if (bcl_perph->dynamic_vbat_compensation_config != NULL) {
		*temp_compensation_range = bcl_get_temp_compensation_range(bcl_perph, batt_temp);
	} else {
		/* No compensation config, use default */
		goto default_config;
	}
	/* Step 3: Calculate adjusted vbat values */
	if(bcl_calculate_adjusted_vbat_threshold(bcl_perph, dynamic_vbat_current_range, *temp_compensation_range, cycle_count,
				    adjust_lv0, adjust_lv1, adjust_lv2) < 0) {
		goto default_config;
	}

	return;

default_config:
	*adjust_lv0 = bcl_perph->dynamic_vbat_config[dynamic_vbat_current_range].vbat_mv_lv0;
	*adjust_lv1 = bcl_perph->dynamic_vbat_config[dynamic_vbat_current_range].vbat_mv_lv1;
	*adjust_lv2 = bcl_perph->dynamic_vbat_config[dynamic_vbat_current_range].vbat_mv_lv2;

	return;
}

static int apply_dynamic_vbat_thresholds(struct bcl_device *bcl_perph, int batt_temp, bool force_write)
{
	int i;
	int dynamic_vbat_current_range = 0;
	int temp_compensation_range = 0;
	int cycle_compensation_range = 0;
	unsigned int adjust_dynamic_vbat_lv0 = 0;
	unsigned int adjust_dynamic_vbat_lv1 = 0;
	unsigned int adjust_dynamic_vbat_lv2 = 0;
	int ret = 0;

	if (!bcl_perph || bcl_perph->dynamic_vbat_config_count <= 0)
		return -EINVAL;

	mutex_lock(&bcl_perph->dynamic_vbat_lock);

	for (i = 0; i < bcl_perph->dynamic_vbat_config_count; i++) {
		if (batt_temp <= bcl_perph->dynamic_vbat_config[i].temp) {
			dynamic_vbat_current_range = i;
			break;
		}
	}
	if (i == bcl_perph->dynamic_vbat_config_count)
		dynamic_vbat_current_range = bcl_perph->dynamic_vbat_config_count - 1;

	bcl_process_cycle_count_and_adjust_vbat(bcl_perph, dynamic_vbat_current_range,
						batt_temp,
						&temp_compensation_range,
						&cycle_compensation_range,
						&adjust_dynamic_vbat_lv0,
						&adjust_dynamic_vbat_lv1,
						&adjust_dynamic_vbat_lv2);

	if (force_write ||
	    bcl_perph->dynamic_vbat_pre_range != dynamic_vbat_current_range ||
	    bcl_perph->prev_temp_compensation_range != temp_compensation_range ||
	    bcl_perph->prev_cycle_compensation_range != cycle_compensation_range) {
			write_vbat_trip(bcl_perph, BCLBIG_COMP_VCMP_L0_THR, adjust_dynamic_vbat_lv0);
			write_vbat_trip(bcl_perph, BCLBIG_COMP_VCMP_L1_THR, adjust_dynamic_vbat_lv1);
			write_vbat_trip(bcl_perph, BCLBIG_COMP_VCMP_L2_THR, adjust_dynamic_vbat_lv2);
			bcl_perph->dynamic_vbat_pre_range = dynamic_vbat_current_range;
			bcl_perph->prev_temp_compensation_range = temp_compensation_range;
			bcl_perph->prev_cycle_compensation_range = cycle_compensation_range;

			dev_info(bcl_perph->dev, "update bcl vbat batt_temp=%d, lv0=%d, lv1=%d, lv2=%d\n",
						batt_temp, adjust_dynamic_vbat_lv0, adjust_dynamic_vbat_lv1, adjust_dynamic_vbat_lv2);
	}

	mutex_unlock(&bcl_perph->dynamic_vbat_lock);
	return ret;
}

/**
 * bcl_force_apply_dynamic_vbat_threshold - Apply dynamic vbat configuration based on battery temperature
 * @bcl_perph: BCL device pointer
 *
 * This function reads battery temperature, finds the corresponding range in
 * dynamic_vbat_config, and updates vbat thresholds using bcl_write_vbat_tz.
 * This version is for PMIH010X PMIC type.
 *
 * Return: 0 on success, negative error code on failure
 */
static int bcl_force_apply_dynamic_vbat_threshold(struct bcl_device *bcl_perph)
{
	int batt_temp;

	if (bcl_read_battery_temp(bcl_perph, &batt_temp) < 0) {
		dev_err(bcl_perph->dev, "Failed to read battery temperature\n");
		return -EIO;
	}
	if (bcl_perph->dynamic_vbat_config_count <= 0) {
		dev_err(bcl_perph->dev, "dynamic_vbat_config_count is 0, skip vbat check\n");
		return -EINVAL;
	}

	return apply_dynamic_vbat_thresholds(bcl_perph, batt_temp, true);
}

void bcl_vbat_check(struct work_struct *work)
{
	struct bcl_device *bcl_perph = container_of(work, struct bcl_device, vbat_check_work);
	int batt_temp;

	if (bcl_read_battery_temp(bcl_perph, &batt_temp) < 0) {
		dev_err(bcl_perph->dev, "Failed to read battery temperature\n");
		return;
	}

	if (bcl_perph->dynamic_vbat_config_count <= 0) {
		dev_err(bcl_perph->dev, "dynamic_vbat_config_count is 0, skip vbat check\n");
		return;
	}

	apply_dynamic_vbat_thresholds(bcl_perph, batt_temp, false);
}

/**
 * pmh0101_bcl_force_apply_dynamic_vbat_threshold - Force apply dynamic vbat configuration based on battery temperature
 * @bcl_perph: BCL device pointer
 *
 * This function reads battery temperature, finds the corresponding range in
 * dynamic_vbat_config, and updates vbat thresholds using oplus_bcl_write_vbat_tz.
 * This version is for PMH0101 and PM8550 PMIC types.
 *
 * Return: 0 on success, negative error code on failure
 */
static int pmh0101_bcl_force_apply_dynamic_vbat_threshold(struct bcl_device *bcl_perph)
{
	int batt_temp;

	if (bcl_read_battery_temp(bcl_perph, &batt_temp) < 0) {
		dev_err(bcl_perph->dev, "Failed to read battery temperature\n");
		return -EIO;
	}
	if (bcl_perph->dynamic_vbat_config_count <= 0) {
		dev_err(bcl_perph->dev, "dynamic_vbat_config_count is 0, skip vbat check\n");
		return -EINVAL;
	}

	return apply_dynamic_vbat_thresholds(bcl_perph, batt_temp, true);
}

void pmh0101_bcl_vbat_check(struct work_struct *work)
{
	struct bcl_device *bcl_perph = container_of(work, struct bcl_device, vbat_check_work);
	int batt_temp;

	if (bcl_read_battery_temp(bcl_perph, &batt_temp) < 0) {
		dev_err(bcl_perph->dev, "Failed to read battery temperature\n");
		return;
	}
	if (bcl_perph->dynamic_vbat_config_count <= 0) {
		dev_err(bcl_perph->dev, "dynamic_vbat_config_count is 0, skip vbat check\n");
		return;
	}
	apply_dynamic_vbat_thresholds(bcl_perph, batt_temp, false);
}

static int bcl_update_dynamic_vbat_config(struct bcl_device *bcl_perph, int index,
				  int temp, int vbat_mv_lv0,
				  int vbat_mv_lv1, int vbat_mv_lv2);

/**
 * bcl_do_restore_vbat_from_backup - Restore vbat config from backup data
 * @bcl_perph: BCL device pointer
 * @is_auto: true if this is auto restore, false if manual restore
 *
 * Common function to restore dynamic_vbat_config from dynamic_vbat_data_backup
 *
 * Return: 0 on success, negative error code on failure
 */
static int bcl_do_restore_vbat_from_backup(struct bcl_device *bcl_perph, bool is_auto)
{
	int i, ret;

	if (!bcl_perph) {
		pr_err("BCL device is NULL in %s\n", __func__);
		return -ENOTSUPP;
	}
	if (!bcl_perph->support_dynamic_vbat) {
		dev_err(bcl_perph->dev, "dynamic_vbat not supported\n");
		return -ENOTSUPP;
	}

	if (!bcl_perph->dynamic_vbat_data_backup) {
		dev_err(bcl_perph->dev, "backup data not available\n");
		return -EINVAL;
	}

	dev_info(bcl_perph->dev, "%s restore from backup\n", is_auto ? "Auto" : "sys boot complete");

	/* Update config with values from backup */
	for (i = 0; i < bcl_perph->dynamic_vbat_config_count &&
	     i < bcl_perph->dynamic_vbat_data_backup_count; i++) {
		ret = bcl_update_dynamic_vbat_config(bcl_perph, i,
				bcl_perph->dynamic_vbat_data_backup[i].temp,
				bcl_perph->dynamic_vbat_data_backup[i].vbat_mv_lv0,
				bcl_perph->dynamic_vbat_data_backup[i].vbat_mv_lv1,
				bcl_perph->dynamic_vbat_data_backup[i].vbat_mv_lv2);
		if (ret) {
			dev_err(bcl_perph->dev, "Failed to %s restore from backup at index %d\n",
					is_auto ? "auto" : "", i);
			return ret;
		}
	}

	/* Apply the restored configuration based on current battery temperature */
	/* Choose the appropriate function based on PMIC type */
	if (bcl_perph->pmic_type == PMIC_SUBTYPE_PMH0101 ||
	    bcl_perph->pmic_type == PMIC_SUBTYPE_PM8550) {
		ret = pmh0101_bcl_force_apply_dynamic_vbat_threshold(bcl_perph);
	} else {
		ret = bcl_force_apply_dynamic_vbat_threshold(bcl_perph);
	}
	if (ret) {
		dev_err(bcl_perph->dev, "Failed to apply restored vbat config, err:%d\n", ret);
		return ret;
	}

	dev_info(bcl_perph->dev, "%s update from backup\n", is_auto ? "Auto" : "sys boot complete");
	return 0;
}

void bcl_manual_restore_vbat_config_from_backup(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct bcl_device *bcl_perph = container_of(delayed_work, struct bcl_device, vbat_manual_restore_work);

	/* Check if already triggered manually */
	if (bcl_perph->vbat_auto_restore_triggered) {
		dev_info(bcl_perph->dev, "auto restore already triggered, skip manual restore\n");
		return;
	}
	bcl_perph->vbat_manual_restore_triggered = true;
	bcl_do_restore_vbat_from_backup(bcl_perph, false);
}

void bcl_auto_restore_vbat_config_from_backup(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct bcl_device *bcl_perph = container_of(delayed_work,
			struct bcl_device, vbat_auto_restore_work);

	/* Check if already triggered manually */
	if (bcl_perph->vbat_manual_restore_triggered) {
		dev_info(bcl_perph->dev, "Manual restore already triggered, skip auto restore\n");
		return;
	}
	bcl_perph->vbat_auto_restore_triggered = true;
	bcl_do_restore_vbat_from_backup(bcl_perph, true);
}

/**
 * bcl_update_dynamic_vbat_config - Update dynamic vbat configuration values
 * @bcl_perph: BCL device pointer
 * @index: Index of the configuration to update
 * @temp: Temperature threshold
 * @vbat_mv_lv0: VBAT threshold for level 0
 * @vbat_mv_lv1: VBAT threshold for level 1
 * @vbat_mv_lv2: VBAT threshold for level 2
 *
 * This function only updates the configuration values in memory.
 * To apply the configuration, call bcl_force_apply_dynamic_vbat_threshold or
 * pmh0101_bcl_force_apply_dynamic_vbat_threshold separately.
 *
 * Return: 0 on success, negative error code on failure
 */
static int bcl_update_dynamic_vbat_config(struct bcl_device *bcl_perph, int index,
				  int temp, int vbat_mv_lv0,
				  int vbat_mv_lv1, int vbat_mv_lv2)
{
	if (!bcl_perph) {
		pr_err("BCL device is NULL in %s\n", __func__);
		return -EINVAL;
	}

	if (!bcl_perph->support_dynamic_vbat) {
		dev_err(bcl_perph->dev, "dynamic_vbat is not supported\n");
		return -ENOTSUPP;
	}

	if (!bcl_perph->dynamic_vbat_config) {
		dev_err(bcl_perph->dev, "dynamic_vbat_config is NULL\n");
		return -EINVAL;
	}

	if (index < 0 || index >= bcl_perph->dynamic_vbat_config_count) {
		dev_err(bcl_perph->dev, "Invalid index %d, valid range: 0-%d\n",
				index, bcl_perph->dynamic_vbat_config_count - 1);
		return -EINVAL;
	}

	/* Validate input parameters */
	/* temp unit: 0.1°C, so -100 = -10.0°C, 1000 = 100.0°C */
	if (temp < -100 || temp > 1000) {
		dev_err(bcl_perph->dev, "Invalid temp value: %d (valid range: -100 to 1000, i.e., -10.0°C to 100.0°C)\n", temp);
	}

	if (vbat_mv_lv0 < BCL_VBAT_BASE_MV || vbat_mv_lv0 > BCL_VBAT_MAX_MV ||
	    vbat_mv_lv1 < BCL_VBAT_BASE_MV || vbat_mv_lv1 > BCL_VBAT_MAX_MV ||
	    vbat_mv_lv2 < BCL_VBAT_BASE_MV || vbat_mv_lv2 > BCL_VBAT_MAX_MV) {
		dev_err(bcl_perph->dev, "Invalid vbat values: lv0=%d, lv1=%d, lv2=%d\n",
				vbat_mv_lv0, vbat_mv_lv1, vbat_mv_lv2);
		return -EINVAL;
	}

	/* Update configuration values */
	bcl_perph->dynamic_vbat_config[index].temp = temp;
	bcl_perph->dynamic_vbat_config[index].vbat_mv_lv0 = vbat_mv_lv0;
	bcl_perph->dynamic_vbat_config[index].vbat_mv_lv1 = vbat_mv_lv1;
	bcl_perph->dynamic_vbat_config[index].vbat_mv_lv2 = vbat_mv_lv2;

	dev_info(bcl_perph->dev,
		"Updated dynamic_vbat_config[%d]: temp=%d, lv0=%d, lv1=%d, lv2=%d\n",
		index, temp, vbat_mv_lv0, vbat_mv_lv1, vbat_mv_lv2);

	return 0;
}

/**
 * bcl_get_pmic_name - Get PMIC name string based on PMIC type
 * @pmic_type: PMIC subtype type
 *
 * Return: PMIC name string
 */
static const char *bcl_get_pmic_name(int pmic_type)
{
	switch (pmic_type) {
	case PMIC_SUBTYPE_PMH0101:
		return "PMH0101";
	case PMIC_SUBTYPE_PMIH010X:
		return "PMIH010X";
	case PMIC_SUBTYPE_PM8550:
		return "PM8550";
	default:
		return "Unknown";
	}
}

static ssize_t dynamic_vbat_proc_read(struct file *file, char __user *buf,
			      size_t count, loff_t *ppos)
{
	struct bcl_device *bcl_perph = pde_data(file_inode(file));
	char buffer[1024];
	size_t len = 0;
	int i;
	bool auto_restore_pending;
	const char *pmic_name;

	if (!bcl_perph || !bcl_perph->support_dynamic_vbat) {
		len = snprintf(buffer, sizeof(buffer), "dynamic_vbat not supported\n");
		return simple_read_from_buffer(buf, count, ppos, buffer, len);
	}

	/* Get PMIC name */
	pmic_name = bcl_get_pmic_name(bcl_perph->pmic_type);
	/* Check if auto restore work is still pending */
	auto_restore_pending = delayed_work_pending(&bcl_perph->vbat_auto_restore_work);

	/* Print status information */
	len += snprintf(buffer + len, sizeof(buffer) - len, "PMIC Type: %s\n", pmic_name);
	len += snprintf(buffer + len, sizeof(buffer) - len, "Status:\n");
	len += snprintf(buffer + len, sizeof(buffer) - len, "  Manual trigger (write 1): %s\n",
			bcl_perph->vbat_manual_restore_triggered ? "Yes" : "No");
	len += snprintf(buffer + len, sizeof(buffer) - len, "  Auto restore pending: %s\n",
			auto_restore_pending ? "Yes" : "No");
	len += snprintf(buffer + len, sizeof(buffer) - len, "  Auto restore triggered: %s\n",
			bcl_perph->vbat_auto_restore_triggered ? "Yes" : "No");
	len += snprintf(buffer + len, sizeof(buffer) - len, "\nConfig values:\n");

	for (i = 0; i < bcl_perph->dynamic_vbat_config_count; i++) {
		if (len >= sizeof(buffer))
			break;
		len += snprintf(buffer + len, sizeof(buffer) - len,
				"[%d] temp=%d, lv0=%d, lv1=%d, lv2=%d\n",
				i,
				bcl_perph->dynamic_vbat_config[i].temp,
				bcl_perph->dynamic_vbat_config[i].vbat_mv_lv0,
				bcl_perph->dynamic_vbat_config[i].vbat_mv_lv1,
				bcl_perph->dynamic_vbat_config[i].vbat_mv_lv2);
	}

	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static ssize_t dynamic_vbat_proc_write(struct file *file, const char __user *buf,
			       size_t count, loff_t *ppos)
{
	struct bcl_device *bcl_perph = pde_data(file_inode(file));
	char buffer[16];
	int val;
	const char *pmic_name;

	if (!bcl_perph) {
		pr_err("BCL device is NULL in %s\n", __func__);
		return -ENODEV;
	}
	if (!bcl_perph->support_dynamic_vbat) {
		dev_err(bcl_perph->dev, "dynamic_vbat not supported\n");
		return -ENOTSUPP;
	}

	if (count >= sizeof(buffer))
		return -EINVAL;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	buffer[count] = 0;

	/* Remove newline character */
	if (count > 0 && buffer[count - 1] == '\n')
		buffer[count - 1] = 0;

	/* Only accept '1', update config with values from backup */
	if (kstrtoint(buffer, 10, &val) != 0 || val != 1) {
		dev_err(bcl_perph->dev, "Invalid input, only '1' is accepted\n");
		return -EINVAL;
	}

	/* Check if backup data is available */
	if (!bcl_perph->dynamic_vbat_data_backup) {
		dev_err(bcl_perph->dev, "backup data not available\n");
		return -EINVAL;
	}

	/* Mark as manually triggered and cancel auto restore work */
	bcl_perph->vbat_manual_restore_triggered = true;
	cancel_delayed_work_sync(&bcl_perph->vbat_auto_restore_work);

	/* Cancel any pending restore work */
	cancel_delayed_work_sync(&bcl_perph->vbat_manual_restore_work);
	pmic_name = bcl_get_pmic_name(bcl_perph->pmic_type);
	dev_info(bcl_perph->dev, "dynamic_vbat_proc_write %s vbat_manual_restore_triggered = %d\n",
			pmic_name, bcl_perph->vbat_manual_restore_triggered);
	/* Schedule delayed work to restore after 15 seconds */
	dev_info(bcl_perph->dev, "Scheduled update from backup in %d ms\n", bcl_perph->vbat_manual_restore_delay_ms);
	schedule_delayed_work(&bcl_perph->vbat_manual_restore_work, msecs_to_jiffies(bcl_perph->vbat_manual_restore_delay_ms));

	return count;
}

const struct proc_ops dynamic_vbat_proc_ops = {
	.proc_read = dynamic_vbat_proc_read,
	.proc_write = dynamic_vbat_proc_write,
};

void oplus_bcl_probe_init(struct platform_device *pdev, struct bcl_device *bcl_perph)
{
	struct proc_dir_entry *proc_node;
	int err;

	if (!bcl_perph || !bcl_perph->support_dynamic_vbat) {
		dev_err(&pdev->dev, "oplus_bcl_probe_init bcl_perph or support_dynamic_vbat is NULL\n");
		return;
	}

	/* First instance: create proc root directory */
	if (!oplus_bcl_stat) {
		oplus_bcl_stat = proc_mkdir("bcl_stat", NULL);
		if (oplus_bcl_stat) {
			proc_node = proc_create("bcl_count", 0664, oplus_bcl_stat, &proc_bcl_count);
			dev_info(&pdev->dev, "Created proc root directory: /proc/bcl_stat\n");
		} else {
			dev_err(&pdev->dev, "Couldn't create oplus bcl_stat\n");
		}
	}

	/* init vbat check work by pmic type */
	if (bcl_perph->pmic_type == PMIC_SUBTYPE_PMH0101 || bcl_perph->pmic_type == PMIC_SUBTYPE_PM8550) {
		dev_err(&pdev->dev, "bcl_probe bcl_perph->pmic_type %d init work !\n", bcl_perph->pmic_type);
		INIT_WORK(&bcl_perph->vbat_check_work, pmh0101_bcl_vbat_check);
	} else {
		dev_err(&pdev->dev, "bcl_probe bcl_perph->pmic_type %d init work !\n", bcl_perph->pmic_type);
		INIT_WORK(&bcl_perph->vbat_check_work, bcl_vbat_check);
	}

	/* Initialize restore flows and proc only when backup table exists */
	if (bcl_perph->dynamic_vbat_data_backup && bcl_perph->dynamic_vbat_data_backup_count > 0) {
		/* init delayed works */
		INIT_DELAYED_WORK(&bcl_perph->vbat_manual_restore_work, bcl_manual_restore_vbat_config_from_backup);
		INIT_DELAYED_WORK(&bcl_perph->vbat_auto_restore_work, bcl_auto_restore_vbat_config_from_backup);
		bcl_perph->vbat_auto_restore_triggered = false;
		bcl_perph->vbat_manual_restore_triggered = false;
		/* create proc (requires oplus_bcl_stat present) */
		if (oplus_bcl_stat) {
			char proc_name[32];
			snprintf(proc_name, sizeof(proc_name), "dynamic_vbat_%d", bcl_perph->id);
			bcl_perph->dynamic_vbat_proc_entry = proc_create_data(proc_name, 0664,
					oplus_bcl_stat, &dynamic_vbat_proc_ops, bcl_perph);
			if (!bcl_perph->dynamic_vbat_proc_entry)
				dev_err(&pdev->dev, "Couldn't create %s proc entry\n", proc_name);
			else
				dev_info(&pdev->dev, "Created proc entry: /proc/bcl_stat/%s\n", proc_name);
		}

		/* schedule auto-restore */
		dev_info(&pdev->dev, "Scheduled auto restore from backup in %d ms\n",
			 bcl_perph->vbat_auto_restore_delay_ms);
		schedule_delayed_work(&bcl_perph->vbat_auto_restore_work, msecs_to_jiffies(bcl_perph->vbat_auto_restore_delay_ms));
	} else {
		dev_info(&pdev->dev, "Skip proc entry and auto restore: no backup data\n");
	}

	/* Initialize vbat check state variables for multi-PMIC support */
	/* -1 indicates invalid/uninitialized state, ensures first check triggers update */
	mutex_init(&bcl_perph->dynamic_vbat_lock);
	bcl_perph->dynamic_vbat_pre_range = -1;
	bcl_perph->prev_temp_compensation_range = -1;
	bcl_perph->prev_cycle_compensation_range = -1;
	bcl_perph->batt_psy = NULL;

	/* Register battery supply notifier */
	bcl_perph->psy_nb.notifier_call = battery_supply_callback;
	err = power_supply_reg_notifier(&bcl_perph->psy_nb);
	if (err < 0)
		dev_err(&pdev->dev, "psy notifier register error ret:%d\n", err);
}

/**
 * bcl_load_default_vbat_data_from_dts - Load default vbat parameters from dts and store
 * @pdev: platform device pointer
 * @bcl_perph: BCL device pointer
 *
 * Read bcl,dynamic_vbat_data from dts, initialize dynamic_vbat_config
 *
 * Return: 0 on success, negative error code on failure
 */
static int bcl_load_default_vbat_data_from_dts(struct platform_device *pdev,
					struct bcl_device *bcl_perph)
{
	int ret;
	struct device_node *dev_node = pdev->dev.of_node;
	int num_elem;
	int buf[64] = {0};
	int i;

	num_elem = of_property_count_elems_of_size(dev_node, "bcl,dynamic_vbat_data", sizeof(int));
	if (num_elem <= 0) {
		dev_err(&pdev->dev, "dynamic_vbat_data not found or empty\n");
		return -EINVAL;
	}

	if (num_elem % 4) {
		dev_err(&pdev->dev, "invalid len for dynamic_vbat_data\n");
		return -EINVAL;
	}

	ret = of_property_read_u32_array(dev_node, "bcl,dynamic_vbat_data", (u32 *)buf, num_elem);
	if (ret) {
		dev_err(&pdev->dev, "dynamic_vbat_data read failed %d\n", ret);
		return ret;
	}
	dev_err(&pdev->dev, "load default vbat data for dynamic_vbat_config\n");
	bcl_perph->dynamic_vbat_config_count = num_elem / 4;
	bcl_perph->dynamic_vbat_config = devm_kcalloc(&pdev->dev,
			bcl_perph->dynamic_vbat_config_count, sizeof(struct dynamic_vbat_data), GFP_KERNEL);
	if (!bcl_perph->dynamic_vbat_config) {
		dev_err(&pdev->dev, "fail to alloc dynamic_vbat_config memory\n");
		return -ENOMEM;
	}

	for (i = 0; i < bcl_perph->dynamic_vbat_config_count; i++) {
		bcl_perph->dynamic_vbat_config[i].temp = buf[i * 4 + 0];
		bcl_perph->dynamic_vbat_config[i].vbat_mv_lv0 = buf[i * 4 + 1];
		bcl_perph->dynamic_vbat_config[i].vbat_mv_lv1 = buf[i * 4 + 2];
		bcl_perph->dynamic_vbat_config[i].vbat_mv_lv2 = buf[i * 4 + 3];
		dev_err(&pdev->dev, "dynamic_vbat_config[%d]:temp=%d, lv0=%d, lv1=%d, lv2=%d\n", i,
			bcl_perph->dynamic_vbat_config[i].temp,
			bcl_perph->dynamic_vbat_config[i].vbat_mv_lv0,
			bcl_perph->dynamic_vbat_config[i].vbat_mv_lv1,
			bcl_perph->dynamic_vbat_config[i].vbat_mv_lv2);
	}

	return 0;
}

/**
 * bcl_load_backup_vbat_data_from_dts - Load backup vbat parameters from dts and store
 * @pdev: platform device pointer
 * @bcl_perph: BCL device pointer
 *
 * Read bcl,dynamic_vbat_data_alt from dts, initialize dynamic_vbat_data_backup
 * This is an optional parameter. Returns -EINVAL if property doesn't exist (normal case,
 * caller will handle it as optional). Returns error code if property exists but fails
 * to parse/load.
 *
 * Return: 0 on success, -EINVAL if property doesn't exist (optional), negative error code on failure
 */
static int bcl_load_backup_vbat_data_from_dts(struct platform_device *pdev,
				       struct bcl_device *bcl_perph)
{
	int ret;
	struct device_node *dev_node = pdev->dev.of_node;
	int num_elem;
	int buf[64] = {0};
	int i;

	num_elem = of_property_count_elems_of_size(dev_node, "bcl,dynamic_vbat_data_alt", sizeof(int));
	if (num_elem < 0) {
		/* Property doesn't exist, which is fine for optional parameter */
		return -EINVAL;
	}
	if (num_elem == 0) {
		dev_err(&pdev->dev, "dynamic_vbat_data_alt is empty\n");
		return -EINVAL;
	}

	if (num_elem % 4) {
		dev_err(&pdev->dev, "invalid len for dynamic_vbat_data_alt\n");
		return -EINVAL;
	}

	ret = of_property_read_u32_array(dev_node, "bcl,dynamic_vbat_data_alt", (u32 *)buf, num_elem);
	if (ret) {
		dev_err(&pdev->dev, "dynamic_vbat_data_alt read failed %d\n", ret);
		return ret;
	}
	dev_err(&pdev->dev, "load backup vbat data for dynamic_vbat_data_backup\n");
	bcl_perph->dynamic_vbat_data_backup_count = num_elem / 4;
	bcl_perph->dynamic_vbat_data_backup = devm_kcalloc(&pdev->dev,
			bcl_perph->dynamic_vbat_data_backup_count, sizeof(struct dynamic_vbat_data), GFP_KERNEL);
	if (!bcl_perph->dynamic_vbat_data_backup) {
		dev_err(&pdev->dev, "fail to alloc dynamic_vbat_data_backup memory\n");
		return -ENOMEM;
	}

	for (i = 0; i < bcl_perph->dynamic_vbat_data_backup_count; i++) {
		bcl_perph->dynamic_vbat_data_backup[i].temp = buf[i * 4 + 0];
		bcl_perph->dynamic_vbat_data_backup[i].vbat_mv_lv0 = buf[i * 4 + 1];
		bcl_perph->dynamic_vbat_data_backup[i].vbat_mv_lv1 = buf[i * 4 + 2];
		bcl_perph->dynamic_vbat_data_backup[i].vbat_mv_lv2 = buf[i * 4 + 3];
		dev_err(&pdev->dev, "dynamic_vbat_data_backup[%d]:temp=%d, lv0=%d, lv1=%d, lv2=%d\n", i,
			bcl_perph->dynamic_vbat_data_backup[i].temp,
			bcl_perph->dynamic_vbat_data_backup[i].vbat_mv_lv0,
			bcl_perph->dynamic_vbat_data_backup[i].vbat_mv_lv1,
			bcl_perph->dynamic_vbat_data_backup[i].vbat_mv_lv2);
	}

	return 0;
}

/**
 * bcl_load_dynamic_vbat_data_from_dts - Load dynamic vbat data from dts and store
 * @pdev: platform device pointer
 * @bcl_perph: BCL device pointer
 *
 * Call default and backup parameter loading interfaces
 *
 * Return: 0 on success, negative error code on failure
 */
static int bcl_load_dynamic_vbat_data_from_dts(struct platform_device *pdev,
					struct bcl_device *bcl_perph)
{
	int ret;
	struct device_node *dev_node = pdev->dev.of_node;

	/* Parse restore delays here (moved from core probe path) */
	if (of_property_read_u32(dev_node, "bcl,vbat_manual_restore_delay_ms",
				 (u32 *)&bcl_perph->vbat_manual_restore_delay_ms)) {
		bcl_perph->vbat_manual_restore_delay_ms = 60000; //60s
	}
	if (bcl_perph->vbat_manual_restore_delay_ms < 10000)
		bcl_perph->vbat_manual_restore_delay_ms = 10000; //10s
	if (bcl_perph->vbat_manual_restore_delay_ms > 60000)
		bcl_perph->vbat_manual_restore_delay_ms = 60000; //60s

	if (of_property_read_u32(dev_node, "bcl,vbat_auto_restore_delay_ms",
				 (u32 *)&bcl_perph->vbat_auto_restore_delay_ms)) {
		bcl_perph->vbat_auto_restore_delay_ms = 100000;
	}
	if (bcl_perph->vbat_auto_restore_delay_ms < 60000) //60s
		bcl_perph->vbat_auto_restore_delay_ms = 60000;
	if (bcl_perph->vbat_auto_restore_delay_ms > 120000) //120s
		bcl_perph->vbat_auto_restore_delay_ms = 120000;

	/* Load default parameters - this is required, failure should be reported */
	ret = bcl_load_default_vbat_data_from_dts(pdev, bcl_perph);
	if (ret) {
		bcl_perph->support_dynamic_vbat = false;
		dev_err(&pdev->dev, "Failed to load default vbat data, err:%d\n", ret);
		return ret;
	}
	/* Load backup parameters (optional, failure won't affect main flow) */
	ret = bcl_load_backup_vbat_data_from_dts(pdev, bcl_perph);
	if (ret) {
		dev_warn(&pdev->dev, "Failed to load backup vbat data (optional), err:%d\n", ret);
		/* Continue even if alt data load fails, as it's optional */
		return ret;
	}
	return 0;
}

void bcl_get_dynamic_vbat_data(struct platform_device *pdev,
				struct bcl_device *bcl_perph)
{
	int ret;

	bcl_perph->support_dynamic_vbat = of_property_read_bool(pdev->dev.of_node, "bcl,support_dynamic_vbat");
	dev_info(&pdev->dev, "bcl support_dynamic_vbat:%d, id:%d\n", bcl_perph->support_dynamic_vbat, bcl_perph->id);
	if (bcl_perph->support_dynamic_vbat) {
		ret = bcl_load_dynamic_vbat_data_from_dts(pdev, bcl_perph);
		if (ret) {
			dev_err(&pdev->dev, "Failed to load dynamic_vbat_data from dts, err:%d\n", ret);
		}
	}
}

void bcl_get_dynamic_vbat_cycle_compensation(struct platform_device *pdev,
					struct bcl_device *bcl_perph)
{
	int ret;
	struct device_node *dev_node = pdev->dev.of_node;
	int num_elem;
	int buf[64] = {0};
	int i;

	bcl_perph->support_dynamic_vbat_compensation = of_property_read_bool(dev_node, "bcl,support_dynamic_vbat_compensation");
	dev_info(&pdev->dev, "bcl support_dynamic_vbat_compensation:%d, pmic_type:%d\n", bcl_perph->support_dynamic_vbat_compensation, bcl_perph->pmic_type);
	if (bcl_perph->support_dynamic_vbat_compensation) {
		num_elem = of_property_count_elems_of_size(dev_node, "bcl,dynamic_vbat_data_compensation", sizeof(int));
		if (num_elem > 0) {
			if (num_elem % 4) {
				dev_err(&pdev->dev, "invalid len for dynamic_vbat_data_compensation\n");
				goto err_exit;
			}
			ret = of_property_read_u32_array(dev_node, "bcl,dynamic_vbat_data_compensation", (u32 *)buf, num_elem);
			if (ret) {
				dev_err(&pdev->dev, "dynamic_vbat_data_compensation read failed %d\n", ret);
				goto err_exit;
			}

			bcl_perph->dynamic_vbat_config_compensation_count = num_elem / 4;
			bcl_perph->dynamic_vbat_compensation_config = devm_kcalloc(&pdev->dev,
					bcl_perph->dynamic_vbat_config_compensation_count, sizeof(struct dynamic_vbat_data_compensation), GFP_KERNEL);
			if (!bcl_perph->dynamic_vbat_compensation_config) {
				dev_err(&pdev->dev, "fail to alloc dynamic_vbat_compensation_config memory\n");
				goto err_exit;
			}

			for (i = 0; i < bcl_perph->dynamic_vbat_config_compensation_count; i++) {
				bcl_perph->dynamic_vbat_compensation_config[i].temp = buf[i * 4 + 0];
				bcl_perph->dynamic_vbat_compensation_config[i].vbat_mv_200_500_compensation = buf[i * 4 + 1];
				bcl_perph->dynamic_vbat_compensation_config[i].vbat_mv_500_1000_compensation = buf[i * 4 + 2];
				bcl_perph->dynamic_vbat_compensation_config[i].vbat_mv_over_1000_compensation = buf[i * 4 + 3];
				dev_err(&pdev->dev, "dynamic_vbat_compensation_config[%d]:temp=%d, vbat_mv_200_500_compensation=%d, vbat_mv_500_1000_compensation=%d, vbat_mv_over_1000_compensation=%d\n", i,
					bcl_perph->dynamic_vbat_compensation_config[i].temp,
					bcl_perph->dynamic_vbat_compensation_config[i].vbat_mv_200_500_compensation,
					bcl_perph->dynamic_vbat_compensation_config[i].vbat_mv_500_1000_compensation,
					bcl_perph->dynamic_vbat_compensation_config[i].vbat_mv_over_1000_compensation);
			}
			return;
		}
	}

err_exit:
	bcl_perph->support_dynamic_vbat_compensation = false;
	return;
}
