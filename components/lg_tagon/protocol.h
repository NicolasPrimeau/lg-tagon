// LG Tag On protocol: the FeliCa frames and LG messages the 2016 "LG Smart Laundry&DW"
// app (com.lg.apps.lglaundry 4.8.6) exchanges with the appliance. Pure code, no ESPHome,
// so it is unit tested on the host. Findings and sources: docs/protocol.md.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace tagon {

constexpr size_t IDM_SIZE = 8;
constexpr size_t BLOCK_SIZE = 16;
using Idm = std::array<uint8_t, IDM_SIZE>;

// Block addresses the app reads. It reads the header first on every tag-on.
constexpr uint8_t HEADER_BLOCK = 0x08;
constexpr uint8_t MODEL_BLOCK = 0x17;
constexpr uint8_t MODEL_BLOCKS = 2;
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

// The header block (CommonCommand.get_DataHeader): starts 83 76, byte 3 is 3, byte 5 is the
// device state. NFCProtocol.isPowerOn/isErrorState: when its high nibble is 3, bit 1 means
// powered on and bit 0 an error; other values are on unless F3, and in error if F4. While
// the machine is off its tag still answers this block, but every other read fails (FF 50).
struct Header {
  bool powered;
  bool error;
};

inline std::optional<Header> parse_header(const uint8_t *data, size_t size) {
  if (size < BLOCK_SIZE || data[0] != 0x83 || data[1] != 0x76 || data[3] != 3) {
    return std::nullopt;
  }
  const uint8_t state = data[5];
  if ((state & 0xF0) == 0x30) {
    return Header{(state & 0x02) != 0, (state & 0x01) != 0};
  }
  return Header{state != 0xF3, state == 0xF4};
}

// Which machine the tag belongs to, from its model code: the app sends "RV13..._D_US" codes to
// its dryer tables and "..._WO_US" codes to its washer tables (SmartNConnectivity.mappingModel).
enum class Appliance : uint8_t { UNKNOWN, DRYER, WASHER };

inline Appliance appliance_from_model(const std::string &model) {
  if (model.find("_D_") != std::string::npos) {
    return Appliance::DRYER;
  }
  if (model.find("_WO_") != std::string::npos) {
    return Appliance::WASHER;
  }
  return Appliance::UNKNOWN;
}

// Status codes: USVictor27ListStatus_Dryer and USVictor27_ListStatus (washer) in the app.
inline const char *status_name(Appliance appliance, uint8_t status) {
  static const char *const DRYER[] = {
      "Off",   "Initial", "Pause",         "Dry",                   "Cooling",           "Steam",
      "Mist",  "Wrinkle care", "End",      "Error",                 "Install check",     "Duct clogged",
      "Firmware download", "Cycle download", "Smart diagnosis", "Smart grid delay", "Smart grid reduced",
  };
  static const std::pair<uint8_t, const char *> WASHER[] = {
      {0, "Off"},           {5, "Initial"},     {6, "Pause"},         {7, "Error"},
      {10, "Delay start"},  {20, "Detecting"},  {21, "Adding water"}, {22, "Detergent"},
      {23, "Washing"},      {25, "Shoes"},      {30, "Rinsing"},      {31, "Rinse hold"},
      {40, "Spinning"},     {50, "Drying"},     {60, "End"},          {61, "Tumbling"},
      {70, "Steam pause"},  {71, "Steam"},      {80, "Total cycles"}, {81, "Tub clean due"},
      {82, "Tub clean due"}, {90, "Motor test"},
  };
  if (appliance == Appliance::DRYER && status < sizeof(DRYER) / sizeof(DRYER[0])) {
    return DRYER[status];
  }
  if (appliance == Appliance::WASHER) {
    for (const auto &entry : WASHER) {
      if (entry.first == status) {
        return entry.second;
      }
    }
  }
  return "Unknown";
}

// Working on the load. Dryer: drying, cooling, steam and mist; wrinkle care runs after the
// cycle. Washer: from detecting the load through drying (the app's 20 to 60); tumbling runs
// after the end.
inline bool is_running(Appliance appliance, uint8_t status) {
  switch (appliance) {
    case Appliance::DRYER:
      return status >= 3 && status <= 6;
    case Appliance::WASHER:
      return status >= 20 && status < 60;
    default:
      return false;
  }
}

