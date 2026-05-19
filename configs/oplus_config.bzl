# OPLUS platform-specific kernel configurations
# Each platform (canoe, sun, vienna, etc.) has its own perf and consolidate configs

# Common OPLUS configs shared across all platforms
_common_oplus_perf_config = {
# OPLUS_FEATURE_TP_BASIC
    "CONFIG_LEDS_QCOM_LPG": "n",
    "CONFIG_LEDS_AW210XX": "m",
# OPLUS_FEATURE_TP_BASIC end
# if OPLUS_FEATURE_CHG_BASIC
    "CONFIG_OPLUS_CHG_V2": "m",
    "CONFIG_OPLUS_CHARGER_DDK_BUILD": "y",
    "CONFIG_OPLUS_CHARGER": "y",
    "CONFIG_OPLUS_ADSP_CHARGER": "y",
    "CONFIG_DISABLE_OPLUS_FUNCTION": "n",
    "CONFIG_OPLUS_CHIP_SOC_NODE": "y",
    "CONFIG_OPLUS_SHIP_MODE_SUPPORT": "y",
    "CONFIG_OPLUS_SMART_CHARGER_SUPPORT": "y",
    "CONFIG_OPLUS_CHG_DRM_PANEL_NOTIFY": "y",
    "CONFIG_OPLUS_CALL_MODE_SUPPORT": "y",
    "CONFIG_OPLUS_CHECK_CHARGERID_VOLT": "y",
    "CONFIG_OPLUS_CHG_DYNAMIC_CONFIG": "y",
    "CONFIG_OPLUS_CHG_TEST_KIT": "m",
    "CONFIG_OPLUS_RTC_DET_SUPPORT": "y",
    "CONFIG_OPLUS_CHG_IC_DEBUG": "y",
    "CONFIG_OPLUS_CHG_MMS_DEBUG": "y",
    "CONFIG_OPLUS_WIRELESS_NU1669": "y",
    "CONFIG_OPLUS_WIRELESS_SC96257": "y",
    "CONFIG_OPLUS_CHG_VOOCPHY": "y",
    "CONFIG_OPLUS_CHG_VOOCPHY_CHGLIB": "y",
    "CONFIG_OPLUS_CHG_AP_VOOCPHY": "y",
    "CONFIG_OPLUS_CHG_ADSP_VOOCPHY": "y",
    "CONFIG_OPLUS_SY6603_BATT_BAL": "y",
    "CONFIG_OPLUS_SC7637_LEVEL_SHIFT": "y",
    "CONFIG_OPLUS_VOOCPHY_SC8517": "y",
    "CONFIG_OPLUS_AUDIO_SWITCH_GLINK": "y",
    "CONFIG_OPLUS_DYNAMIC_CONFIG": "m",
    "CONFIG_OPLUS_DYNAMIC_CONFIG_CHARGER": "y",
    "CONFIG_OPLUS_HL7603_CHARGER": "y",
    "CONFIG_OPLUS_GAUGE_NFG8011B": "y",
    "CONFIG_OPLUS_STATE_RETENTION": "y",
    "CONFIG_OPLUS_MAGCVR_NOTIFY": "y",
    "CONFIG_OPLUS_PHY_SC8547D": "y",
    "CONFIG_OPLUS_BOOST_SC83107": "y",
    "CONFIG_OPLUS_UFCS_CLASS": "m",
    "CONFIG_OPLUS_CHG_RECOVERY": "y",
    "CONFIG_OPLUS_CHG_STATE_KEEP": "y",
    "CONFIG_OPLUS_CHG_STATE_KEEP_DCP": "y",
    "CONFIG_OPLUS_CHARGER_MAXIM": "y",
    "CONFIG_OPLUS_WIRELESS_PEN": "m",
    "CONFIG_OPLUS_DEBUG_AUTH": "y",
# OPLUS_FEATURE_CHG_BASIC end
    "CONFIG_OPLUS_FEATURE_GEAS": "m",
    "CONFIG_OPLUS_FEATURE_GEAS_CPU": "m",
    "CONFIG_OPLUS_FEATURE_GEAS_FDRIVE": "m",
    "CONFIG_OPLUS_FEATURE_GEAS_GPU": "m",
    "CONFIG_OPLUS_FEATURE_GEAS_BWMON": "m",
    "CONFIG_OPLUS_FEATURE_GEAS_MEMLAT": "m",
# if OPLUS_FEATURE_CPU
    "CONFIG_OPLUS_FEATURE_SCHED_ASSIST": "m",
    "CONFIG_OPLUS_FEATURE_EAS_OPT": "m",
    "CONFIG_OPLUS_SCHED_GROUP_OPT": "y",
    "CONFIG_OPLUS_CPU_AUDIO_PERF": "y",
    "CONFIG_OPLUS_FEATURE_LOADBALANCE": "y",
    "CONFIG_OPLUS_FEATURE_PIPELINE": "y",
    "CONFIG_OPLUS_FEATURE_CPU_JANKINFO": "m",
    "CONFIG_OPLUS_FEATURE_SCHED_DDL": "y",
    "CONFIG_OPLUS_ADD_CORE_CTRL_MASK": "y",
    "CONFIG_OPLUS_SCHED_HALT_MASK_PRT": "y",
    "CONFIG_OPLUS_FEATURE_BAN_APP_SET_AFFINITY": "y",
    "CONFIG_OPLUS_FEATURE_FRAME_BOOST": "m",
    "CONFIG_OPLUS_FEATURE_SCHED_CFBT": "y",
    "CONFIG_OPLUS_FEATURE_GKI_CPUFREQ_BOUNCING": "m",
    "CONFIG_OPLUS_FEATURE_CEILING_FREE": "y",
    "CONFIG_OPLUS_PROCS_LOAD_STATE": "n",
    "CONFIG_OPLUS_FEATURE_ABNORMAL_FLAG": "m",
    "CONFIG_OPLUS_FEATURE_UCLAMP": "m",
# OPLUS_FEATURE_CPU end
    "CONFIG_OPLUS_FEATURE_MM_BOOSTPOOL": "y",
    "CONFIG_OPLUS_FEATURE_MM_TA_CMA_RSV": "y",
    "CONFIG_OPLUS_FEATURE_MM_OSVELTE": "m",
    "CONFIG_OPLUS_RPMH_QCOM": "y",
    "CONFIG_OPLUS_FEATURE_FEEDBACK": "m",
    "CONFIG_OPLUS_POWERINFO_STANDBY_DEBUG": "y",
    "CONFIG_OPLUS_FEATURE_STANDBY_NETLINK_CLOCK": "y",
    "CONFIG_OPLUS_FEATURE_STANDBY_NETLINK_REGULATOR": "y",
    "CONFIG_OPLUS_FEATURE_STANDBY_NETLINK_SMP2P": "y",
# if OPLUS_FEATURE_SCHED_EXT
    "CONFIG_OPLUS_FEATURE_SCHED_EXT": "y",
# OPLUS_FEATURE_SCHED_EXT end
    "CONFIG_OPLUS_FEATURE_AIZEROCOPY": "m",
# if OPLUS_POGO_KEYBOARD
    "CONFIG_OPLUS_POGOPIN_FUNCTION": "y",
# OPLUS_POGO_KEYBOARD end
    "CONFIG_OPLUS_FEATURE_TRACE_SENSOR_ERR": "m",
    "CONFIG_OPLUS_SYSTEM_KERNEL_QCOM": "y",
}

