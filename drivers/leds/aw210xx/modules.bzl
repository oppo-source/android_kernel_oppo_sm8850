def register_modules(registry):
    registry.register(
        name = "drivers/leds/aw210xx/leds_aw210xx_algo",
        out = "leds_aw210xx_algo.ko",
        config = "CONFIG_LEDS_AW210XX",
        srcs = [
            # do not sort
            "drivers/leds/aw210xx/aw_breath_algorithm.c",
            "drivers/leds/aw210xx/aw_breath_algorithm.h",
            "drivers/leds/aw210xx/aw_lamp_interface.c",
            "drivers/leds/aw210xx/aw_lamp_interface.h",
            "drivers/leds/aw210xx/leds_aw210xx.c",
            "drivers/leds/aw210xx/leds_aw210xx.h",
            "drivers/leds/aw210xx/leds_aw210xx_reg.h",
        ],
        deps = [
            "//vendor/oplus/kernel/boot:oplus_bsp_bootmode",
        ],
    )
