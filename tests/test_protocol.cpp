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

static void decodes_a_running_dryer() {
  std::vector<uint8_t> payload(MONITORING_PAYLOAD, 0);
  payload[0] = 3;   // Dry
  payload[1] = 0;   // remaining 0 h
  payload[2] = 42;  // 42 min
  payload[3] = 1;   // initial 1 h
  payload[4] = 5;   // 5 min
  payload[5] = 7;   // course
  payload[6] = 0;   // no error
  const auto m = monitoring_message(payload);
  const auto state = decode_dryer(m.data(), m.size());
  CHECK(state.has_value());
  CHECK(state && state->remaining_minutes == 42);
  CHECK(state && state->initial_minutes == 65);
  CHECK(state && state->course == 7);
  CHECK(state && dryer_is_running(state->status));
  CHECK(state && std::strcmp(dryer_status_name(state->status), "Dry") == 0);
}

static void a_finished_dryer_is_not_running() {
  CHECK(!dryer_is_running(8));  // End
  CHECK(!dryer_is_running(7));  // Wrinkle care, after the cycle
  CHECK(!dryer_is_running(0));  // Off
  CHECK(std::strcmp(dryer_status_name(200), "Unknown") == 0);
}

static void rejects_monitoring_with_a_bad_crc() {
  auto m = monitoring_message(std::vector<uint8_t>(MONITORING_PAYLOAD, 1));
  m[4] ^= 0xFF;
  CHECK(!decode_dryer(m.data(), m.size()).has_value());
}

static void rejects_another_message_type() {
  CHECK(!decode_dryer(MODEL_DUMP, sizeof(MODEL_DUMP)).has_value());
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

int main() {
  crc_matches_the_dryers_own_records();
  reads_the_model_code();
  rejects_a_corrupted_model();
  builds_the_same_request_as_the_lg_app();
  a_live_read_sets_the_mode_byte();
  decodes_a_running_dryer();
  a_finished_dryer_is_not_running();
  rejects_monitoring_with_a_bad_crc();
  rejects_another_message_type();
  parses_a_felica_read_response();
  rejects_a_felica_error_status();
  if (failures != 0) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("all tests passed\n");
  return 0;
}
