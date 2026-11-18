// Host tests for protocol.h, checked against a real dump of the DLEX4370K dryer's tag
// (TagInfo on a Pixel 9, 2026-11-11). Build and run: tests/run.sh
#include "../components/lg_tagon/protocol.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace tagon;

static int failures = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
      failures++;                                                      \
    }                                                                  \
  } while (0)

// Blocks 0x17-0x18 of the WM4370HKA washer's tag (TagInfo on a Pixel 9, 2026-11-15).
static const uint8_t WASHER_MODEL_DUMP[] = {
    0x10, 0x46, 0x48, 0x34, 0x46, 0x32, 0x43, 0x44, 0x59, 0x50, 0x33, 0x5F, 0x57, 0x4F, 0x5F, 0x55,
    0x53, 0x52, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// Blocks 0x17-0x18 of the dryer's tag.
static const uint8_t MODEL_DUMP[] = {
    0x15, 0x14, 0x52, 0x56, 0x31, 0x33, 0x42, 0x32, 0x41, 0x53, 0x5F, 0x44, 0x5F, 0x55, 0x53, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53, 0x77, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// The first use-history record (block 0x0F): 9 bytes and their CRC.
static const uint8_t HISTORY_RECORD[] = {0x10, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x19, 0x58, 0x98};

static std::vector<uint8_t> monitoring_message(const std::vector<uint8_t> &payload) {
  std::vector<uint8_t> m = {MONITORING_TYPE, static_cast<uint8_t>(payload.size())};
  m.insert(m.end(), payload.begin(), payload.end());
  const uint16_t crc = crc16(m.data(), m.size());
  m.push_back(crc & 0xFF);
  m.push_back(crc >> 8);
  m.resize(MONITORING_BLOCKS * BLOCK_SIZE, 0);
  return m;
}

static void crc_matches_the_dryers_own_records() {
  const uint16_t crc = crc16(HISTORY_RECORD, 9);
  CHECK((crc & 0xFF) == HISTORY_RECORD[9]);
  CHECK((crc >> 8) == HISTORY_RECORD[10]);
}

static void reads_the_model_code() {
  const auto model = decode_model(MODEL_DUMP, sizeof(MODEL_DUMP));
  CHECK(model.has_value());
  CHECK(model && *model == "RV13B2AS_D_US");
}

static void rejects_a_corrupted_model() {
  uint8_t copy[sizeof(MODEL_DUMP)];
  std::memcpy(copy, MODEL_DUMP, sizeof(copy));
  copy[5] ^= 0x01;
  CHECK(!decode_model(copy, sizeof(copy)).has_value());
}

static void builds_the_same_request_as_the_lg_app() {
  const Idm idm = {0x02, 0xFE, 0, 0, 0, 0, 0, 0};
  uint8_t out[read_request_size(2)];
  const size_t n = build_read_request(out, idm, MODEL_BLOCK, 2, MEMORY_MODE);
  const uint8_t expected[] = {0x14, 0x06, 0x02, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                              0x01, 0x09, 0x00, 0x02, 0x00, 0x17, 0x00, 0x00, 0x18, 0x00};
  CHECK(n == sizeof(expected));
  CHECK(std::memcmp(out, expected, sizeof(expected)) == 0);
}

// The app's live reads (command IDs 500 and up, monitoring is 601) set the mode byte to 4.
static void a_live_read_sets_the_mode_byte() {
  const Idm idm = {0x02, 0xFE, 0, 0, 0, 0, 0, 0};
  uint8_t out[read_request_size(2)];
  const size_t n = build_read_request(out, idm, MONITORING_BLOCK, 2, LIVE_MODE);
  const uint8_t expected[] = {0x14, 0x06, 0x02, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                              0x01, 0x09, 0x00, 0x02, 0x00, 0x23, 0x04, 0x00, 0x24, 0x04};
  CHECK(n == sizeof(expected));
  CHECK(std::memcmp(out, expected, sizeof(expected)) == 0);
}

// The dryer's own monitoring block, read live on 2026-11-15: Dry, 51 of 57 min left.
static const uint8_t DRYER_MONITORING[] = {
    0x23, 0x1C, 0x03, 0x00, 0x33, 0x00, 0x39, 0x03, 0x00, 0x03, 0x03, 0x00, 0x00, 0x04, 0x00, 0x00,
    0x00, 0xA2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x29, 0x62,
};

static void decodes_the_dryers_live_block() {
  const auto m = decode_monitoring(DRYER_MONITORING, sizeof(DRYER_MONITORING));
  CHECK(m.has_value());
  CHECK(m && m->status == 3);
  CHECK(m && m->remaining_minutes == 51);
  CHECK(m && m->initial_minutes == 57);
  CHECK(m && m->course == 3);
  CHECK(m && m->error == 0);
  CHECK(m && field_value(Appliance::DRYER, Field::DRY_LEVEL, *m) == 3);
  CHECK(m && field_value(Appliance::DRYER, Field::DRYER_TEMPERATURE, *m) == 3);
  CHECK(m && !field_value(Appliance::DRYER, Field::SPIN, *m).has_value());
  CHECK(m && is_running(Appliance::DRYER, m->status));
  CHECK(m && std::strcmp(status_name(Appliance::DRYER, m->status), "Dry") == 0);
}

static void decodes_a_washer_block() {
  std::vector<uint8_t> payload(MONITORING_PAYLOAD, 0);
  payload[0] = 40;  // spinning
  payload[1] = 0;
  payload[2] = 12;  // 12 min left
  payload[3] = 1;
  payload[4] = 4;   // 64 min cycle
  payload[5] = 2;   // course
  payload[7] = 3;   // soil
  payload[8] = 4;   // spin
  payload[9] = 2;   // water temperature
  payload[10] = 1;  // rinse
  payload[14] = 0x08 | 0x01;  // door locked, child lock on
  const auto bytes = monitoring_message(payload);
  const auto m = decode_monitoring(bytes.data(), bytes.size());
  CHECK(m.has_value());
  CHECK(m && m->remaining_minutes == 12);
  CHECK(m && m->initial_minutes == 64);
  CHECK(m && field_value(Appliance::WASHER, Field::SOIL_LEVEL, *m) == 3);
  CHECK(m && field_value(Appliance::WASHER, Field::SPIN, *m) == 4);
  CHECK(m && field_value(Appliance::WASHER, Field::WATER_TEMPERATURE, *m) == 2);
  CHECK(m && field_value(Appliance::WASHER, Field::RINSE, *m) == 1);
  CHECK(m && !field_value(Appliance::WASHER, Field::DRY_LEVEL, *m).has_value());
  CHECK(m && door_locked(Appliance::WASHER, *m) == true);
  CHECK(m && !door_locked(Appliance::DRYER, *m).has_value());
  CHECK(m && child_locked(*m));
  CHECK(m && is_running(Appliance::WASHER, m->status));
  CHECK(m && std::strcmp(status_name(Appliance::WASHER, m->status), "Spinning") == 0);
}

static void washer_status_names_follow_the_app() {
  CHECK(std::strcmp(status_name(Appliance::WASHER, 0), "Off") == 0);
  CHECK(std::strcmp(status_name(Appliance::WASHER, 23), "Washing") == 0);
  CHECK(std::strcmp(status_name(Appliance::WASHER, 30), "Rinsing") == 0);
  CHECK(std::strcmp(status_name(Appliance::WASHER, 60), "End") == 0);
  CHECK(std::strcmp(status_name(Appliance::WASHER, 3), "Unknown") == 0);
  CHECK(!is_running(Appliance::WASHER, 6));   // Pause
  CHECK(!is_running(Appliance::WASHER, 60));  // End
  CHECK(!is_running(Appliance::WASHER, 61));  // Tumbling, after the cycle
  CHECK(is_running(Appliance::WASHER, 20));   // Detecting the load
  CHECK(is_running(Appliance::WASHER, 50));   // Drying
}

static void a_finished_dryer_is_not_running() {
  CHECK(!is_running(Appliance::DRYER, 8));  // End
  CHECK(!is_running(Appliance::DRYER, 7));  // Wrinkle care, after the cycle
  CHECK(!is_running(Appliance::DRYER, 0));  // Off
  CHECK(std::strcmp(status_name(Appliance::DRYER, 200), "Unknown") == 0);
  CHECK(std::strcmp(status_name(Appliance::UNKNOWN, 3), "Unknown") == 0);
}

static void rejects_monitoring_with_a_bad_crc() {
  auto m = monitoring_message(std::vector<uint8_t>(MONITORING_PAYLOAD, 1));
  m[4] ^= 0xFF;
  CHECK(!decode_monitoring(m.data(), m.size()).has_value());
}

static void rejects_another_message_type() {
  CHECK(!decode_monitoring(MODEL_DUMP, sizeof(MODEL_DUMP)).has_value());
}

static void reads_the_washers_model_code() {
  const auto model = decode_model(WASHER_MODEL_DUMP, sizeof(WASHER_MODEL_DUMP));
  CHECK(model && *model == "FH4F2CDYP3_WO_US");
}

// The dryer's header block, read live on 2026-11-15 while drying and after it switched off.
static const uint8_t HEADER_ON[] = {0x83, 0x76, 0x22, 0x03, 0x08, 0x32, 0x00, 0x00,
                                    0x55, 0x06, 0x0A, 0x55, 0x00, 0x00, 0x00, 0x00};
static const uint8_t HEADER_OFF[] = {0x83, 0x76, 0x22, 0x03, 0x08, 0x30, 0x00, 0x00,
                                     0x55, 0x06, 0x0A, 0x55, 0x00, 0x00, 0x00, 0x00};

static void the_header_says_whether_the_machine_is_on() {
  const auto on = parse_header(HEADER_ON, sizeof(HEADER_ON));
  const auto off = parse_header(HEADER_OFF, sizeof(HEADER_OFF));
  CHECK(on && on->powered && !on->error);
  CHECK(off && !off->powered && !off->error);
  uint8_t error[sizeof(HEADER_ON)];
  std::memcpy(error, HEADER_ON, sizeof(error));
  error[5] = 0x33;
  CHECK(parse_header(error, sizeof(error)).value_or(Header{}).error);
  error[0] = 0x00;
  CHECK(!parse_header(error, sizeof(error)).has_value());
}

static void tells_the_appliance_from_its_model_code() {
  CHECK(appliance_from_model("RV13B2AS_D_US") == Appliance::DRYER);
  CHECK(appliance_from_model("FH4F2CDYP3_WO_US") == Appliance::WASHER);
  CHECK(appliance_from_model("T1854ADFS5") == Appliance::UNKNOWN);
}

static void parses_a_felica_read_response() {
  std::vector<uint8_t> r = {0, FELICA_READ_RESPONSE, 0x02, 0xFE, 0, 0, 0, 0, 0, 0, 0x00, 0x00, 2};
  r.insert(r.end(), MODEL_DUMP, MODEL_DUMP + sizeof(MODEL_DUMP));
  r[0] = static_cast<uint8_t>(r.size());
  const auto blocks = parse_read_response(r.data(), r.size(), 2);
  CHECK(blocks.has_value());
  CHECK(blocks && blocks->size == 32);
  CHECK(blocks && decode_model(blocks->data, blocks->size).value_or("") == "RV13B2AS_D_US");
}

static void rejects_a_felica_error_status() {
  std::vector<uint8_t> r = {0, FELICA_READ_RESPONSE, 0x02, 0xFE, 0, 0, 0, 0, 0, 0, 0xFF, 0xA1, 0};
  r[0] = static_cast<uint8_t>(r.size());
  CHECK(!parse_read_response(r.data(), r.size(), 2).has_value());
}

static void one_missed_poll_keeps_the_tag_reachable() {
  Reachability r;
  CHECK(r.record(true));
  CHECK(r.record(false));
  CHECK(r.record(false));
  CHECK(!r.record(false));
  CHECK(!r.record(false));
  CHECK(r.record(true));
  CHECK(r.record(false));
}

static void starts_unreachable_until_a_read_works() {
  Reachability r;
  CHECK(!r.record(false));
  CHECK(r.record(true));
}

int main() {
  crc_matches_the_dryers_own_records();
  reads_the_model_code();
  rejects_a_corrupted_model();
  builds_the_same_request_as_the_lg_app();
  a_live_read_sets_the_mode_byte();
  decodes_the_dryers_live_block();
  decodes_a_washer_block();
  washer_status_names_follow_the_app();
  reads_the_washers_model_code();
  tells_the_appliance_from_its_model_code();
  the_header_says_whether_the_machine_is_on();
  a_finished_dryer_is_not_running();
  rejects_monitoring_with_a_bad_crc();
  rejects_another_message_type();
  parses_a_felica_read_response();
  rejects_a_felica_error_status();
  one_missed_poll_keeps_the_tag_reachable();
  starts_unreachable_until_a_read_works();
  if (failures != 0) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("all tests passed\n");
  return 0;
}