// The live monitoring block (WM_MonitoringData in the app). Bytes 0-6 mean the same on both
// machines; the settings after them depend on the machine.
struct Monitoring {
  uint8_t status;
  uint16_t remaining_minutes;
  uint16_t initial_minutes;
  uint8_t course;
  uint8_t error;
  std::array<uint8_t, MONITORING_PAYLOAD> payload;
};

inline std::optional<Monitoring> decode_monitoring(const uint8_t *data, size_t size) {
  const auto message = parse_message(data, size, MONITORING_TYPE);
  if (!message || message->size < MONITORING_PAYLOAD) {
    return std::nullopt;
  }
  const uint8_t *p = message->payload;
  Monitoring m{p[0], static_cast<uint16_t>(p[1] * 60 + p[2]), static_cast<uint16_t>(p[3] * 60 + p[4]),
               p[5], p[6], {}};
  std::copy(p, p + MONITORING_PAYLOAD, m.payload.begin());
  return m;
}

// Machine-specific settings, published as the app's index numbers.
enum class Field : uint8_t { DRY_LEVEL, DRYER_TEMPERATURE, SOIL_LEVEL, SPIN, WATER_TEMPERATURE, RINSE };
constexpr size_t FIELD_COUNT = 6;

inline std::optional<uint8_t> field_value(Appliance appliance, Field field, const Monitoring &m) {
  struct Location {
    Appliance appliance;
    Field field;
    uint8_t byte;
  };
  static const Location LOCATIONS[] = {
      {Appliance::DRYER, Field::DRY_LEVEL, 7},          {Appliance::DRYER, Field::DRYER_TEMPERATURE, 8},
      {Appliance::WASHER, Field::SOIL_LEVEL, 7},        {Appliance::WASHER, Field::SPIN, 8},
      {Appliance::WASHER, Field::WATER_TEMPERATURE, 9}, {Appliance::WASHER, Field::RINSE, 10},
  };
  for (const auto &l : LOCATIONS) {
    if (l.appliance == appliance && l.field == field) {
      return m.payload[l.byte];
    }
  }
  return std::nullopt;
}

// Flag byte 14: bit 0 child lock on both machines; bit 3 is the washer's door lock (on the
// dryer it is the damp dry signal).
constexpr size_t FLAGS_BYTE = 14;

inline bool child_locked(const Monitoring &m) { return (m.payload[FLAGS_BYTE] & 0x01) != 0; }

inline std::optional<bool> door_locked(Appliance appliance, const Monitoring &m) {
  if (appliance != Appliance::WASHER) {
    return std::nullopt;
  }
  return (m.payload[FLAGS_BYTE] & 0x08) != 0;
}

// Whether to report the tag as reachable. The dryer's tag misses a poll now and then and
// answers the next one, so only several misses in a row count as losing it.
class Reachability {
 public:
  static constexpr uint8_t MISSES_BEFORE_UNREACHABLE = 3;

  bool record(bool answered) {
    if (answered) {
      misses_ = 0;
      seen_ = true;
    } else if (misses_ < MISSES_BEFORE_UNREACHABLE) {
      misses_++;
    }
    return seen_ && misses_ < MISSES_BEFORE_UNREACHABLE;
  }

 private:
  uint8_t misses_{0};
  bool seen_{false};
};

// The model block is [length][name][crc low][crc high], CRC over length and name
// (CommonCommand.get_ModelName). The dryer's name starts with a 0x14 byte; skip control bytes.
inline std::optional<std::string> decode_model(const uint8_t *data, size_t size) {
  if (size < 3 || static_cast<size_t>(data[0]) + 3 > size) {
    return std::nullopt;
  }
  const uint8_t length = data[0];
  const uint16_t crc = crc16(data, length + 1);
  if (data[length + 1] != (crc & 0xFF) || data[length + 2] != (crc >> 8)) {
    return std::nullopt;
  }
  std::string name;
  for (size_t i = 1; i <= length && data[i] != 0; i++) {
    if (data[i] >= 0x20 && data[i] < 0x7F) {
      name.push_back(static_cast<char>(data[i]));
    }
  }
  return name;
}

}  // namespace tagon
