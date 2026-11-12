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
  elements `00 <block> 00`. Write: command `0x08`, same addressing. At most 4 blocks per read.

## Messages

Each item is an LG message: `[type][length][payload][crc low][crc high]`, with CRC-16/XMODEM
(poly 0x1021, init 0) over type, length and payload. Verified on all 10 history records and the
model block of the dump.

| Block(s) | Type | Contents |
|---|---|---|
| 0x08 | header | LG header, read first by the app |
| 0x09-0x0D | | Diagnosis (5 blocks) |
| 0x0E | | Current downloaded cycle |
| 0x0F-0x15 | | Use history: up to 10 records of 9 bytes + CRC, no type/length |
| 0x17-0x18 | 0x15 | Model code, 20 bytes NUL padded: `RV13B2AS_D_US` |
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

## Open questions

- Is monitoring live during a cycle? Read it twice a few minutes apart while drying.
- Does every poll beep? If so, poll only while the dryer draws power or vibrates.
- The washer: model and tag type unknown. Scan it with the Pixel.