_common_oplus_consolidate_config = {
# if OPLUS_FEATURE_CHG_BASIC
    "CONFIG_OPLUS_FEATURE_CHG_IC_VIRTUAL": "y",
# OPLUS_FEATURE_CHG_BASIC end
# if OPLUS_FEATURE_CPU
    "CONFIG_OPLUS_FEATURE_SCHED_ASSIST": "m",
    "CONFIG_OPLUS_FEATURE_EAS_OPT": "m",
    "CONFIG_OPLUS_SCHED_GROUP_OPT": "y",
    "CONFIG_OPLUS_CPU_AUDIO_PERF": "y",
    "CONFIG_OPLUS_FEATURE_LOADBALANCE": "y",
    "CONFIG_OPLUS_FEATURE_PIPELINE": "y",
    "CONFIG_OPLUS_FEATURE_CPU_JANKINFO": "m",
    "CONFIG_OPLUS_FEATURE_SCHED_DDL": "y",
    "CONFIG_OPLUS_ADD_CORE_CTRL_MASK": "y",
    "CONFIG_OPLUS_SCHED_HALT_MASK_PRT": "y",
    "CONFIG_OPLUS_FEATURE_BAN_APP_SET_AFFINITY": "y",
    "CONFIG_OPLUS_FEATURE_FRAME_BOOST": "m",
    "CONFIG_OPLUS_FEATURE_SCHED_CFBT": "y",
    "CONFIG_OPLUS_FEATURE_GKI_CPUFREQ_BOUNCING": "m",
    "CONFIG_OPLUS_FEATURE_CEILING_FREE": "y",
    "CONFIG_OPLUS_PROCS_LOAD_STATE": "n",
    "CONFIG_OPLUS_FEATURE_ABNORMAL_FLAG": "m",
    "CONFIG_OPLUS_FEATURE_UCLAMP": "m",
# OPLUS_FEATURE_CPU end
# if OPLUS_FEATURE_SCHED_EXT
    "CONFIG_OPLUS_FEATURE_SCHED_EXT": "y",
# OPLUS_FEATURE_SCHED_EXT end
# if OPLUS_POGO_KEYBOARD
    "CONFIG_OPLUS_POGOPIN_FUNCTION": "y",
# OPLUS_POGO_KEYBOARD end
}

# Platform-specific OPLUS perf configs
_oplus_perf_configs = {
    "canoe": _common_oplus_perf_config | {
        # Add canoe-specific configs here if needed
    },
    "sun": _common_oplus_perf_config | {
        # Add sun-specific configs here if needed
    },
    "vienna": _common_oplus_perf_config | {
        # Add vienna-specific configs here if needed
    },
}

# Platform-specific OPLUS consolidate configs
_oplus_consolidate_configs = {
    "canoe": _common_oplus_consolidate_config | {
        # Add canoe-specific configs here if needed
    },
    "sun": _common_oplus_consolidate_config | {
        # Add sun-specific configs here if needed
    },
    "vienna": _common_oplus_consolidate_config | {
        # Add vienna-specific configs here if needed
    },
}

def get_oplus_perf_config(platform):
    """Get OPLUS perf config for the specified platform.
    
    Args:
        platform: Platform name (e.g., "canoe", "sun", "vienna")
    
    Returns:
        Dictionary of OPLUS perf configurations for the platform.
        Returns empty dict if platform is not found.
    """
    return _oplus_perf_configs.get(platform, {})

def get_oplus_consolidate_config(platform):
    """Get OPLUS consolidate config for the specified platform.
    
    Args:
        platform: Platform name (e.g., "canoe", "sun", "vienna")
    
    Returns:
        Dictionary of OPLUS consolidate configurations for the platform.
        Returns empty dict if platform is not found.
    """
    return _oplus_consolidate_configs.get(platform, {})

