// PN532 driver for LG Tag On. Command and frame formats: NXP PN532 User Manual (UM0701-02).
#include "lg_tagon.h"

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <cstring>

namespace esphome::lg_tagon {

static const char *const TAG = "lg_tagon";

static constexpr uint8_t CMD_GET_FIRMWARE = 0x02;
static constexpr uint8_t CMD_SAM_CONFIGURATION = 0x14;
static constexpr uint8_t CMD_RF_CONFIGURATION = 0x32;
static constexpr uint8_t CMD_IN_DATA_EXCHANGE = 0x40;
static constexpr uint8_t CMD_IN_LIST_PASSIVE_TARGET = 0x4A;

static constexpr uint32_t READY_TIMEOUT_MS = 100;
static constexpr uint32_t POLL_TIMEOUT_MS = 1000;
// The tag answers a read in 154.62 ms x (1 + blocks); allow for retries.
static constexpr uint32_t READ_TIMEOUT_MS = 2500;
static constexpr size_t MAX_FRAME = 96;

bool LgTagOn::is_ready_() {
  uint8_t status = 0;
  return this->read(&status, 1) == i2c::ERROR_OK && (status & 0x01);
}

bool LgTagOn::send_command_(const std::vector<uint8_t> &data) {
  std::vector<uint8_t> frame = {0x00, 0x00, 0xFF};
  const uint8_t length = data.size() + 1;
  frame.push_back(length);
  frame.push_back(static_cast<uint8_t>(-length));
  frame.push_back(0xD4);
  uint8_t sum = 0xD4;
  for (uint8_t b : data) {
    frame.push_back(b);
    sum += b;
  }
  frame.push_back(static_cast<uint8_t>(-sum));
  frame.push_back(0x00);
  const auto written = this->write(frame.data(), frame.size());
  if (written != i2c::ERROR_OK) {
    ESP_LOGW(TAG, "Command 0x%02X: I2C write error %d", data[0], written);
    return false;
  }
  // The PN532 acknowledges every command before it starts working on it.
  if (!this->wait_ready_(READY_TIMEOUT_MS)) {
    uint8_t status = 0xEE;
    const auto err = this->read(&status, 1);
    ESP_LOGW(TAG, "Command 0x%02X: no acknowledgement (status read error %d, status 0x%02X)", data[0], err,
             status);
    return false;
  }
  uint8_t ack[7];
  const auto err = this->read(ack, sizeof(ack));
  static const uint8_t EXPECTED[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
  if (err != i2c::ERROR_OK || memcmp(ack + 1, EXPECTED, sizeof(EXPECTED)) != 0) {
    ESP_LOGW(TAG, "Command 0x%02X: bad acknowledgement (error %d): %s", data[0], err,
             format_hex_pretty(ack, sizeof(ack)).c_str());
    return false;
  }
  return true;
}

// Reads a response frame whose ready bit is already set. `out` gets the bytes after the
// response code.
bool LgTagOn::read_response_(uint8_t command, std::vector<uint8_t> &out) {
  uint8_t buf[MAX_FRAME + 1];
  if (this->read(buf, sizeof(buf)) != i2c::ERROR_OK) {
    return false;
  }
  // buf: status, 00 00 FF, LEN, LCS, D5, command + 1, data, DCS, 00
  const uint8_t *f = buf + 1;
  if (f[0] != 0x00 || f[1] != 0x00 || f[2] != 0xFF) {
    ESP_LOGW(TAG, "Bad frame preamble");
    return false;
  }
  const uint8_t length = f[3];
  if (static_cast<uint8_t>(length + f[4]) != 0 || length < 2 || length + 7u > MAX_FRAME) {
    ESP_LOGW(TAG, "Bad frame length");
    return false;
  }
  if (f[5] != 0xD5 || f[6] != command + 1) {
    ESP_LOGW(TAG, "Unexpected response 0x%02X to command 0x%02X", f[6], command);
    return false;
  }
  uint8_t sum = 0;
  for (uint8_t i = 0; i <= length; i++) {
    sum += f[5 + i];
  }
  if (sum != 0) {
    ESP_LOGW(TAG, "Bad frame checksum");
    return false;
  }
  out.assign(f + 7, f + 5 + length);
  return true;
}

// Busy-waits for the ready bit. Only for short commands, which answer within milliseconds.
bool LgTagOn::wait_ready_(uint32_t timeout_ms) {
  const uint32_t start = millis();
  while (!this->is_ready_()) {
    if (millis() - start > timeout_ms) {
      return false;
    }
    delay(1);
  }
  return true;
}

// Sending an ACK frame makes the PN532 drop whatever command it is still working on, so a
// timeout or a reboot of the ESP32 alone (the PN532 keeps its state) cannot leave it stuck.
void LgTagOn::abort_() {
  static const uint8_t ACK[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
  this->write(ACK, sizeof(ACK));
  delay(5);
  uint8_t discard[MAX_FRAME + 1];
  if (this->is_ready_()) {
    this->read(discard, sizeof(discard));
  }
}

bool LgTagOn::configure_() {
  this->abort_();
  const std::vector<std::vector<uint8_t>> commands = {
      {CMD_GET_FIRMWARE},
      // Normal mode, no virtual card timeout, IRQ pin used.
      {CMD_SAM_CONFIGURATION, 0x01, 0x00, 0x01},
      // MaxRetries: ATR 0xFF, PSL 1, passive activation 2, so a poll returns quickly when
      // the dryer is out of reach instead of retrying forever.
      {CMD_RF_CONFIGURATION, 0x05, 0xFF, 0x01, 0x02},
      // Timings: non-DEP timeout 0x0F = 1.64 s, longer than the tag's 0.46 s for 2 blocks.
      {CMD_RF_CONFIGURATION, 0x02, 0x00, 0x0B, 0x0F},
      // RF field off until the next poll.
      {CMD_RF_CONFIGURATION, 0x01, 0x00},
  };
  for (const auto &command : commands) {
    std::vector<uint8_t> response;
    if (!this->send_command_(command)) {
      return false;
    }
    if (!this->wait_ready_(READY_TIMEOUT_MS) || !this->read_response_(command[0], response)) {
      ESP_LOGW(TAG, "Command 0x%02X: no response", command[0]);
      return false;
    }
    if (command[0] == CMD_GET_FIRMWARE && response.size() >= 3) {
      ESP_LOGI(TAG, "PN5%02X firmware %u.%u", response[0], response[1], response[2]);
    }
  }
  return true;
}

void LgTagOn::setup() {
  // The first command wakes a sleeping PN532 and can fail. If both tries fail, update()
  // keeps trying, so a slow or busy PN532 at boot is not fatal.
  this->configured_ = this->configure_() || this->configure_();
  if (!this->configured_) {
    ESP_LOGW(TAG, "PN532 not responding yet; will retry");
  }
}

void LgTagOn::update() {
  if (this->step_ != Step::IDLE) {
    return;
  }
  if (!this->configured_) {
    this->configured_ = this->configure_();
    if (!this->configured_) {
      ESP_LOGW(TAG, "PN532 not responding; check wiring and that its switches select I2C");
      if (this->reachable_ != nullptr) {
        this->reachable_->publish_state(false);
      }
      return;
    }
    ESP_LOGI(TAG, "PN532 link restored");
  }
  // FeliCa at 212 kbit/s, polling any system code, asking for the system code back.
  if (!this->send_command_({CMD_IN_LIST_PASSIVE_TARGET, 0x01, 0x01, 0x00, 0xFF, 0xFF, 0x01, 0x00})) {
    this->lose_link_("poll not acknowledged");
    return;
  }
  this->step_ = Step::POLL;
  this->step_started_ = millis();
  this->step_timeout_ = POLL_TIMEOUT_MS;
}

void LgTagOn::request_blocks_(Step step, uint8_t first, uint8_t count, uint8_t mode) {
  std::vector<uint8_t> command = {CMD_IN_DATA_EXCHANGE, 0x01};
  uint8_t request[::tagon::read_request_size(::tagon::MAX_READ_BLOCKS)];
  const size_t n = ::tagon::build_read_request(request, this->idm_, first, count, mode);
  command.insert(command.end(), request, request + n);
  if (!this->send_command_(command)) {
    this->lose_link_("read not acknowledged");
    return;
  }
  this->blocks_requested_ = count;
  this->step_ = step;
  this->step_started_ = millis();
  this->step_timeout_ = READ_TIMEOUT_MS;
}

void LgTagOn::loop() {
  if (this->step_ == Step::IDLE) {
    return;
  }
  if (!this->is_ready_()) {
    if (millis() - this->step_started_ > this->step_timeout_) {
      this->lose_link_("timed out waiting for the appliance");
    }
    return;
  }
  const uint8_t command = this->step_ == Step::POLL ? CMD_IN_LIST_PASSIVE_TARGET : CMD_IN_DATA_EXCHANGE;
  std::vector<uint8_t> response;
  if (!this->read_response_(command, response)) {
    this->lose_link_("unreadable response");
    return;
  }
  if (this->step_ == Step::POLL) {
    this->handle_poll_(response);
  } else {
    this->handle_blocks_(response);
  }
}

void LgTagOn::handle_poll_(const std::vector<uint8_t> &response) {
  // NbTg, Tg, POL_RES length, 0x01, IDm[8], PMm[8], system code[2]
  if (response.size() < 12 || response[0] != 1) {
    ESP_LOGD(TAG, "No appliance in range");
    this->finish_(false);
    return;
  }
  std::copy(response.begin() + 4, response.begin() + 12, this->idm_.begin());
  ESP_LOGD(TAG, "Poll response: %s", format_hex_pretty(response.data(), response.size()).c_str());
  this->request_blocks_(Step::READ_HEADER, ::tagon::HEADER_BLOCK, 1, ::tagon::MEMORY_MODE);
}

// After the header, as the app does: the model once, then the live monitoring block.
void LgTagOn::request_after_header_() {
  if (this->model_known_) {
    this->request_blocks_(Step::READ_MONITORING, ::tagon::MONITORING_BLOCK, ::tagon::MONITORING_BLOCKS,
                          ::tagon::LIVE_MODE);
  } else {
    this->request_blocks_(Step::READ_MODEL, ::tagon::MODEL_BLOCK, ::tagon::MODEL_BLOCKS, ::tagon::MEMORY_MODE);
  }
}

void LgTagOn::handle_blocks_(const std::vector<uint8_t> &response) {
  // InDataExchange status, then the FeliCa response.
  if (response.empty() || (response[0] & 0x3F) != 0) {
    ESP_LOGW(TAG, "Read failed, PN532 status 0x%02X", response.empty() ? 0xFF : response[0]);
    this->finish_(false);
    return;
  }
  const auto blocks =
      ::tagon::parse_read_response(response.data() + 1, response.size() - 1, this->blocks_requested_);
  if (!blocks) {
    ESP_LOGW(TAG, "The appliance refused the read of %u block(s): %s", this->blocks_requested_,
             format_hex_pretty(response.data(), response.size()).c_str());
    this->finish_(false);
    return;
  }
  if (this->step_ == Step::READ_HEADER) {
    ESP_LOGD(TAG, "Header: %s", format_hex_pretty(blocks->data, blocks->size).c_str());
    this->request_after_header_();
    return;
  }
  if (this->step_ == Step::READ_MODEL) {
    const auto model = ::tagon::decode_model(blocks->data, blocks->size);
    if (model) {
      ESP_LOGI(TAG, "Appliance model code %s", model->c_str());
      this->model_known_ = true;
      if (this->model_ != nullptr) {
        this->model_->publish_state(*model);
      }
    }
    this->request_blocks_(Step::READ_MONITORING, ::tagon::MONITORING_BLOCK, ::tagon::MONITORING_BLOCKS,
                          ::tagon::LIVE_MODE);
    return;
  }
  const auto state = ::tagon::decode_dryer(blocks->data, blocks->size);
  if (this->raw_ != nullptr) {
    this->raw_->publish_state(format_hex_pretty(blocks->data, blocks->size));
  }
  if (!state) {
    ESP_LOGW(TAG, "Monitoring block failed its checksum");
    this->finish_(false);
    return;
  }
  this->publish_state_(*state);
  this->finish_(true);
}

void LgTagOn::publish_state_(const ::tagon::DryerState &state) {
  ESP_LOGD(TAG, "Status %u (%s), %u min left, course %u, error %u", state.status,
           ::tagon::dryer_status_name(state.status), state.remaining_minutes, state.course, state.error);
  if (this->status_ != nullptr) {
    this->status_->publish_state(::tagon::dryer_status_name(state.status));
  }
  if (this->status_code_ != nullptr) {
    this->status_code_->publish_state(state.status);
  }
  if (this->remaining_ != nullptr) {
    this->remaining_->publish_state(state.remaining_minutes);
  }
  if (this->initial_ != nullptr) {
    this->initial_->publish_state(state.initial_minutes);
  }
  if (this->course_ != nullptr) {
    this->course_->publish_state(state.course);
  }
  if (this->error_ != nullptr) {
    this->error_->publish_state(state.error);
  }
  if (this->dry_level_ != nullptr) {
    this->dry_level_->publish_state(state.dry_level);
  }
  if (this->temperature_ != nullptr) {
    this->temperature_->publish_state(state.temperature);
  }
  if (this->running_ != nullptr) {
    this->running_->publish_state(::tagon::dryer_is_running(state.status));
  }
}

void LgTagOn::finish_(bool reachable) {
  this->step_ = Step::IDLE;
  if (this->reachable_ != nullptr) {
    this->reachable_->publish_state(reachable);
  }
  // Turn the field off between polls; the next InListPassiveTarget turns it back on.
  std::vector<uint8_t> response;
  if (!this->send_command_({CMD_RF_CONFIGURATION, 0x01, 0x00}) || !this->wait_ready_(READY_TIMEOUT_MS) ||
      !this->read_response_(CMD_RF_CONFIGURATION, response)) {
    this->lose_link_("field off not confirmed");
  }
}

// The PN532 may still be mid-command or holding a reply: reset it on the next update.
void LgTagOn::lose_link_(const char *why) {
  ESP_LOGW(TAG, "PN532 exchange failed (%s); resetting it", why);
  this->step_ = Step::IDLE;
  this->configured_ = false;
  if (this->reachable_ != nullptr) {
    this->reachable_->publish_state(false);
  }
}

void LgTagOn::dump_config() {
  ESP_LOGCONFIG(TAG, "LG Tag On:");
  LOG_I2C_DEVICE(this);
  LOG_UPDATE_INTERVAL(this);
  if (!this->configured_) {
    ESP_LOGW(TAG, "  PN532 not responding; check wiring and that its switches select I2C");
  }
}

}  // namespace esphome::lg_tagon
