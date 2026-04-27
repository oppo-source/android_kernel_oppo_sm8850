/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Internal header file for bcl_pmic5 driver.
 * Contains structure and enum definitions shared between bcl_pmic5.c and oplus_bcl.c
 */

#ifndef _BCL_PMIC5_INTERNAL_H_
#define _BCL_PMIC5_INTERNAL_H_

#include <linux/types.h>
#include <linux/device.h>
#include <linux/regmap.h>
#include <linux/thermal.h>
#include <linux/notifier.h>
#include <linux/workqueue.h>
#include <linux/proc_fs.h>


/* Enums */
enum bcl_dev_type {
	BCL_IBAT_LVL0,
	BCL_IBAT_LVL1,
	BCL_VBAT_LVL0,
	BCL_VBAT_LVL1,
	BCL_VBAT_LVL2,
	BCL_LVL0,
	BCL_LVL1,
	BCL_LVL2,
	BCL_2S_IBAT_LVL0,
	BCL_2S_IBAT_LVL1,
	BCL_TYPE_MAX,
};

enum bcl_monitor_type {
	BCL_MON_DEFAULT,
	BCL_MON_VBAT_ONLY,
	BCL_MON_IBAT_ONLY,
	BCL_MON_MAX,
};

enum {
	BCLBIG_COMP_VCMP_L0_THR,
	BCLBIG_COMP_VCMP_L1_THR,
	BCLBIG_COMP_VCMP_L2_THR,
	REG_MAX,
};

struct bcl_desc {
	bool vadc_type;
	u32 vbat_regs[REG_MAX];
	bool vbat_zone_enabled;
	u32 vcmp_thresh_base;
	u32 vcmp_thresh_max;
	u32 ibat_scaling_factor;
	u32 ibat_thresh_scaling_factor;
};

#ifdef OPLUS_FEATURE_CHG_BASIC
/* Forward declarations - full definitions are in oplus_bcl.h */
struct dynamic_vbat_data;
struct dynamic_vbat_data_compensation;
struct thermal_zone_device;
struct thermal_trip;
struct power_supply;
/* Global variable - defined in bcl_pmic5.c */
extern struct blocking_notifier_head bcl_pmic5_notifier;
/* struct timeval definition - used by both native and OPLUS code */
struct timeval {
	long tv_sec;
	long tv_usec;
};
#endif

struct bcl_device;

struct bcl_peripheral_data {
	int                     irq_num;
	int                     status_bit_idx;
	long			trip_thresh;
	int                     last_val;
	int                     def_vbat_min_thresh;
	struct mutex            state_trans_lock;
	bool			irq_enabled;
	bool			irq_freed;
	enum bcl_dev_type	type;
	struct thermal_zone_device_ops ops;
	struct thermal_zone_device *tz_dev;
	struct bcl_device	*dev;
};

struct bcl_device {
	struct device			*dev;
	struct regmap			*regmap;
	uint16_t			fg_bcl_addr;
	uint8_t				dig_major;
	uint8_t				dig_minor;
	uint8_t				bcl_param_1;
	uint8_t				bcl_type;
	void				*ipc_log;
	int				bcl_monitor_type;
	bool				ibat_ccm_enabled;
	bool				ibat_ccm_lando_enabled;
	bool				ibat_use_qg_adc;
	bool				no_bit_shift;
	uint32_t			ibat_ext_range_factor;
	struct bcl_peripheral_data	param[BCL_TYPE_MAX];
	const struct bcl_desc		*desc;
	struct notifier_block		nb;
#ifdef OPLUS_FEATURE_CHG_BASIC
	bool				support_track;
	int				id;
	bool				support_dynamic_vbat;
	bool			support_dynamic_vbat_compensation;
	struct dynamic_vbat_data	*dynamic_vbat_config;
	int				dynamic_vbat_config_count;
	struct dynamic_vbat_data_compensation *dynamic_vbat_compensation_config;
	int				dynamic_vbat_config_compensation_count;
	struct dynamic_vbat_data	*dynamic_vbat_data_backup;
	int				dynamic_vbat_data_backup_count;
	struct notifier_block		psy_nb;
	struct work_struct		vbat_check_work;
	struct delayed_work		vbat_manual_restore_work;
	struct delayed_work		vbat_auto_restore_work;
	bool				vbat_manual_restore_triggered;
	bool				vbat_auto_restore_triggered;
	struct proc_dir_entry		*dynamic_vbat_proc_entry;
	struct mutex			dynamic_vbat_lock;
	int				prev_temp_compensation_range;
	int				prev_cycle_compensation_range;
	int				vbat_manual_restore_delay_ms;
	int				vbat_auto_restore_delay_ms;
	struct power_supply		*batt_psy;
	int				pmic_type;
	int				dynamic_vbat_pre_range;
#endif
};

#ifdef OPLUS_FEATURE_CHG_BASIC
/* Function declarations - functions defined in bcl_pmic5.c */
int bcl_write_register(struct bcl_device *bcl_perph, int16_t reg_offset, uint8_t data);
void convert_vbat_to_vcmp_val(const struct bcl_desc *desc, int vbat, int *val);
int bcl_write_vbat_tz(struct thermal_zone_device *tzd, const struct thermal_trip *trip, int temp);
#endif

#endif /* _BCL_PMIC5_INTERNAL_H_ */

