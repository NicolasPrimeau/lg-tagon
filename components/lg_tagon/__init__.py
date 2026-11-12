"""ESPHome config for the LG Tag On component: a PN532 on I2C reading an LG dryer."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, i2c, sensor, text_sensor
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_RUNNING,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_MINUTE,
)

DEPENDENCIES = ["i2c"]
AUTO_LOAD = ["sensor", "text_sensor", "binary_sensor"]

lg_tagon_ns = cg.esphome_ns.namespace("lg_tagon")
LgTagOn = lg_tagon_ns.class_("LgTagOn", cg.PollingComponent, i2c.I2CDevice)

_minutes = sensor.sensor_schema(
    unit_of_measurement=UNIT_MINUTE,
    accuracy_decimals=0,
    device_class=DEVICE_CLASS_DURATION,
    state_class=STATE_CLASS_MEASUREMENT,
)
_code = sensor.sensor_schema(accuracy_decimals=0)
_diagnostic_code = sensor.sensor_schema(
    accuracy_decimals=0, entity_category=ENTITY_CATEGORY_DIAGNOSTIC
)

TEXT_SENSORS = {
    "status": text_sensor.text_sensor_schema(icon="mdi:tumble-dryer"),
    "model": text_sensor.text_sensor_schema(entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    "raw_monitoring": text_sensor.text_sensor_schema(
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC
    ),
}
SENSORS = {
    "status_code": _diagnostic_code,
    "remaining_time": _minutes,
    "initial_time": _minutes,
    "course": _code,
    "error_code": _diagnostic_code,
    "dry_level": _code,
    "temperature_setting": _code,
}
BINARY_SENSORS = {
    "running": binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_RUNNING),
    "reachable": binary_sensor.binary_sensor_schema(
        device_class=DEVICE_CLASS_CONNECTIVITY,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
}

SETTERS = {
    "status": "set_status_sensor",
    "model": "set_model_sensor",
    "raw_monitoring": "set_raw_sensor",
    "status_code": "set_status_code_sensor",
    "remaining_time": "set_remaining_sensor",
    "initial_time": "set_initial_sensor",
    "course": "set_course_sensor",
    "error_code": "set_error_sensor",
    "dry_level": "set_dry_level_sensor",
    "temperature_setting": "set_temperature_sensor",
    "running": "set_running_sensor",
    "reachable": "set_reachable_sensor",
}

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LgTagOn),
            **{cv.Optional(key): schema for key, schema in TEXT_SENSORS.items()},
            **{cv.Optional(key): schema for key, schema in SENSORS.items()},
            **{cv.Optional(key): schema for key, schema in BINARY_SENSORS.items()},
        }
    )
    .extend(cv.polling_component_schema("30s"))
    .extend(i2c.i2c_device_schema(0x24))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
    for key, setter in SETTERS.items():
        if key not in config:
            continue
        if key in TEXT_SENSORS:
            entity = await text_sensor.new_text_sensor(config[key])
        elif key in SENSORS:
            entity = await sensor.new_sensor(config[key])
        else:
            entity = await binary_sensor.new_binary_sensor(config[key])
        cg.add(getattr(var, setter)(entity))
