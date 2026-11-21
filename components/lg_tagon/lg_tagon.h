// ESPHome component: polls an LG Tag On appliance through a PN532 over I2C and publishes
// its live state. The PN532 work runs as a state machine in loop(), so a slow FeliCa read
// (about 0.5 s) never blocks ESPHome.
#pragma once

#include <array>
#include <vector>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "protocol.h"

namespace esphome::lg_tagon {

class LgTagOn : public PollingComponent, public i2c::I2CDevice {
 public:
  void setup() override;
  void update() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_status_sensor(text_sensor::TextSensor *s) { status_ = s; }
  void set_model_sensor(text_sensor::TextSensor *s) { model_ = s; }
  void set_raw_sensor(text_sensor::TextSensor *s) { raw_ = s; }
  void set_status_code_sensor(sensor::Sensor *s) { status_code_ = s; }
  void set_remaining_sensor(sensor::Sensor *s) { remaining_ = s; }
  void set_initial_sensor(sensor::Sensor *s) { initial_ = s; }
  void set_course_sensor(sensor::Sensor *s) { course_ = s; }
  void set_error_sensor(sensor::Sensor *s) { error_ = s; }
  void set_field_sensor(::tagon::Field field, sensor::Sensor *s) { fields_[static_cast<size_t>(field)] = s; }
  void set_running_sensor(binary_sensor::BinarySensor *s) { running_ = s; }
  void set_door_lock_sensor(binary_sensor::BinarySensor *s) { door_lock_ = s; }
  void set_child_lock_sensor(binary_sensor::BinarySensor *s) { child_lock_ = s; }
  void set_reachable_sensor(binary_sensor::BinarySensor *s) { reachable_ = s; }

 protected:
  enum class Step { IDLE, POLL, READ_HEADER, READ_MODEL, READ_MONITORING };

  bool send_command_(const std::vector<uint8_t> &data);
  bool is_ready_();
  bool wait_ready_(uint32_t timeout_ms);
  bool read_response_(uint8_t command, std::vector<uint8_t> &out);
  bool configure_();
  void request_blocks_(Step step, uint8_t first, uint8_t count, uint8_t mode);
  void request_after_header_();
  void handle_poll_(const std::vector<uint8_t> &response);
  void handle_blocks_(const std::vector<uint8_t> &response);
  void finish_(bool reachable);
  void record_reachable_(bool answered);
  void publish_off_();
  void lose_link_(const char *why);
  void abort_();
  void publish_state_(const ::tagon::Monitoring &state);

  Step step_{Step::IDLE};
  uint32_t step_started_{0};
  uint32_t step_timeout_{0};
  uint32_t last_status_check_{0};
  uint8_t blocks_requested_{0};
  ::tagon::Idm idm_{};
  bool model_known_{false};
  ::tagon::Appliance appliance_{::tagon::Appliance::UNKNOWN};
  bool configured_{false};
  ::tagon::Reachability reachability_;

  text_sensor::TextSensor *status_{nullptr};
  text_sensor::TextSensor *model_{nullptr};
  text_sensor::TextSensor *raw_{nullptr};
  sensor::Sensor *status_code_{nullptr};
  sensor::Sensor *remaining_{nullptr};
  sensor::Sensor *initial_{nullptr};
  sensor::Sensor *course_{nullptr};
  sensor::Sensor *error_{nullptr};
  std::array<sensor::Sensor *, ::tagon::FIELD_COUNT> fields_{};
  binary_sensor::BinarySensor *running_{nullptr};
  binary_sensor::BinarySensor *door_lock_{nullptr};
  binary_sensor::BinarySensor *child_lock_{nullptr};
  binary_sensor::BinarySensor *reachable_{nullptr};
};

}  // namespace esphome::lg_tagon
