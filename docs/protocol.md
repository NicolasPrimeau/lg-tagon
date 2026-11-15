# LG Tag On protocol

What the 2016-era LG washers and dryers expose over their "Tag On" NFC logo, worked out from
the LG Smart Laundry&DW Android app and a tag dump. Nothing here comes from LG documentation
beyond the owner's manual.

## Sources

- App: `com.lg.apps.lglaundry` 4.8.6 (APKPure), decompiled with androguard. Key classes:
  `com.lge.nfc.tagtypepacket.NfcFPacket` / `NfcVPacket` (framing), `com.lge.nfc.command.PS_WM_Command`
  (block addresses), `com.lge.nfc.dataformat.wm.WM_MonitoringData` (field layout),
  `com.lge.nfc.util.Helper` (CRC), `com.lge.nfcres.usvictor.USVictor27ListStatus_Dryer` (status names).
- Dump: NXP TagInfo 6.3.0 on a Pixel 9, DLEX4370K dryer (serial 709KWREA3102), 2026-11-11,
  dryer on and idle. A OnePlus 13 did not detect the tag at all; the dryer still beeped.

## Transport

- The app supports two tag types: NFC-V (ISO 15693) and FeliCa (NFC-F). The DLEX4370K uses
  **FeliCa**: IDm `02:FE:00:00:00:00:00:00`, PMm `FFFF000000FFFFFF`, system code `0x12FC`,
  NFC Forum Type 3 with an NDEF record pointing at `com.lgeha.nuts` (LG's later app).
- The appliance beeps when a reader's field is present. IC code `0xFFFF` and a 154.62 ms per-block
  read time suggest the appliance's controller answers reads itself (FeliCa Plug style), so reads
  should be live. Not yet proven: compare two monitoring reads during a cycle.
- Read: FeliCa Read Without Encryption, command `0x06`, one service `0x0009`, three-byte block list
  elements `00 <block> <mode>`. Write: command `0x08`, same addressing. At most 4 blocks per read.
- The mode byte (`NfcFPacket.mode_RF`) is 0 for stored data (header, model, history) and 4 for live
  data (app command IDs 500 and up: monitoring, options, energy). The dryer refuses a monitoring
  read with mode 0 (status `FF A5`).
- The app reads the header (block 0x08) first on every tag-on; do the same.
- LG status pairs in the response: `FF 50` no answer from the machine (it is off), `FF 51` host
  error, `FF A5` refused, `FF 60` bad response.
- Confirmed live on 2026-11-15: two reads a minute apart counted the remaining time down.

## Messages

Each item is an LG message: `[type][length][payload][crc low][crc high]`, with CRC-16/XMODEM
(poly 0x1021, init 0) over type, length and payload. Verified on all 10 history records and the
model block of the dump.

| Block(s) | Type | Contents |
|---|---|---|
| 0x08 | header | `83 76 22 03 ...`; byte 5 is the device state: high nibble 3, bit 1 powered on, bit 0 error (`32` drying, `30` off). Readable while the machine is off; nothing else is |
| 0x09-0x0D | | Diagnosis (5 blocks) |
| 0x0E | | Current downloaded cycle |
| 0x0F-0x15 | | Use history: up to 10 records of 9 bytes + CRC, no type/length |
| 0x17-0x18 | | Model code: `[length][name][crc]`, CRC over length and name. Dryer `RV13B2AS_D_US` (name prefixed by 0x14, NUL padded), washer `FH4F2CDYP3_WO_US` |
| 0x22 | 0x22 | Detergent guide (washers) |
| 0x23-0x24 | 0x23 | **Monitoring, live state, 28-byte payload** |
| 0x24 | 0x24 | Cycle control (write; overlaps the monitoring read, so likely a mailbox) |
| 0x25-0x26 | 0x25/0x26 | Option settings (write/read) |
| 0x27-0x29 | 0x27 | Energy monitoring |

Blocks 0x20 and up were not in the TagInfo dump, which stops at 0x1F.

## Monitoring payload (dryer)

| Byte | Field |
|---|---|
| 0 | Status (below) |
| 1, 2 | Remaining time, hours and minutes |
| 3, 4 | Initial (total) time, hours and minutes |
| 5 | Cycle index |
| 6 | Error index |
| 7 | Dry level |
| 8 | Temperature |
| 9 | Time dry |
| 10 | Load item |
| 11 | Buzzer |
| 12, 13 | Delay-start time, hours and minutes |
| 14-16 | Flag bits (child lock, damp dry signal, crease care, ...; model dependent) |
| 26 | Cycle type |
| 27 | More/less time |

Dryer status: 0 off, 1 initial, 2 pause, 3 dry, 4 cooling, 5 steam, 6 mist, 7 wrinkle care, 8 end,
9 error, 10 install check, 11 duct clogged, 12 firmware download, 13 cycle download,
14 smart diagnosis, 15/16 smart grid. The app treats status > 2 as running and 8 as end.

The app maps model codes `RV13xx_D_US` to the dryer tables; `RV13B2AS_D_US` is newer than the app
and absent from its list, so the layout above is the family's, not confirmed for this model.
Cycle and error names are not in this app version; they are published as numbers.

## Monitoring payload (washer)

Bytes 0-6 and 12-16 as for the dryer. Washer status codes (`USVictor27_ListStatus`): 0 off,
5 initial, 6 pause, 7 error, 10 delay start, 20 detecting, 21 adding water, 22 detergent,
23 washing, 25 shoes, 30 rinsing, 31 rinse hold, 40 spinning, 50 drying, 60 end, 61 tumbling,
70/71 steam, 80-82 counters and tub clean alarms, 90 motor test. The app maps
`FH4F2CDYP3_WO_US` (WM4370HKA) to these tables.

| Byte | Field |
|---|---|
| 7 | Soil level (wash) |
| 8 | Spin |
| 9 | Water temperature |
| 10 | Rinse |
| 11 | Dry level (combo models) |
| 14 | Flags: bit 0 child lock, bit 3 door lock, bit 5 detergent, bit 6 drum light |
| 24 | Water flow |
| 25 | Soak |

## Open questions

- Does every poll beep? If so, poll only while the dryer draws power or vibrates.
- The washer's monitoring layout is the app's, not yet confirmed against a live read.
