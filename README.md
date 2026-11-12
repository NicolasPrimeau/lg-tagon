# lg-tagon

Home Assistant for a 2016-era LG dryer (DLEX4370K) through its "Tag On" NFC logo: an ESP32 and a
PN532 reader stuck on the logo read the dryer's live state and publish it over ESPHome. No cloud.

What the dryer exposes, and how it was found: [docs/protocol.md](docs/protocol.md).

## Hardware

- ESP32 DevKit (ESP32-WROOM-32U: plug in the antenna)
- PN532 NFC module V3 (must support FeliCa; the V3 does)
- 4 female-to-female jumper wires, USB-A to USB-C cable, USB charger

## Wiring

Set the PN532's two switches to **I2C**: switch 1 ON, switch 2 OFF (the silkscreen table on the
board shows it).

| PN532 | ESP32 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO21 |
| SCL | GPIO22 |

Stick the PN532 with its antenna side flat on the Tag On logo.

## Flash

```bash
cp secrets.yaml.example secrets.yaml   # fill in Wi-Fi and keys
uvx esphome run dryer.yaml             # first time over USB; later over Wi-Fi
```

Home Assistant discovers the device; add it with the `api_key` from `secrets.yaml`.

## Entities

Status (Off, Dry, Cooling, End, ...), Running, Time remaining, Cycle length, Cycle, Dry level,
Temperature setting, Error code, plus diagnostics: Status code, Model code, Tag reachable and
Raw monitoring (hex, for decoding fields not mapped yet).

## Tests

```bash
tests/run.sh   # protocol decoding, checked against a real dump of the dryer's tag
```
