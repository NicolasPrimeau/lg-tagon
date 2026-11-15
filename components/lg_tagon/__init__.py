"""ESPHome config for the LG Tag On component: a PN532 on I2C reading an LG washer or dryer.

The component tells the machine from the model code on its tag; settings that do not apply to
that machine stay unpublished.
"""

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
Field = cg.global_ns.namespace("tagon").enum("Field", is_class=True)
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
    "status": text_sensor.text_sensor_schema(),
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
}
# Machine-specific settings, as the app's index numbers.
FIELDS = {
    "dry_level": "DRY_LEVEL",
    "temperature_setting": "DRYER_TEMPERATURE",
    "soil_level": "SOIL_LEVEL",
    "spin_speed": "SPIN",
    "water_temperature": "WATER_TEMPERATURE",
    "rinse": "RINSE",
}
BINARY_SENSORS = {
    "running": binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_RUNNING),
    # Home Assistant's lock class reads on as "Unlocked", so the firmware publishes "locked".
    "door_lock": binary_sensor.binary_sensor_schema(icon="mdi:lock"),
    "child_lock": binary_sensor.binary_sensor_schema(icon="mdi:lock-outline"),
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
    "running": "set_running_sensor",
    "door_lock": "set_door_lock_sensor",
    "child_lock": "set_child_lock_sensor",
    "reachable": "set_reachable_sensor",
}

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LgTagOn),
            **{cv.Optional(key): schema for key, schema in TEXT_SENSORS.items()},
            **{cv.Optional(key): schema for key, schema in SENSORS.items()},
            **{cv.Optional(key): _code for key in FIELDS},
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
    for key, field in FIELDS.items():
        if key in config:
            entity = await sensor.new_sensor(config[key])
            cg.add(var.set_field_sensor(getattr(Field, field), entity))
