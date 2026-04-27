/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef _OPLUS_BCL_PMIC5_H_
#define _OPLUS_BCL_PMIC5_H_

#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/notifier.h>
#include <linux/proc_fs.h>
#include <linux/types.h>

struct bcl_device;
struct file;
struct timeval;

/* OPLUS specific enums and structures */

enum {
	PMIC_SUBTYPE_PMH0101,
	PMIC_SUBTYPE_PMIH010X,
	PMIC_SUBTYPE_PM8550,
	PMIC_SUBTYPE_MAX,
};

#define BCL_VBAT_CYCLE_THRESH_LOW	200
#define BCL_VBAT_CYCLE_THRESH_MID	500
#define BCL_VBAT_CYCLE_THRESH_HIGH	1000

/* Cycle compensation range indices */
#define BCL_CYCLE_COMPENSATION_RANGE_LOW        0   /* cycle < 200 */
#define BCL_CYCLE_COMPENSATION_RANGE_MID_LOW    1   /* 200 <= cycle < 500 */
#define BCL_CYCLE_COMPENSATION_RANGE_MID_HIGH   2   /* 500 <= cycle < 1000 */
#define BCL_CYCLE_COMPENSATION_RANGE_HIGH       3   /* cycle >= 1000 */

struct dynamic_vbat_data {
	int temp;
	int vbat_mv_lv0;
	int vbat_mv_lv1;
	int vbat_mv_lv2;
};

struct dynamic_vbat_data_compensation {
    int temp;
    int vbat_mv_200_500_compensation;
    int vbat_mv_500_1000_compensation;
    int vbat_mv_over_1000_compensation;
};


/* Global variables */
extern int BCL_LEVEL0_COUNT;
extern int BCL_LEVEL1_COUNT;
extern int BCL_LEVEL2_COUNT;
extern struct proc_dir_entry *oplus_bcl_stat;

/* Function declarations - only functions called from bcl_pmic5.c */
int get_pmic_subtype(struct bcl_device *bcl_perph);
void do_gettimeofday(struct timeval *tv);
int battery_supply_callback(struct notifier_block *nb, unsigned long event, void *data);
void bcl_vbat_check(struct work_struct *work);
void pmh0101_bcl_vbat_check(struct work_struct *work);
void bcl_restore_vbat_config_from_backup(struct work_struct *work);
void bcl_auto_restore_vbat_config_from_backup(struct work_struct *work);
void bcl_get_dynamic_vbat_data(struct platform_device *pdev, struct bcl_device *bcl_perph);
void bcl_get_dynamic_vbat_cycle_compensation(struct platform_device *pdev, struct bcl_device *bcl_perph);

/* proc_ops structures */
extern const struct proc_ops proc_bcl_count;
extern const struct proc_ops dynamic_vbat_proc_ops;

/* OPLUS-specific probe initialization */
void oplus_bcl_probe_init(struct platform_device *pdev, struct bcl_device *bcl_perph);


#endif /* _OPLUS_BCL_PMIC5_H_ */

