// LG Tag On protocol: the FeliCa frames and LG messages the 2016 "LG Smart Laundry&DW"
// app (com.lg.apps.lglaundry 4.8.6) exchanges with the appliance. Pure code, no ESPHome,
// so it is unit tested on the host. Findings and sources: docs/protocol.md.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace tagon {

constexpr size_t IDM_SIZE = 8;
constexpr size_t BLOCK_SIZE = 16;
using Idm = std::array<uint8_t, IDM_SIZE>;

// Block addresses the app reads. It reads the header first on every tag-on.
constexpr uint8_t HEADER_BLOCK = 0x08;
constexpr uint8_t MODEL_BLOCK = 0x17;
constexpr uint8_t MODEL_BLOCKS = 2;
constexpr uint8_t MODEL_TYPE = 0x15;
constexpr uint8_t MONITORING_BLOCK = 0x23;
constexpr uint8_t MONITORING_BLOCKS = 2;
constexpr uint8_t MONITORING_TYPE = 0x23;
constexpr size_t MONITORING_PAYLOAD = 28;

// FeliCa Read Without Encryption, on the service the app uses.
constexpr uint8_t FELICA_READ = 0x06;
constexpr uint8_t FELICA_READ_RESPONSE = 0x07;
constexpr uint16_t SERVICE_CODE = 0x0009;
constexpr size_t MAX_READ_BLOCKS = 4;  // the tag's attribute block: "Maximum blocks to read: 4"

// The third byte of each block list element (NfcFPacket.mode_RF in the app). The app sends 0
// for the tag's stored data (header, model, history) and 4 for the appliance's live data
// (command IDs 500 and up: monitoring, options, energy). The dryer refuses a mismatch with
// status FF A5.
constexpr uint8_t MEMORY_MODE = 0x00;
constexpr uint8_t LIVE_MODE = 0x04;

// LG's message checksum: CRC-16/XMODEM (poly 0x1021, init 0), stored low byte first.
inline uint16_t crc16(const uint8_t *data, size_t size) {
  uint16_t crc = 0;
  for (size_t i = 0; i < size; i++) {
    uint8_t x = static_cast<uint8_t>((crc >> 8) ^ data[i]);
    x ^= x >> 4;
    crc = static_cast<uint16_t>((crc << 8) ^ (x << 12) ^ (x << 5) ^ x);
  }
  return crc;
}

// An LG message is [type][length][payload: length bytes][crc low][crc high].
struct Message {
  const uint8_t *payload;
  uint8_t size;
};

inline std::optional<Message> parse_message(const uint8_t *data, size_t size, uint8_t type) {
  if (size < 4 || data[0] != type) {
    return std::nullopt;
  }
  const uint8_t length = data[1];
  if (static_cast<size_t>(length) + 4 > size) {
    return std::nullopt;
  }
  const uint16_t crc = crc16(data, length + 2);
  if (data[length + 2] != (crc & 0xFF) || data[length + 3] != (crc >> 8)) {
    return std::nullopt;
  }
  return Message{data + 2, length};
}

// The FeliCa request, byte for byte as the app builds it: three-byte block list elements.
inline size_t build_read_request(uint8_t *out, const Idm &idm, uint8_t first_block, uint8_t count,
                                 uint8_t mode) {
  size_t n = 0;
  out[n++] = 0;  // length, set below
  out[n++] = FELICA_READ;
  for (uint8_t b : idm) {
    out[n++] = b;
  }
  out[n++] = 1;  // one service
  out[n++] = SERVICE_CODE & 0xFF;
  out[n++] = SERVICE_CODE >> 8;
  out[n++] = count;
  for (uint8_t i = 0; i < count; i++) {
    out[n++] = 0x00;  // three-byte element, service index 0
    out[n++] = static_cast<uint8_t>(first_block + i);
    out[n++] = mode;
  }
  out[0] = static_cast<uint8_t>(n);
  return n;
}

constexpr size_t read_request_size(uint8_t count) { return 14 + 3 * static_cast<size_t>(count); }

// A FeliCa read response is [length][0x07][IDm][status 1][status 2][block count][blocks].
// Returns the block data, or nothing if the tag reported an error.
struct Blocks {
  const uint8_t *data;
  size_t size;
};

inline std::optional<Blocks> parse_read_response(const uint8_t *data, size_t size, uint8_t count) {
  constexpr size_t header = 13;
  if (size < header || data[1] != FELICA_READ_RESPONSE || data[10] != 0 || data[11] != 0) {
    return std::nullopt;
  }
  if (data[12] != count || size < header + count * BLOCK_SIZE) {
    return std::nullopt;
  }
  return Blocks{data + header, count * BLOCK_SIZE};
}

// Dryer status codes (USVictor27ListStatus_Dryer in the app).
inline const char *dryer_status_name(uint8_t status) {
  static const char *const NAMES[] = {
      "Off",   "Initial", "Pause",         "Dry",                   "Cooling",           "Steam",
      "Mist",  "Wrinkle care", "End",      "Error",                 "Install check",     "Duct clogged",
      "Firmware download", "Cycle download", "Smart diagnosis", "Smart grid delay", "Smart grid reduced",
  };
  if (status < sizeof(NAMES) / sizeof(NAMES[0])) {
    return NAMES[status];
  }
  return "Unknown";
}

// Drying, cooling, steam and mist: the drum is working on the load. Wrinkle care runs after
// the cycle has finished, so it does not count.
inline bool dryer_is_running(uint8_t status) { return status >= 3 && status <= 6; }

// The live monitoring block (WM_MonitoringData in the app), dryer field layout.
struct DryerState {
  uint8_t status;
  uint16_t remaining_minutes;
  uint16_t initial_minutes;
  uint8_t course;
  uint8_t error;
  uint8_t dry_level;
  uint8_t temperature;
  uint8_t time_dry;
};

inline std::optional<DryerState> decode_dryer(const uint8_t *data, size_t size) {
  const auto message = parse_message(data, size, MONITORING_TYPE);
  if (!message || message->size < 10) {
    return std::nullopt;
  }
  const uint8_t *p = message->payload;
  return DryerState{
      p[0], static_cast<uint16_t>(p[1] * 60 + p[2]), static_cast<uint16_t>(p[3] * 60 + p[4]),
      p[5], p[6], p[7], p[8], p[9],
  };
}

// The model message holds LG's internal model code, NUL padded.
inline std::optional<std::string> decode_model(const uint8_t *data, size_t size) {
  const auto message = parse_message(data, size, MODEL_TYPE);
  if (!message) {
    return std::nullopt;
  }
  size_t end = 0;
  while (end < message->size && message->payload[end] != 0) {
    end++;
  }
  return std::string(reinterpret_cast<const char *>(message->payload), end);
}

}  // namespace tagon
