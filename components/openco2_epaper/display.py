"""SSD1681 (1.54" 200x200) e-paper driver for the OpenCO2 Sensor.

Unlike ESPHome's waveshare_epaper, this driver keeps the last image in RTC
memory and writes it to the controller's "previous image" RAM before every
partial refresh, so partial refreshes work after the panel was reset, put to
sleep or powered off - including across the ESP32's deep sleep.
"""

from esphome import pins
import esphome.codegen as cg
from esphome.components import display, spi
import esphome.config_validation as cv
from esphome.const import (
    CONF_BUSY_PIN,
    CONF_DC_PIN,
    CONF_ID,
    CONF_LAMBDA,
    CONF_PAGES,
    CONF_RESET_PIN,
)

DEPENDENCIES = ["spi"]

CONF_FULL_UPDATE_INTERVAL = "full_update_interval"

openco2_epaper_ns = cg.esphome_ns.namespace("openco2_epaper")
OpenCO2EPaper = openco2_epaper_ns.class_(
    "OpenCO2EPaper", cg.PollingComponent, spi.SPIDevice, display.DisplayBuffer
)

CONFIG_SCHEMA = cv.All(
    display.FULL_DISPLAY_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(OpenCO2EPaper),
            cv.Required(CONF_DC_PIN): pins.gpio_output_pin_schema,
            cv.Required(CONF_RESET_PIN): pins.gpio_output_pin_schema,
            cv.Required(CONF_BUSY_PIN): pins.gpio_input_pin_schema,
            # A full (flashing) refresh at most this often; everything in
            # between is a partial refresh.
            cv.Optional(
                CONF_FULL_UPDATE_INTERVAL, default="2h"
            ): cv.positive_time_period_seconds,
        }
    )
    .extend(cv.polling_component_schema("never"))
    .extend(spi.spi_device_schema()),
    cv.has_at_most_one_key(CONF_PAGES, CONF_LAMBDA),
)

FINAL_VALIDATE_SCHEMA = spi.final_validate_device_schema(
    "openco2_epaper", require_miso=False, require_mosi=True
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await display.register_display(var, config)
    await spi.register_spi_device(var, config, write_only=True)

    cg.add(var.set_dc_pin(await cg.gpio_pin_expression(config[CONF_DC_PIN])))
    cg.add(var.set_reset_pin(await cg.gpio_pin_expression(config[CONF_RESET_PIN])))
    cg.add(var.set_busy_pin(await cg.gpio_pin_expression(config[CONF_BUSY_PIN])))
    cg.add(
        var.set_full_update_interval(
            config[CONF_FULL_UPDATE_INTERVAL].total_seconds
        )
    )

    if CONF_LAMBDA in config:
        lambda_ = await cg.process_lambda(
            config[CONF_LAMBDA], [(display.DisplayRef, "it")], return_type=cg.void
        )
        cg.add(var.set_writer(lambda_))
