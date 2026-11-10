# What rtw88 does on an RTL8822BU, and where r22b differs

> **Status:** Written 2026-11-04, in the middle of the investigation. Suspect 1 turned out to be the cause (fixed in +297, patch 0288); suspect 2 was adopted (+299, patch 0290); upload speed came with transmit aggregation (+301, patch 0292). Line numbers refer to an unpublished working tree that still had temporary code.

Written 2026-11-04 from reading the Linux 7.0 in-tree driver (rtw88, © Realtek, GPL-2.0 OR BSD-3-Clause, read under its BSD-3-Clause option; facts only) and the fork's working tree. Realtek's out-of-tree 88x2bu driver was not used. No board was touched and nothing was built.

## How to read this

- **Part 1** is what rtw88 does, for an engineer who won't open the Linux source. **Part 2** is the comparison, ranked. Nothing in Part 2 is proven: every cause is a suspect until a soak shows it.
- `[code]` means read from the driver. `[cap]` means also seen in a capture: `logs/wifi-rtw88-startup-registers.txt` (values, not joined), `logs/wifi-rtw88-periodic-registers.txt` (counts only, joined, 42 s of TCP on 5 GHz), `logs/wifi-rtw88-c2-usb-summary.txt` (USB sizes on the C2).
- **Capture values are the bytes on the wire, in order, so little-endian.** `W 0x080c 13000050` means 0x50000013. In this report register values are numbers unless they are quoted from a capture.
- Haiku line numbers are the working tree on 2026-11-04 with the TEMP code still in it. They will shift when it goes. Paths are under `src/add-ons/kernel/drivers/network/wlan/realtekwifi/dev/rtwn/`: `r22b_*` in `rtl8822b/`, `r22bu_*` in `rtl8822b/usb/`, `if_rtwn*` and `usb/` directly.
- Unless a row says otherwise, values are for this device: 2T2R, cut D, RFE type 3, three bulk-out endpoints, efuse says no BT core (`wifi_only`), share_ant set (the last two inferred from the capture, see item 9).

**The firmware header matters for many branches.** `firmware/realtek/rtw88/rtw8822b_fw.bin` is 30.20.0, H2C format 14, and its feature word at offset 0x0c is **0**. So rtw88 sees no features at all on this chip: no LPS C2H, no LCLK or PG deep mode, no beacon filter offload, no scan notify, no firmware adaptivity, no scan offload. Every `rtw_fw_feature_check()` branch below takes the "not supported" side.

---

# Part 1: what rtw88 does

## 1. Bring-up order

`rtw_core_start()` calls `rtw_power_on()`. Before that, at probe, `rtw_chip_info_setup()` powers the MAC on, downloads the firmware once to read the hardware feature report, reads the efuse and powers off again.

### 1.1 MAC power on (`rtw_mac_power_on`) [cap]

1. **Pre-system config:** 0x1c = 0. 0x64 bits 29,28 set (PAPE and LNAON from WL). 0x4c bits 26,25 clear. 0x40 bit 2 set. Then BB and RF off: 0x02 bits 1,0 clear; 0x1f bits 2,1,0 clear; 0xec bits 26:24 clear.
2. **Is it already on?** Read RPWM (0xfe58 on USB). If 16-bit 0x80 is 0xc078 the firmware is alive, so toggle RPWM bit 7. It counts as off if 0x100 reads 0xea, or if 0xf5 bit 0 is set. If it is already in the requested state, power off, repeat step 1, power on.
3. **Power sequence for USB, in order** (each is a read-modify-write of one byte):

   | Reg | Action | Reg | Action |
   |---|---|---|---|
   | 0x4a | bit 0 = 0 | 0x06 | bit 0 = 1 |
   | 0x05 | bits 7,4,3 = 0 | 0x05 | bit 7 = 0 |
   | 0x12 | bit 1 = 0, then bit 0 = 1 | 0x05 | bits 4,3 = 0 |
   | 0x20 | bit 0 = 1 | 0x10c3 | bit 0 = 1 |
   | | wait 1 ms | 0x05 | bit 0 = 1, then poll until bit 0 = 0 |
   | 0x00 | bit 5 = 0 | 0x20 | bit 3 = 1 |
   | 0x05 | bits 4,3,2 = 0 | 0x29 | = 0xf9 |
   | 0x06 | poll until bit 1 = 1 | 0x24 | bit 2 = 0 |
   | 0xff1a | = 0 | 0xaf | bit 5 = 1 |

   Polls are 50 us apart for up to 1 s. 0x10a8-0x10aa are cut C only. Afterwards clear 0xf5 bit 0.
4. **System config:** 0x1080 gets bits 16 and 8. 0x03 gets 0xdc. 0x1103 = (old & 0xf0) | 0x0c. If 0x80 bit 20 is set, clear it and 0x40 bit 19.

### 1.2 Firmware download (`__rtw_download_firmware`) [cap]

1. Save LTE register 0x38. CPU off: 0x03 bit 2 clear, 0x1d bit 0 clear.
2. Save and set: 0x10d = 0xc0; 0x100 = 0x05; 0x1330 = bit 31; 0x230 = 0x200; 0x22c gets bit 31; 0x550 = (old & ~bit 3) | bit 4.
3. Reset the platform: 0x1082 bit 0 clear, 0x09 bit 6 clear, 0x1082 bit 0 set, 0x09 bit 6 set.
4. 16-bit 0x80 = (old & 0x3800) | 1. 0x1208 gets bit 25.
5. For DMEM, IMEM and EMEM (if used), in chunks of at most 0x1000 bytes: send the chunk as a reserved page, then DMA it. On USB, if size + 48 is a multiple of 512, send one byte more. For the DMA: poll 0x1208 bit 31 clear; 0x1200 = 0x18780000 + 48; 0x1204 = destination; 0x1208 = bit 31 | bit 29 | length, plus bit 24 after the first chunk; poll bit 31 clear. After each section read the checksum status at 0x1208 bit 27 and set the done and OK bits in 0x80.
6. Restore step 2. 0x210 = bit 2. If 0x80 has the checksum bits, set bit 14 and clear bit 0.
7. CPU on: 0x1d bit 0, 0x03 bit 2. Restore LTE 0x38. Poll 0x80 until the ready pattern (bits 15,14,6,5,4,3).

**Sending a reserved page:** 0x204 = page | bit 15; 0x101 bit 0 set; 0x550 = (old & ~bit 3) | bit 4; bulk out with QSEL BEACON and the descriptor offset 48, LS set; poll 0x204 bit 15 = 1; 0x204 = boundary | bit 15; restore 0x550 and 0x101.

### 1.3 MAC init (`rtw_mac_init`) [cap]

**Queues (`txdma_queue_mapping`):** 16-bit 0x10c = 0xf5a0 for three endpoints (VO, VI normal; BE, BK low; MGMT, HI high). 0x100 = 0, then 0xff. 0x1330 = bit 31. 0x10c bit 0 set.

**Pages (`__priority_queue_cfg`):** the Tx FIFO is 2048 pages of 128 bytes, 52 reserved, so the boundary is 0x7cc.

| Reg | Value | Note |
|---|---|---|
| 0x230 / 0x234 / 0x238 / 0x23c | 0x40 / 0x40 / 0x40 / 0 | high / low / normal / extra |
| 0x240 | 0x70b | public |
| 0x22c | bit 31 set | load |
| 0x204, 0x206, 0x424, 0x456 | 0x7cc | boundaries |
| 0x422 | bit 4 set | |
| 0x11c | 0x5eff | Rx FIFO boundary: 24576 - 256 - 1 |
| 0x208 bits 7:4 | 3 | USB Tx aggregation block count |
| 0x20b | 3 | the same |
| 0x20d | bit 1 set | |
| 0x208 | bit 0 set, poll clear | automatic LLT |
| 0x103 | 0 | |

**H2C queue (`init_h2c`):** head and read address 0x244, 0x24c = 0x3fa00 (bits 17:0); tail 0x248 = 0x3fe00; 0x254 low bits to 1, then bit 2 set; 0x20d bit 7 set; check 0x10d4 and 0x10d0 agree.

**`rtw8822b_mac_init`, in order:**

| Reg | Value | Name, comment |
|---|---|---|
| 0x4bc | bit 6 clear | BIT_PRE_TX_CMD |
| 0x455 | 0x70 | A-MPDU maximum time |
| 0x45e | bit 2 set | BIT_EN_EOF_V1 |
| 0x4c8 | 0x2020_08ff | RTS length 0xff, RTS time 8, max aggregate 0x20, RTS max aggregate 0x20 |
| 0x4ce | 0x0801 (16 bit) | BAR retry 1, RA try-rate aggregate limit 8 |
| 0x1448, 0x144a, 0x144c, 0x144e | 6 | fast EDCA thresholds |
| 0x5b4 | bits 6:4 clear | |
| 0x522 | 0 (16 bit) | TXPAUSE |
| 0x51b / 0x512 | 9 / 0x19 | slot / PIFS |
| 0x514 | 0x1010_0e0a | SIFS (the MAC table then overrides bytes, below) |
| 0x502 / 0x506 | 0x186 / 0x3bc | VO / VI TXOP |
| 0x544 | 0x001b_0005 | NAV |
| 0x55e | 0x3030 | Rx TSF offsets |
| 0x550 | bit 3 set | BIT_EN_BCN_FUNCTION |
| 0x540 | 0x6404 | TBTT prohibit |
| 0x558 / 0x559 | 4 / 2 | |
| 0x521 | bit 4 clear | BIT_SIFS_BK_EN |
| 0x6a0 | 0x0fffffff | Rx filter 0 and 1 |
| 0x6a4 | 0xffff | Rx filter 2 |
| 0x608 | 0xe400220e | RCR, replaced later |
| 0x60c | 24 | 12288 / 512 |
| 0x606, 0x605 | 0x30 | TCR |
| 0x7d8 | 0xb0810041 | |
| 0x7d4 | 0x98 | |
| 0x718 | bit 6 set | BIT_DIS_CHK_VHTSIGB_CRC, the MAC table then writes 0x40 |

**`rtw_drv_info_cfg`:** 0x60f = 4. 0x115 low nibble = 0xf. RCR bit 28 set. 0x7d4 bits 9,8 clear.

**`rtw_usb_interface_cfg`:** 0x290 = 0x1e at high speed (burst count, DMA mode, burst size 512). 0x20c bit 9 set (BIT_DROP_DATA_EN).

### 1.4 PHY (`rtw8822b_phy_set_param`) [cap]

1. 0x02 bits 1,0 set. 0x1f bits 2,1,0 set. 0xec bits 26:24 set. 0x808 bits 29,28 clear.
2. Tables, in order: MAC (8-bit writes), BB, AGC (`0x81c`), RF A, RF B. All have conditions on cut, package 15, interface USB and exact RFE type, and rtw88's parser and r22b's take the same branches. (All 43032 hex literals in `rtw8822b_table.c` and `r22b_table.c` compare equal by script.) BB pseudo-addresses are delays: 0xfe 50 ms, 0xfd 5 ms, 0xfc 1 ms, 0xfb 50 us, 0xfa 5 us, 0xf9 1 us. RF: 0xffe 50 ms, 0xfe 100 us, and 1 us after every write. RF tables go through SIPI writes (capture: 401 at 0xc90, 352 at 0xe90).
3. Crystal cap: 0x24 bits 30:25 and 0x28 bits 6:1.
4. 0x808 bits 29,28 set.
5. `rtw8822b_config_trx_mode` (item 7), `rtw_phy_init` (item 4, EDCCA init), `rtw8822b_phy_rfe_init`, `rtw8822b_pwrtrack_init` (reads the swing index from 0xc1c), `rtw8822b_phy_bf_init`.

**RFE init:** 0x64 bits 29,28 = 3. 0x4c bits 26,25 = 0. 0x40 bit 2 = 1. 0x1990 bits 5:0 = 0x30, bits 11:10 = 3. 0x974 bits 5:0 = 0x3f, bits 11:10 = 3.

**Beamforming init, which runs even with no beamformer** (`rtw_bf_phy_init`): 0x14c0: bit 16 set, bits 15:12 = 0xa, bit 7 clear, bits 5:0 clear. 0x167c = 0x70. 0x1680 = 0 (16 bit). 0x42c bit 30 set. 0x45f = 0x10. 0x6dc bits 29:24 = 4 (6M). 0x1c94 = 0xafffafff.

**MAC table values that override `rtw8822b_mac_init` or matter later** [cap]:

| Reg | Value | Reg | Value |
|---|---|---|---|
| 0x420, 0x421 | 0x80, 0x1f | 0x512 | 0x1c (overrides 0x19) |
| 0x428, 0x429 | 0x0a, 0x10 | 0x514, 0x516 | 0x0a, 0x0a (byte writes; 0x515 stays 0x0e, 0x517 stays 0x10) |
| 0x430-0x437 | 00 00 00 01 04 05 07 08 | 0x521, 0x525 | 0x2f, 0x4f |
| 0x43c-0x43f | 04 05 07 08 | 0x551 | 0x10 |
| 0x440-0x442 | 0x5d, 0x01, 0x00 | 0x55c, 0x55d | 0x50, 0xff |
| 0x444-0x44b, 0x44c-0x453 | ARFR0, ARFR1 masks | 0x577, 0x5be | 0x0b, 0x64 |
| 0x455, 0x45e | 0x70, 0x04 | 0x608, 0x609 | 0x0e, 0x22 |
| 0x49c-0x4ab | ARFR4, ARFR5 masks | 0x6a0-0x6a5 | all 0xff |
| 0x4c8-0x4cb | ff 08 20 20 | 0x6de, 0x620-0x627 | 0x84, all 0xff |
| 0x4cc-0x4cf | ff ff 01 08 | 0x638 | 0x50 |
| 0x500-0x50f | EDCA defaults | 0x63c, 0x63d, 0x63e, 0x63f | 0x0a, 0x0a, 0x0e, 0x0e |
| | | 0x640, 0x642, 0x643 | 0x40, 0x40, 0 |
| | | 0x652, 0x66e, 0x718, 0x7d4, 0x7da | 0xc8, 5, 0x40, 0x98, 8 |

### 1.5 After the PHY

1. `rtw_mac_postinit`: nothing on this chip. `rtw_hci_start`: nothing on USB.
2. H2C packets through bulk out (QSEL 0x13): `H2C_PKT_GENERAL_INFO` (sub id 0x0d, firmware Tx boundary), `H2C_PKT_PHYDM_INFO` (sub id 0x11: RFE type, RF type 2 for 2T2R, cut, then antenna_tx in the "Rx" field and antenna_rx in the "Tx" field, which rtw88 has crossed).
3. Coex power-on and init (item 9).
4. `rtw_sec_enable_sec_engine`: 0x100 bit 9 set. 0x680 = 0xcf.
5. RCR = 0xf410408e once the interface is up [cap: `W 0x0608 8e4010f4`]. Bits: 31 APP_FCS, 30 APP_MIC, 29 APP_ICV, 28 APP_PHYSTS, 26 VHT_DACK, 20 PKTCTL_DLEN, 14 HTC_LOC_CTRL, 7 CBSSID_BCN, 3 AB, 2 AM, 1 APM. **Bit 6 (CBSSID_DATA) is never set by rtw88.**
6. Start the 2 s watchdog.

**Add interface (station):** 0x610-0x615 = MAC address. 0x100 mask 0x30000 = no link. 0x550 = 0x08 exactly, so BIT_DIS_TSF_UDT is off from here on.

### 1.6 Calibration

There is one: the firmware IQK. No LCK, no DPK (`dpk_track` is NULL on the 8822B), no CFO tracking, no Tx path diversity, no RFK table.

**IQK (`rtw8822b_do_iqk`):** send `H2C_PKT_IQK` (sub id 0x0e, clear 0, segment 0), read RF A register 0x08 up to 300 times 20 ms apart until 0xabcde, write it back to 0, read 0x1bf0 for logging only.

**When:** `rtw_set_channel` outside a scan sets `need_rfk`. mac80211's `mgd_prepare_tx` runs the IQK if the flag is set. Power tracking also runs it when the averaged thermal moved by 8 or more since the last one.

### 1.7 USB mode switch

`rtw_usb_switch_mode` writes 0xc4 and re-enumerates to go to USB 3, if the efuse allows and the module parameter is on (default on). On a USB 2 port the second probe sees the U3 mode bits and carries on. `rtw_usb_phy_cfg` writes USB 3 PHY register 0x01 = 0xa841 on cut D; the USB 2 table is empty.

---

## 2. Channel and band switching

`rtw_set_channel`: update the state, then `rtw8822b_set_channel` in this order: BB, MAC, RF, Rx DFIR, IGI toggle, CCA, RFE. Then the coex band notification (does nothing here, `stop_dm`), Tx power, `need_rfk`. **No capture covers this:** the startup capture ends in IPS before any channel set (no 0x2860 read, no 0x483 write).

### 2.1 BB (`rtw8822b_set_channel_bb`)

| Register | 2.4 GHz | 5 GHz |
|---|---|---|
| 0x808 bit 28 | 1 | 0 |
| 0x454 bit 7 | 0 | 1 |
| 0xa80 bit 18 | 0 | 1 |
| 0x814 bits 15:10 | 15 | 34 |
| 0x958 bits 4:0 | 0 | ch 36-64: 1; 100-144: 2; 149-177: 3 |
| 0x860 bits 28:17 | 0x96a | 36-48: 0x494; 52-64: 0x453; 100-116: 0x452; 118-177: 0x412 |
| 0xa24, 0xa28 low 16 | 0x384f6577, 0x1525 (ch 14: 0x00006577, 0) | not written |
| 0xcbc bits 9:8 | 2 | **1** (path A only) |

**0x8ac by width.** 20 MHz: clear with 0xffcffc00, low bits 0. 40 MHz: clear with 0xff3ff300, | (idx << 2) | 1; 0xa00 bit 4 set if the primary is the upper one. 80 MHz: clear with 0xfcefcf00, | (idx << 2) | 2; on RFE 2 or 3 also 0x840 bits 15:12 = 6 and 0x8c8 bit 10 = 1. All of them: 0x8c4 bit 30 = 1.

Sub-channel index: 1 upper, 2 lower, 3 upmost, 4 lowest. Channel 64 at 80 MHz (centre 58) is upmost.

### 2.2 MAC (`rtw_set_channel_mac`)

| Register | Value |
|---|---|
| 0x483 | low nibble = sub-channel; high nibble at 80 MHz = 9 (40 upper) or 10 (40 lower) |
| 0x668 bits 8:7 | 80 MHz bit 8, 40 MHz bit 7, 20 MHz neither |
| 0x24 bits 21:20 | 0 |
| 0x55c, 0x638 | 0x50 (80 MHz MAC clock) |
| 0x454 bit 7 | set on 5 GHz, clear on 2.4 GHz |

### 2.3 RF (`rtw8822b_set_channel_rf`)

RF 0x18 is read from path A and rewritten: bits 16,9,8 band (5 GHz: 16 and 8), bits 7:0 channel, bits 18:17 (bit 18 above channel 144, bit 17 from 80), bits 11:10 width (20 MHz both, 40 bit 11, 80 bit 10).

RF A 0xbe bits 17:15 = 0 on 2.4 GHz; on 5 GHz a value from these tables, indexed by (channel - base) / 2:

| Band | Base | Values |
|---|---|---|
| 36-64 | 36 | 7 6 6 5 0 0 7 ff 6 5 0 0 7 6 6 |
| 100-144 | 100 | 6 5 0 0 7 6 6 ff 0 0 7 6 6 5 0 ff 7 6 6 5 0 0 7 |
| 149-177 | 149 | 5 5 0 7 7 6 5 ff 0 7 7 6 5 5 0 |

Then RF A 0xdf bit 18 = 1 for channel 144, else 0. RF 0x18 on A, then on B. RF A 0xb8 bit 19 = 0, then 1.

Our stall logs read 0x1053a on channel 64 at 80 MHz, which fits.

### 2.4 Rx DFIR, IGI toggle

| Width | 0x948 bits 29:28 | 0x94c bits 29:28 | 0xc20, 0xe20 bit 31 |
|---|---|---|---|
| 20 | 2 | 2 | 1 |
| 40 | 1 | 0 | 0 |
| 80 | 2 | 1 | 0 |

**IGI toggle (`rtw8822b_toggle_igi`):** 0xc50 bits 6:0 to IGI - 2, back to IGI; the same on 0xe50; 0x808 byte 0 to 0, then back to rx | (rx << 4). That write of 0 is the only thing in rtw88 that touches the Rx path select at run time, and it only happens at a channel set or a path change.

### 2.5 CCA and RFE for RFE type 3

| Reg | 2R on 2.4 GHz | 2R on 5 GHz |
|---|---|---|
| 0x82c | 0x75da8010 | 0x75da8010 |
| 0x830 | 0x97a0eaac | 0x79a0eaaa |
| 0x838 | 0x86666341 | 0x86666361 |

At 20 MHz on channels 52-64 or 100-144, 0x838 bits 7:4 = 4. Cut B at 40 MHz and RFE 5 change 0x830, not relevant here.

| Reg (both 0xc.. and 0xe..) | 2.4 GHz | 5 GHz |
|---|---|---|
| 0xcb0 bits 23:0 | 0x745774 | 0x477547 |
| 0xcb4 byte 1 | 0x57 | 0x75 |
| 0xcbc bits 11,10,5:0 | 0 | 0 |
| 0xca0 low 16 | 0xa501 (2T or 2R) | 0xa5a5 |

### 2.6 Tx power

`rtw_phy_set_tx_power_level`: per path compute the index per rate (CCK only on 2.4 GHz, OFDM and up always), then write it in groups of four through 0x1d00 (A) and 0x1d80 (B), as 0x1d00 + (rate & 0xfc).

### 2.7 Scans

mac80211 scan start: 0x610 with the scan address, IGI forced to 0x1c on both paths, DIG off. Scan end: IGI back to the last DIG value, DIG on. While scanning the watchdog skips all dynamic work. No Tx queue is paused.

---

## 3. Association

**What rtw88 does, in the order its callbacks are likely called.** The real order comes from mac80211, which isn't on disk. No capture covers it: the startup capture never joins and the periodic one starts after the join.

| Trigger | Action |
|---|---|
| `mgd_prepare_tx` | Coex connect notification (nothing, `stop_dm`). IQK if `need_rfk`. |
| `sta_add` (`rtw_sta_add`) | `si->mac_id` = the vif's (0). init_ra_lv = 1. `rtw_update_sta_info(reset = true)` sends RA info. `H2C_CMD_MEDIA_STATUS_RPT` connect. |
| BSSID change | 0x618-0x61d. |
| ASSOC (joined) | Coex (nothing). **Download reserved pages.** **`H2C_CMD_RSVD_PAGE`.** **`H2C_CMD_DEFAULT_PORT`.** Coex media status (nothing). `rtw_bf_assoc`. Beacon filter (no feature, nothing). Then 0x100 mask 0x30000 = linked (0x102 bits 1:0) and 0x6a8 bits 10:0 = AID. |
| `conf_tx` per AC | 0x500, 0x504, 0x508, 0x50c: four read-modify-writes each: TXOP bits 26:16, CWmax bits 15:12, CWmin bits 11:8, AIFS bits 7:0 = aifsn * slot + SIFS (16 on 5 GHz, 10 on 2.4 GHz). |
| Key set | CAM through 0x674 (data), 0x670 (command, bit 31, bit 16, entry << 3 + word), words 7 down to 0. Pairwise from entry 4, group by key index. IV generated in software. CCMP management frames in software. |
| ASSOC (left) | Leave LPS. `rtw_bf_disassoc`. Net type no link, AID 0. |
| `sta_remove` | Media status disconnect. |
| Key delete | Flush the MAC queues (polls, item 5), clear CAM. |

### H2C contents (mailbox: 0x1d0 + 4 * box for word 0, 0x1f0 + 4 * box for word 1; poll 0x1cc bit box clear for up to 3 ms; word 1 first, then word 0)

| Command | Id | Word 0 fields | Word 1 |
|---|---|---|---|
| RA info | 0x40 | macid 15:8; rate id 20:16; init RA level 22:21; SGI 23; bandwidth 25:24; LDPC 26; no-update 27; VHT 29:28; DIS_PT 30 (always 1) | RA mask, 32 bits |
| RSSI monitor | 0x42 | macid 15:8; RSSI 31:24 | STBC bit 1 |
| Media status | 0x01 | op mode 8; macid 23:16 | |
| Default port | 0x2c | port 15:8; macid 23:16 | |
| WL PHY info | 0x58 | Tx throughput 17:8; Rx throughput 27:18 | Tx rate 7:0; Rx rate 15:8; Rx EVM 23:16 |
| Reserved pages | 0x00 | byte 1 probe response; byte 2 PS-Poll; byte 3 null; byte 4 QoS null | |

**RA info inputs (`rtw_update_sta_info`):** on 5 GHz the wireless set is OFDM + VHT (or OFDM + HT, or OFDM), and `rrsr_val_init` becomes 0x150. On 2.4 GHz it is 0x15f. The mask starts from the peer's MCS map, is cut to two streams, is limited by the band (OFDM 0x10 in VHT, 0x30 in HT on 5 GHz), then by the RSSI level, and then recovered if that left nothing.

- **stbc_en** = BIT(1) for a VHT peer with Rx STBC, BIT(0) for an HT peer. **ldpc_en** likewise.
- **The rate id** for VHT with two chains on 5 GHz is 9 (ARFR0_AC_2SS).

**Reserved pages at join (`rtw_fw_download_rsvd_page`):** a dummy first page, then PS-Poll, QoS null, null, LPS PG DPK and LPS PG info. Then the first page is downloaded again so the descriptor is right. The null frames get descriptors like management frames.

**Beamforming at join (`rtw_bf_assoc`):** if the AP is an MU or SU beamformer, 0x718 becomes 0x9b in its low bits, 0x6e4 onward holds the AP's address, and 0x6f4 the CSI parameters. Each watchdog then reads 0x6dc and 0x440 (`rtw_bf_cfg_csi_rate`). The periodic capture shows those reads (21 each), so **Linux was a beamformee in that capture**.

---

## 4. The periodic work every 2 s (`rtw_watch_dog_work`)

The periodic capture is 42 s, so 21 runs. The counts below are from it.

| # | Step | Condition | Registers and commands | Captured |
|---|---|---|---|---|
| 1 | Busy flag: more than 100 unicast data frames either way in 2 s | always | | |
| 2 | On a change of that flag, `rtw_coex_run_coex` | always | see item 9: 0xaa | R, W 0xaa: 1 |
| 3 | Throughput average, reset counters | always | | |
| | **Stop here while scanning** | | | |
| 4 | Leave LPS | in LPS | H2C 0x20 | 0 |
| 5 | `rtw_coex_wl_status_check` | not in LPS or IPS | **read 0x770, read 0x774, write 0x76e = 0x0c** | 21 each |
| | `rtw_coex_query_bt_hid_list`, `rtw_coex_active_query_bt_info` | return at once on this chip | | |
| 6 | RSSI: level, H2C 0x42 | per station | mailbox | 21 of the 47 H2C |
| 7 | **False alarm statistics** (below) | always | | 21 |
| 8 | Packet counters | always | | |
| 9 | DIG (below) | not scanning | 0xc50, 0xe50 on change | 5 |
| 10 | CCK PD | 2.4 GHz only, and `cck_pd_set` is NULL on the 8822B: does nothing | | |
| 11 | **H2C 0x58 WL PHY info** | always | mailbox | 21 |
| 12 | RA info with no-update = 1 | every 4th run, per station | mailbox | 5 |
| 13 | **RRSR** (item 6) | always | 32-bit 0x440 | W 21 |
| 14 | Path diversity, CFO, DPK | none on this chip | | |
| 15 | Power tracking (below) | efuse thermal not 0xff | RF 0x42, 0xc94, 0xc1c, 0xe94, 0xe1c | see below |
| 16 | **EDCCA** (below) | always, firmware has no adaptivity | 0x8a4 | R, W 42 |
| 17 | Dynamic Rx aggregation (item 8) | always | 0x10c, 0x283, 0x280 | 21 each |
| 18 | Per-vif: CSI rate if beamformee | bfee role | R 0x6dc, R 0x440 | 21 each |
| 19 | Software beacon loss flag | firmware has no filter | no register | |
| 20 | Enter LPS | PS enabled, joined, at most 50 frames each way, no beacon loss | H2C 0x20 | 0 |

**The H2C count fits exactly:** 21 RSSI + 21 PHY info + 5 RA = 47 mailbox polls at 0x1cc [cap].

**Not periodic, though it looks so in the capture:**

- **EDCA:** rtw88 has no EDCA code in the watchdog. R and W of 0x500-0x50c, 20 each, are 5 `conf_tx` calls (each makes 4 read-modify-writes per AC). What called them 5 times in 42 s is in mac80211. An earlier reading of the capture counted them with the 2 s work; they don't belong there.
- **LED:** R and W of 0x4c, 810 each (19 per second), are mac80211's throughput LED trigger through `rtw8822b_led_set` (0x4c bits 18:16 = 0, bit 22 clear, bit 19 = off). This is what most of the control traffic is.
- **0x4e0:** 1622 writes = exactly the accesses to 0x00-0xff (810 + 810 + 1 + 1). See item 7.

### False alarm statistics (`rtw8822b_false_alarm_statistics`)

Read: 0x808 bit 28 (CCK on), 0xa5c low 16 (CCK false alarms), 0xf48 low 16 (OFDM false alarms), 0xf04 (CCK ok, error), 0xf14 (OFDM), 0xf10 (HT), 0xf0c (VHT), 0xf08 high 16 (OFDM CCA), and 0xfcc low 16 (CCK CCA) only if CCK is on. On 5 GHz 0xfcc isn't read, which fits the capture.

Reset, in this order: 0x9a4 bit 17 set then clear; 0xa2c bit 15 clear then set; 0xb58 bit 0 set then clear.

### DIG (`rtw_phy_dig`)

Skip while damping or with DIG disabled. Linked means at least one station.

- **False alarm thresholds:** linked 750, 500, 250 with steps +4, +3, +2 (+6, +4, +2 if the RSSI fell); unlinked 5000, 4000, 2000. Then subtract 2 from the IGI always.
- **Bounds, linked:** min_rssi floored at 0x1c. Lower = min(min_rssi, 0x40). Maximum = min(0x5a, min_rssi + 15). Upper = min(lower + 15, maximum). Unlinked: the same with 0x1c, 0x26, 0x2a and min_rssi = 0x1c.
- Write 0xc50 and 0xe50 bits 6:0 only on a change.
- **Damping:** bit history pattern 5 or 9 with the matching false alarm pattern holds the IGI for up to 21 runs or until RSSI moves by more than 3.

### EDCCA

- **Init (`rtw8822b_adaptivity_init`):** 0x8a4 byte 0 and byte 1 = 0x7f. 0x520 bit 15 clear (BIT_DIS_EDCCA). 0x524 bit 11 set. 0x944 bits 29:28 = 1. 0x8a0 bits 1:0 = 0. 0x8dc bit 5 set.
- **Every 2 s (`rtw8822b_adaptivity`), normal mode:** L2H = max(IGI + 8, 48), H2L = L2H - 8, into 0x8a4 byte 0 and byte 1. So at IGI 0x40 the thresholds are 0x48 and 0x40.
- **Adaptivity mode (DFS region ETSI or JP):** L2H = min(IGI, ini), H2L = L2H - 7, with ini = 36 (ETSI) or 60 (JP). Which mode applied in our captures depends on the regulatory domain, which isn't recorded.

### Power tracking (`rtw8822b_pwr_track`)

Odd runs: trigger with RF A 0x42 bits 17:16 = 3 (read 0x2908 then write 0xc90). Even runs: read RF A 0x42 bits 15:10 and average. Capture: 10 writes at 0xc90 and 21 reads of 0x2908, which is 10 + 11.

If the average differs from this reading: delta = |average - efuse| up to 29, from a table chosen by band (2.4 GHz CCK or OFDM; 5 GHz groups 36-64, 100-144, 149-177) and sign. For each path whose index changed, compute the swing and AGC offset and write 0xc94 or 0xe94 bits 29:25 (AGC) and 0xc1c or 0xe1c bits 31:21 (swing). The captured 5 writes are this. IQK if |average - last IQK thermal| is 8 or more.

---

## 5. Transmit

### 5.1 Descriptor (48 bytes; fields rtw88 defines)

| Word | Bits | Field |
|---|---|---|
| W0 | 15:0, 23:16, 24, 26, 31 | size, offset (48), BMC, LS, DISQSELSEQ |
| W1 | 7:0, 12:8, 20:16, 23:22, 28:24, 29 | macid, QSEL, rate id, security type, packet offset, more data |
| W2 | 12, 19, 22:20, 23 | AGG_EN, SPE_RPT, A-MPDU density, BT null |
| W3 | 7:6, 8, 10, 12, 15, 21:17 | HW SSN select, USE_RATE, DISDATAFB, USE_RTS, NAV use header, max aggregate number |
| W4 | 6:0, 12:8, 28:24 | data rate, rate fallback limit (not on 8822B), RTS rate |
| W5 | 4, 6:5, 7, 9:8, 12 | short GI, bandwidth, LDPC, STBC, RTS short |
| W6 | 11:0 | SW define (report sequence) |
| W7 | 15:0, 31:24 | checksum, DMA Tx aggregate number |
| W8 | 15 | EN_HWSEQ |
| W9 | 7, 6:0, 23:12 | TIM enable, offset, software sequence |

**There is no retry limit or lifetime field in the descriptor rtw88 uses.**

### 5.2 By frame type

| Field | Unicast data | Null, QoS null | Management | EAPOL | Multicast data |
|---|---|---|---|---|---|
| Function | data | mgmt | mgmt | data, as unicast | data, no station |
| QSEL | TID (`skb->priority`) | TID | 0x12 | TID (AC VO, pushed at once, by the comment in `rtw_ops_wake_tx_queue`) | **0x11 HIGH** |
| Endpoint (ours) | BE, BK low; VI, VO normal | as TID | high | normal | **high** |
| macid | station | station or vif | station or vif | station | **vif (0)** |
| rate id | station's | 8 (B) or 7 (G) | 8 or 7 | station's | **6 (BG), default** |
| W4 rate | highest for the peer | lowest basic | lowest basic | highest | **6M** |
| USE_RATE | **0** | 1 | 1 | **0** | **0** |
| DISDATAFB | 0 | 1 | 1 | 0 | 0 |
| W5 BW | station's | 0 | 0 | station's | **0** |
| W5 STBC | **bool: 0 or 1** | 0 | 0 | bool | 0 |
| W5 LDPC | **bool: 0 or 1** | 0 | 0 | bool | 0 |
| W5 short GI | never set | 0 | 0 | 0 | 0 |
| A-MPDU | if the queue's BA is up | no | no | not started by it | no |
| W2 density | **the peer's own value** | | | | |
| W3 max agg | (4 << peer's HT factor) - 1 | | | | |
| RTS | if `use_rts` or above `rts_threshold`: W3 USE_RTS, W4 RTS rate 24M, W5 RTS short | no | no | as data | no |
| Sequence | software, from the header | hardware (EN_HWSEQ, DISQSELSEQ) | hardware | software | software |
| W1 more data | 0 | 0 | 0 | 0 | **1** |
| Security | key type: 3 CCMP, 1 TKIP/WEP | key type | key type | key type | key type |

**The STBC and LDPC bool is load-bearing.** `struct rtw_tx_pkt_info` declares both as `bool`, and `rtw_tx_data_pkt_info_update` assigns `si->stbc_en` and `si->ldpc_en` through local `bool`s. So VHT_STBC_EN (2) becomes 1. A VHT peer never gets a 2 in W5 bits 9:8 from rtw88.

**The initial rate is "the highest":** VHT 2SS MCS 7, 8 or 9 by the peer's Tx map, or MCS 15 or 7 on HT, or 54M. With USE_RATE = 0 the firmware picks the real one and reports it in C2H 0x0c.

H2C packets: QSEL 0x13, offset 0, LS 0, 32 bytes. Reserved pages: QSEL 0x10, offset 48, LS 1.

### 5.3 USB

- **Queue to endpoint** comes from `rqpn_table_8822b[3]` as in 1.3. There is one software queue per endpoint.
- **Aggregation (`rtw_usb_tx_agg_skb`):** a queue with a single frame sends it as is. Otherwise frames go into a 20 KiB buffer, each 8-byte aligned, up to `usb_tx_agg_desc_num` = **3 frames** or 20 KiB. If more than one, the first descriptor's W7 bits 31:24 = count and the checksum is recomputed.
- **Checksum:** XOR of the first 32 bytes as 16-bit words, W7 bits 15:0, on every frame.
- **Tx work:** endpoints from last to first, up to 200 transfers each. Every URB has `URB_ZERO_PACKET`.
- **Sizes seen on the C2 [cap]:** 118 to 20060 bytes, median 9384 while uploading, up to 206 open at once. At most three frames adding up to 20 KB suggests A-MSDUs of several KB. Its `max_rc_amsdu_len` from the firmware's rate report allows that: below 550 no A-MSDU, below 1800 1200 bytes, below 4000 2600, below 7000 3500, else unlimited (rates in units of 100 kb/s). 2.4 GHz refuses A-MSDU.

### 5.4 Limits, status and flow

| Mechanism | What rtw88 does on this chip |
|---|---|
| Retry limit, 0x42a | **Never written.** The only writer, `rtw_coex_limited_tx` (0x0808), is behind `stop_dm` and needs 2.4 GHz. It stays at the chip's reset value. **That value isn't in any table or capture: unknown.** |
| Lifetime, 0x426 | Never written here, same reason. |
| Rate fallback | Tables: DARFRC 0x430-0x437, RARFRCH 0x43c-0x43f. 0x438-0x43b are never written. |
| A-MPDU time, 0x455 | 0x70. Limits at 0x4c8-0x4cb. |
| BAR, 0x4ce-0x4cf | 1 and 8. |
| Tx reports | Only if mac80211 asks for status on the frame: W2 SPE_RPT, W6 = sequence << 2. C2H 0x03, or 0xff with sub id 0x0f. Purged after 500 ms with a warning. |
| Tx status otherwise | URB done counts as "acked". So retries are never visible to mac80211. |
| TXPAUSE, 0x522 | 0 in MAC init. Otherwise only AP beacon code and WoWLAN suspend. **Never set in normal station running, not even for a scan.** |
| Flush | USB has no `flush_queues`. `rtw_mac_flush_queues` compares 16-bit reserved and available pages (0x234/0x236 low, 0x238/0x23a normal, 0x230/0x232 high, 0x23c/0x23e extra) up to 5 times, 20 ms apart, then gives up. |
| BA | `rtw_txq_check_agg` starts a session for any non-VO frame except EAPOL; the TX_START reply is IMMEDIATE. |

---

## 6. Hardware-generated responses

**rtw88 has no code that changes SIFS or response path at run time.** Here is everything that sets them.

| Register | Value | Set by | Effect as named |
|---|---|---|---|
| 0x440 RRSR | 0x15d at table load. Then every 2 s: `rrsr_val_init & mask`, a full 32-bit write | Table; `rtw_phy_rrsr_update` | Response rates |
| | | `rtw_bf_cfg_csi_rate`: 16-bit, bit 11 (54M), first time, beamformee only | |
| `rrsr_val_init` | 0 until the first station; 0x150 on 5 GHz (6, 12, 24M); 0x15f on 2.4 GHz | `rtw_update_sta_info` | So **RRSR is 0 while no station exists** |
| mask | bits (order + 4 - 1):0, where order comes from the rate the firmware last reported: within its group, plus 1 for HT and VHT. Lowest across stations | `rtw_phy_get_rrsr_mask` | Bits 22:21 (RSC) are therefore 0 |
| 0x428, 0x429 | 0x0a, 0x10 | Table | SPEC_SIFS |
| 0x514-0x517 | 0x0a, 0x0e, 0x0a, 0x10 | `rtw8822b_mac_init`, then table bytes | SIFS |
| 0x63a | not written | | MAC_SPEC_SIFS |
| 0x63c, 0x63d | 0x0a, 0x0a | Table | response SIFS CCK |
| 0x63e, 0x63f | 0x0e, 0x0e | Table | response SIFS OFDM |
| 0x640 | 0x40 | Table | ACK timeout |
| 0x639 | not written | | ACK timeout CCK |
| 0x642, 0x643 | 0x40, 0 | Table | EIFS |
| 0x638, 0x55c | 0x50 | Table, channel set | 1 us clocks |
| 0x483 | sub-channel | channel set | Tx sub-channel |
| 0x668 bits 8:7 | width | channel set | RF mode |
| 0x454 bit 7 | 1 on 5 GHz | channel set, twice | BIT_CHECK_CCK_EN |
| 0x608 bit 26 | 1 | `rtw_chip_parameter_setup` | VHT_DACK ("default use ack") |
| 0x604+1, +2 | 0x30, 0x30 | `rtw8822b_mac_init` | |
| 0x6cc bit 3, bit 4; 0x6cf bit 3 | set | coex init | Tx response, Tx beacon, beacon queue are high priority at the PTA |
| 0x6dc bits 29:24 | 6M, then per RSSI | BF init, CSI rate | CSI report rate |
| 0x93c bits 31:20 | 0x001 | `rtw8822b_config_trx_mode` | **Path for 1SS Tx: A.** The stall's A-only Tx fits any 1SS frame |

**Bandwidth of a response:** no rtw88 register names it on this chip beyond the width registers above. Bits 22:21 of RRSR are named `BITS_RRSR_RSC` in `reg.h` but not used on the 8822B.

---

## 7. Tx path and keeping the BB and RF healthy

### 7.1 `rtw8822b_config_trx_mode` with Tx AB, Rx AB [cap]

| Reg | Value |
|---|---|
| 0xc08, 0xe08 low 16 | 0x3231 |
| 0x93c bits 19:18 | 3 |
| 0x80c bits 29:28; bit 30 | 1; 1 |
| 0x93c bits 31:20; 0xa04 bits 31:28 | 0x001; 8 |
| 0x940 bits 15:4 | 0x43 (2T) |
| 0x80c byte 0 | 0x33 |
| 0xa2c bits 22, 18 | 0, 0 |
| 0xa04 bits 27:24 | 0 |
| 0x808 byte 0 | 0x33 |
| 0x1904 bit 16; 0x800 bit 28; 0x850 bit 23 | 1, 1, 1 |

**`is_tx2_path` is false here,** so 0x93c stays 0x001 and 0xa04 stays 8 (0x043 and 0xc are used only with is_tx2_path or MP mode).

Then the RF mode table, **path A only**: RF 0xef = 0x80000, 0x33 = 1, wait 2 us, read back RF 0x33 until 1 (up to 100 tries); RF 0xef = 0x80000, 0x33 = 1, 0x3e = 0x34, 0x3f = 0x4080c, 0xef = 0, 0xef = 0.

Then IGI toggle, `set_channel_cca` as if channel 1 at 20 MHz, RFE for the current channel.

### 7.2 What's left alone

- **3-wire, 0xc00 and 0xe00:** 7 from the BB table, never touched afterwards on this chip. `REG_3WIRE_SWA` is only named.
- **EOF:** 0x45e bit 2 set by MAC init and the table. Bit 3 (BIT_EN_GNT_BT_AWAKE) only in `rtw_coex_limited_tx`, not reached.
- **Band or path resets at run time:** none. No write to 0x02 after power on and coex init, and none to 0x808 bits 29:28.
- **Hang detection:** none (item 11).

### 7.3 RF access

| Operation | Method | Timing |
|---|---|---|
| Read | 32-bit read of 0x2800 + addr * 4 (A), 0x2c00 + addr * 4 (B), mask to 20 bits | none |
| Write | Read the old value if the mask isn't full, then 32-bit write to 0xc90 (A) or 0xe90 (B) of (addr << 20) OR data, masked with 0x0fffffff | `udelay(13)` after |
| LSSI read and mix write | exist, not used by this chip | |

**Locking:** `rtw_read_rf` and `rtw_write_rf` assert `rtwdev->mutex`. The watchdog holds it throughout, and so do H2C sends. So a multi-register section in the watchdog can't interleave with another configuration path. Tx and Rx bulk transfers run alongside without any lock.

**What rtw88 does around RF or BB writes during traffic: nothing.** No pause, no flush, no RF mode change. The watchdog's writes to 0x9a4, 0xa2c, 0xb58, 0xc50, 0xe50, 0x8a4, 0x440, 0xc94, 0xc1c and the RF trigger all go out with traffic running.

### 7.4 Register access on USB, including 0x4e0

- **Read:** vendor request 0x05, type 0xc0, value = address, 1000 ms timeout, no retry. On failure the old buffer content is used.
- **Write:** vendor request 0x05, type 0x40, 500 ms, no retry.
- **After any read or write of 0x00-0xff or 0x1000-0x10ff** on the 8822B, 8822C and 8821C: one more write of length 1 to **0x4e0**, with the first data byte, result only logged. rtw88's comment explains the sections (on, off, local from 0xfe00) but not what the write does. [cap: the startup log alternates them, and the count in the periodic log fits exactly.]
- Register access is synchronous and sleeps. 128 small buffers are rotated under a spinlock.

---

## 8. Receive

- **4 URBs (`RTW_USB_RXCB_NUM`), each 32768 bytes,** resubmitted from the completion handler itself, with a new or recycled buffer. Normally 3 open at once in the download phase [cap]. If memory is short, a work item retries.
- **8 preallocated buffers,** recycled. A queue of up to 512 to the bottom-half worker. The worker does at most 200 transfers per run.
- **A completion is rejected** if under 24 or at least 32768 bytes; its URB is still resubmitted. On an error status the URB isn't resubmitted.
- **Parsing:** descriptor 24 bytes. W0: length, CRC, ICV, drv info size in 8-byte units, encryption type, shift, PHY status, SWDEC. W2: C2H flag, W3 rate, W4 bandwidth, W5 TSF low. The next packet is at round_up(length + 24 + drvinfo + shift, 8).
- **Rate of completions [cap]:** download 1199 per second, median 24796 bytes, 28 MB/s; median 568 us apart, 99% within 5720 us.

**Aggregation (`rtw_usb_dynamic_rx_agg_v1`), each watchdog:** 0x10c bit 2 set; 0x283 bit 7 clear; 16-bit 0x280 = size | (timeout << 8). If either direction averaged 1 Mbps or more in the last 2 s: size 5, timeout 0x20. Otherwise: size 0, timeout 1.

**C2H:** a packet with the W2 C2H flag is copied out. 0x0b (BT MP), 0x32 (RF on) and 0x38 (scan result) are handled at once. The others go to a work item:

| Id | Handling |
|---|---|
| 0x03, 0xff/0x0f | Tx report |
| 0x09, 0x45, 0x27 | coex handlers; not expected from a chip with no BT |
| 0x0c | RA report: rate and SGI in byte 0, macid byte 1, bandwidth byte 6, minimum length 7. Sets `dm_info.tx_rate`, the report rate and `max_rc_amsdu_len` |
| 0x36, 0x37 | no firmware feature, not expected |

**Rx filter:** RXFLTMAP 0x6a0-0x6a5 are all ones from the table, so all control frames (ACK, BA, RTS, CTS) reach the host. rtw88 passes them to mac80211, whose handling isn't on disk. The upload phase's short completions (median 164 bytes) are likely these; that isn't proven.

**PHY status (`query_phy_status`):** page 0 for CCK, page 1 for the rest. RSSI is `rtw_phy_rf_power_2_rssi` of the two paths, a linear average; the average is kept per station for frames from the BSS addressed to us or beacons.

---

## 9. Coexistence on a chip with no Bluetooth

**This device is `wifi_only`.** `efuse.btcoex` = (rf_board_option & 0xe0) == 0x20. Evidence: the startup capture takes the POWERON then WONLY antenna path, and the periodic capture has no 0x1700 traffic, which a coex run would cause.

**Power-on setting (`rtw_coex_power_on_setting`), in order** [cap]:

1. `stop_dm` = true. 0x02 bits 1,0 set.
2. Read scoreboard 0xaa (16 bit): bit 1 clear means BT is disabled.
3. RFE type: 0xaa only if it changes (EXTFEM off for RFE 3). 0x974 = 0xff (8 bit). 0x1991 bits 1:0 = 0. 0xcbe bit 3 = 0. LTE indirect: 0x38 bit 7 = 0; 0xa0 low 16 = 0xffff; 0xa4 low 16 = 0xffff.
4. Antenna path POWERON: 0x73 bit 2 set when BT is disabled. Switch: 0x4e bit 7 = 0, 0x4f bit 0 = 1, 0xcb4 byte 0 = 0x77, 0xcbd bits 1:0 = **1** (to BT).
5. Table case 1 with share_ant: 0x6c0 = 0x6c4 = 0x55555555, 0x6c8 = 0xf0ffffff. (Without share_ant the lookup is out of range and nothing is written.)
6. 0xff1a = 0. Debug pins off: 0x66 bit 4, 0x67 bit 0, 0x42 bit 3, 0x65 bit 7, 0x73 bit 3 all clear.

**Init (`__rtw_coex_init_hw_config` with wifi_only), in order** [cap]:

1. Clear the coex variables. Read 0xf1. Read 0xaa.
2. H2C 0x69: op 0x0c, para 0x01 (no 5 ms slot extension).
3. 0x550 bit 3 set.
4. RFE type again, including 0xaa = 0x8002.
5. `rtw8822b_coex_cfg_init`: 0x550 bit 3; 0x790 bits 5:0 = 5; 0x778 = 1; 0x40 bits 5 and 9; 0x4c6 bit 4 set and bit 5 clear; 0x762 bit 12 set.
6. Priorities: 0x6cc bits 3 and 4, 0x6cf bit 3.
7. **Antenna path WONLY:**

   | Signal | LTE 0x38 bits | Value |
   |---|---|---|
   | GNT_BT | 15:14 and 11:10 | 1 (software low) |
   | GNT_WL | 13:12 and 9:8 | 3 (software high) |

   Result: 0x7703 in the low 16 bits [cap: `W 0x1704 03770000`]. 0x73 bit 2 set. Switch 0xcbd bits 1:0 = **2** (to WL on 2.4 GHz). No write again for a band change: `stop_dm` makes `rtw_coex_switchband_notify` return, so **on 5 GHz these two bits are then set to 1 by `set_channel_bb`** (item 2).
8. 0xaa = 0x8003 (ACTIVE, ONOFF, BT interrupt enable). `stop_dm` = true.
9. Table case 1 again. TDMA off: scoreboard, then H2C 0x60 with all zero (share_ant). No BT query, as BT is disabled.

LTE indirect access: poll 0x1700 bit 29; read: 0x1700 = 0x800f0000 | offset, then 0x1708. Write: 0x1704 = value, 0x1700 = 0xc00f0000 | offset.

**At run time with `stop_dm`:**

- Every coex notification (connect, media status, scan, band, IPS, LPS) returns at once.
- `rtw_coex_wl_status_check` still runs each 2 s: read 0x770, 0x774, write **0x76e = 0x0c**: BIT_R_GRANTALL_WLMASK (bit 3) and BIT_STATIS_BT_EN (bit 2). It isn't behind `stop_dm`. **This is the only coex register write in steady state.**
- `rtw_coex_run_coex` on a busy-flag change, and from `wl_remain_work` 12 s after traffic stops: the link info part writes scoreboard bit 2 (COEX_SCBD_SCAN) if `wl_gl_busy`, the monitor reads 0xaa, then `stop_dm` returns.
- Grant signals and the antenna are never changed after init. `rtw8822b_coex_cfg_gnt_fix` is empty. `rtw_coex_limited_tx` is not reached.

**Power off:** 0xaa = 0x8000.

---

## 10. Power save

**IPS applies on USB [cap: the startup capture ends in it].** It's a full `rtw_core_stop` (power-off sequence, H2C state lost) when mac80211 says idle, and `rtw_core_start` + `rtw_set_channel` + a full port restore on leaving. `rtw_hci_link_ps` and `rtw_hci_deep_ps` are empty on USB. The power-off bytes are the reverse flow in `trans_act_to_cardemu` and `trans_cardemu_to_carddis`.

**LPS applies on USB as far as the code goes,** without the deep modes: `fw->feature` = 0 gives `LPS_DEEP_MODE_NONE`.

- **Enter (end of the watchdog):** needs `ps_enabled` (one station vif with `cfg.ps`), joined, at most 50 frames either way in 2 s, no beacon loss. H2C 0x20: mode 1, RLBM 1, smart PS 2, awake interval 1, port, state 0 (RF off).
- **Leave (start of the dynamic part of the watchdog, config callbacks, default port):** H2C 0x20: mode 0, RLBM 0, smart PS 0, awake interval 1, state 0x0c (all on). Then poll 0x604 bit 7 clear, 5 times 20 ms; on failure clear it and warn.
- Whether `cfg.ps` is on is a mac80211 and userspace matter. During our captures with traffic there's no H2C 0x20, as the 47 are accounted for.

---

## 11. Detection of, or recovery from, a hung transmitter or receiver

**None on USB.**

| Mechanism | Where | Applies here |
|---|---|---|
| Firmware crash recovery (`rtw_fw_recovery`) | Called from `pci.c` only (firmware interrupt, PCI error recovery); debugfs has a trigger | No |
| Tx report purge | Warns after 500 ms | Only drops the skbs |
| Software beacon loss flag | Blocks LPS; on the 8812A it changes DIG | No action on the 8822B beyond LPS |
| Firmware beacon loss | Firmware feature, off | No |
| USB URB errors | `rtw_err`, Rx URB not resubmitted on error | No recovery |
| mac80211 connection monitor | Not on disk | Presumably probes and disconnects. Armbian logged no disconnect |

**So "0 stalls under Linux" isn't rtw88 hiding them with a recovery.** The driver has nothing that would.

---

## 12. The registers that change at our stall

"Named" means by rtw88. "Unknown" means rtw88 doesn't name it for this chip, and nothing here is a guess at its meaning.

| Reg | rtw88 name | What rtw88 does with it |
|---|---|---|
| 0x0140 | `REG_PKTBUF_DBG_CTRL` | Only `rtw_fw_read_fifo_page`: FIFO dump for debug or crash. Never during normal running |
| 0x0208 | `REG_AUTO_LLT_V1` (also `REG_DWBCN0_CTRL` for older chips) | Init: bits 7:4 = 3, bit 0 start and poll; byte 3 = 3 |
| 0x0230 | `REG_FIFOPAGE_INFO_1` | High queue pages. Low 16: reserved (written 0x40, 0x200 during firmware download). High 16: available (`prioq_addrs` "avail" at +2), read only by the flush |
| 0x0240 | `REG_FIFOPAGE_INFO_5` | Public pages, 16-bit write of 0x70b. No read, and +2 has no name |
| 0x0400 | unknown | Only the start of a debugfs dump page (`mac_4`) |
| 0x0410 | unknown | |
| 0x04d8 | unknown | |
| 0x04e0 | unnamed, literal in `usb.c` | Target of the dummy write after on-section access (item 7.4). Never read |
| 0x05f8 | unknown | |
| 0x0660 | unknown | `fw_fifo_addr` holds a 0x660, but that's a FIFO page start, not this register |
| 0x067c | unknown | The security registers are 0x670, 0x674, 0x678, 0x680 |
| 0x06b4 | unknown | |
| 0x0d30 | unknown | Nearby names are 0xd00, 0xd04, 0xd08, 0xd40, 0xd44, 0xd48 only |
| 0x0d34 | unknown | |
| 0x0d70 | unknown | |
| 0x0f20 | unknown | |
| 0x0f80 | unknown | |
| 0x0f84 | unknown | |
| 0x0fd0 | unknown | 0xfcc is `REG_CCA_CCK` |
| 0x0fd8 | unknown | |
| 0x10fc | `REG_FW_DBG7` | Read only: firmware key check after download (mask 0xffffff00), and the debug dump |
| 0x1400 | unknown | Only the start of a debugfs dump page (`mac_14`) |
| 0x1410 | unknown | |
| 0x1bfc | unknown on the 8822B | The 8822C's header calls this address `REG_STAT_RPT`. That's another chip, so it says nothing here |

---

# Part 2: where the Haiku driver differs

## Top suspects

| # | Suspect | Haiku | One line |
|---|---|---|---|
| 1 | STBC 2 and LDPC 0 in the descriptor for a VHT peer | `r22b_tx.c:362-367` (HEAD: `:332-333`) | Under test now |
| 2 | A-MPDU density forced to 16 us | `r22b_tx.c:342-343`, `if_rtwn.c:638` | Every A-MPDU; worst for small frames at VHT rates |
| 3 | One Rx transfer against rtw88's four | `usb/rtwn_usb_rx.c:618-627`, `usb/rtwn_usb_ep.c:59` | Rx FIFO of about 1 ms |
| 4 | EDCCA thresholds fixed at 0x7f | `r22b_phy.c:873-884` | Tx isn't held back by energy on the channel |
| 5 | Retry limit 0x3030 | `if_rtwn.c:2000-2001` | rtw88 doesn't write it |
| 6 | Multicast in the TID queue, macid 1 | `r22b_tx.c:309,321,324` | rtw88 uses the HIGH queue, its own macid |

The ranking is judgement, against this signature: MAC and BB frozen during a transmission on path A, more common on 5 GHz, mostly while receiving heavily, never under Linux.

**What the signature does and doesn't say, from the logs:**

- RF A in Tx, B in standby means a frame using path A alone. By 0x93c that's any 1SS frame: legacy management or response, or 1SS HT or VHT without working STBC. A valid 2SS frame would use both. A valid 1SS STBC frame should too.
- "No frame handed over in the 2 ms before" doesn't clear data frames. At the stall the chip still holds them: 0x240's available count drops from 0x70b to 0x669 or 0x49e, and 0x234's from 0x40 to 0 in some incidents, while `tx active 0 pending 0` says USB has nothing left. So tens of KB were in the FIFO.
- In `logs/wifi-stall-295-reset.log` the six dumps come from two distinct stalls (Tx counts 842816-842825, then 865260). In the first the newest frames are 1542-byte data frames; in the last they're 94-byte ones. So at least one stall here wasn't during a download. That's a small sample against "mostly download", worth keeping in mind.

## Behaviour differences, ranked

### 1. STBC 2 and LDPC 0 in the descriptor (suspect)

- **rtw88:** W5 STBC and LDPC are 0 or 1 (item 5.2).
- **Haiku at HEAD:** `si->stbc_en & 0x3` gives 2 for a VHT peer, and `si->ldpc_en & 0x1` gives 0. The comment there says the LDPC part follows rtw88; it doesn't, as rtw88's fields are `bool`. All 287 data descriptors in the stall logs read W5 = 0x240: BW 2, LDPC 0, STBC 2.
- **Fit:** VHT only, so 5 GHz; a 2.4 GHz HT peer gets HT_STBC_EN = 1, which is valid. Never under Linux. The firmware picks the rate, so a fallback to 1SS with STBC 2 asks for more streams than two chains have.
- **Against:** nothing here shows the firmware or MAC uses this field on a 2SS or a 1SS rate. The download bias isn't explained.
- **Status:** the working tree (`r22b_tx.c:362-367`, not marked TEMP) already writes 1 and 1, and a soak is running on it (`logs/wifi-soak-295-stbc.log`). It's in the uncommitted diff, with a comment that states the hang as a fact; that isn't established until the soak ends.

### 2. A-MPDU density 16 us (suspect)

- **rtw88:** W2 bits 22:20 = the peer's own value. It advertises 2 us for its own receive (`chip->ampdu_density`).
- **Haiku:** `ieee80211_ht_get_node_ampdu_density()` (`src/libs/compat/freebsd_wlan/net80211/ieee80211_ht.c:3706`) takes the larger of the peer's and `iv_ampdu_density`, and `if_rtwn.c:638` sets that to 16 us. So W2 = 0x00701000 on every A-MPDU in the logs, whatever the AP asked. The same variable makes Haiku advertise 16 us, so the AP pads its frames to us too.
- **Fit:** at VHT 2SS MCS 9 over 80 MHz, 16 us is about 1700 bytes of padding per MPDU boundary. A 94-byte TCP ACK is nearly all padding, a 1500-byte frame hardly any. On 2.4 GHz HT20 it's about 260 bytes. That matches "5 GHz" and "mostly download" better than anything else here, if the MAC can hang on that. Never under Linux, unless this AP asks for 16 us itself.
- **Against:** no evidence that padding can hang the MAC. A padded 2SS frame would show both paths in Tx, so it needs a 1SS rate too.
- **It doesn't explain the upload gap** (12 against 34.7 MB/s): a 1500-byte MPDU at these rates already takes about 14 us or more, so it gets little or no padding. One frame per USB transfer and no A-MSDU fit that gap better.
- **Smallest test:** log `ni_htparam`'s density, then set `iv_ampdu_density` to `IEEE80211_HTCAP_MPDUDENSITY_2`, as rtw88 advertises, so the peer's value is used.

### 3. One Rx transfer (suspect)

- **rtw88:** 4 URBs of 32 KB, with resubmission in the completion handler, typically 3 open.
- **Haiku:** rtwn keeps one transfer on the pipe (`usb/rtwn_usb_ep.c:59`), and the next submit happens in the callback after parsing (`usb/rtwn_usb_rx.c:618-627`), with 16 buffers behind it that can't be queued on the pipe.
- **Fit:** the Rx FIFO is 24320 bytes usable (0x5eff), which is about 1 ms at 22 MB/s. Any gap in resubmission fills it. "While receiving heavily" and "never under Linux" fit: under Linux three more URBs are open when one completes, so the chip always has somewhere to send data.
- **Against:** the frozen thing is the Tx side, and `RXPKT_NUM` at the stall says Rx DMA is idle with nothing waiting, so it would be a trigger and not the state itself. Nothing in rtw88 handles FIFO overflow, so its effect is unknown. Separate dwc2 lanes didn't cure it, but that isn't this.
- **Smallest test:** read a count of Rx overflow if one exists; unknown from rtw88. Otherwise queue several Rx transfers.

### 4. EDCCA thresholds fixed at 0x7f (suspect)

- **rtw88:** 0x8a4 rewritten every 2 s to L2H = max(IGI + 8, 48), H2L = L2H - 8 (item 4).
- **Haiku:** init writes 0x7f, 0x7f (`r22b_phy.c:684`), and `r22b_phy_dynamic_mechanism` (`r22b_phy.c:873-884`) has no adaptivity step. 0x520, 0x524, 0x944, 0x8a0 and 0x8dc do match.
- **Fit:** with 0x7f the MAC isn't held back by channel energy, only by detected packets. Starting a Tx while the AP's frame is on the air, especially on 80 MHz where preamble detection is on the primary channel, fits "while receiving heavily" and "more on 5 GHz". Never under Linux.
- **Against:** a collision is normal. It takes a hardware race to turn that into a hang. Nothing shows one.
- **Smallest test:** add the two writes after DIG. It's cheap.

### 5. Retry limit (suspect)

- **rtw88:** never writes 0x42a; the reset value applies, unknown.
- **Haiku:** 0x3030 (`if_rtwn.c:2000-2001`), which `r22b_post_init` doesn't restore (48 short, 48 long).
- **Fit:** it's the register that decides how long a frame is retried.
- **Against:** retrying has Rx windows for the ACK timeout, so RF mode would flip between samples. 30 of 30 samples in Tx and frozen counters don't look like retrying. At most it raises the exposure to some other cause.
- **Smallest test:** read 0x42a before `if_rtwn.c:2000` writes it, on a fresh power-up. Then leave it alone.

### 6. Multicast data (suspect)

- **rtw88:** QSEL HIGH (0x11), the HIGH endpoint, more data set, macid of the vif (0), rate id 6, rate 6M, USE_RATE 0.
- **Haiku:** QSEL = TID in the same queue as the A-MPDU data (`r22b_tx.c:321`), low endpoint (`usb/rtwn_usb_tx.c:312`), macid `RTWN_MACID_BC` = 1 (`r22b_tx.c:309`; the firmware knows no station 1), rate id BG (`:324`), forced rate with no fallback (`if_rtwn_tx.c:163-165`).
- **Fit:** a non-aggregated frame for an unknown macid, 1SS at a legacy rate on path A, in a queue full of A-MPDUs. Rare in time, as the stalls are. Band-independent, though.
- **Against:** a ping-only run had multicast too, without a stall, though not under load.
- **Smallest test:** log the last multicast before each stall, or switch to QSEL 0x11, macid 0.

### 7. 0x76e never written (suspect)

- **rtw88:** writes 0x0c every 2 s, reads 0x770 and 0x774 (item 9).
- **Haiku:** no such register in `r22b_reg.h` or code; `r22b_coex.c:502-550` is init only, and the 2 s hook (`r22b_ra.c:772`, `if_rtwn_calib.c:102`) doesn't do it.
- **Fit:** bit 3 is named BIT_R_GRANTALL_WLMASK, in the grant logic that decides who gets the air. The name is all rtw88 gives.
- **Against:** nothing else known. On a chip with GNT_WL forced high it may do nothing.
- **Smallest test:** one write of 0x0c at init, or every 2 s with the reads. Very cheap.

### 8. H2C_CMD_WL_PHY_INFO not sent (suspect)

- **rtw88:** H2C 0x58 every 2 s with throughput, rates and EVM.
- **Haiku:** not sent (`r22b_ra.c:768-769`).
- **Fit:** only the firmware knows what it does with this. Rx throughput in particular is a value that might steer firmware behaviour under a download.
- **Against:** no evidence, and the firmware is a black box.
- **Smallest test:** send it. Throughput in the units `RTW_TP_SHIFT` gives (Mb/s), the last reported Tx rate, the last Rx rate, EVM.

### 9. No reserved pages and no H2C_CMD_RSVD_PAGE (suspect)

- **rtw88:** at every join.
- **Haiku:** `r22b_set_rsvd_page` returns EOPNOTSUPP (`r22b_fw.c:805-809`), and is not reached (`if_rtwn.c:221-225` has PMGT off). `r22b_set_media_status` (`r22b_ra.c:733-741`) sends RA info, media status and default port only.
- **Fit:** weak. The firmware may fetch a null frame from these pages, and on Haiku it was never told where they are.
- **Against:** with no LPS or BT nothing obviously asks for one.

### 10. A failed register read writes garbage (suspect)

- **rtw88:** on failure it uses the buffer's old content, no retry.
- **Haiku:** 10 tries at 250 ms (`usb/rtwn_usb_reg.c:64-82`), then 0xffffffff (`:159-160`). `r22b_write32_mask` and friends would write that back. In the 2 s path that means 0x9a4, 0xa2c, 0xb58, 0xc50, 0xe50.
- **Fit:** all ones in a BB register could freeze something, under load only.
- **Against:** it'd take 2.5 s of failing control transfers, and nothing has logged that.
- **Smallest test:** a counter or a printf at that return.

### 11. Non-atomic multi-step access (suspect, weak)

- **rtw88:** `rtwdev->mutex` is held across sequences.
- **Haiku:** `usbd_do_request_flags(..., &sc->sc_mtx, ...)` (`usb/rtwn_usb_reg.c:69`) drops the driver lock during each transfer, so a read-modify-write isn't atomic against another thread's registers. Tx and Rx callbacks don't write registers, and the task queue is single, so the window is net80211 state changes only. Those don't run in a steady soak.

### 12. No thermal power tracking (suspect, weak)

- **rtw88:** swing and AGC adjust as the chip warms (5 changes in 42 s).
- **Haiku:** only the thermal read and IQK trigger (`r22b_phy.c:912-915,981-993`).
- A hotter chip stays at the cold swing. It could explain worse Tx quality on a long run, not a freeze. rtw88 does write these with traffic on, Haiku doesn't, so it can't be the writes that hang Haiku.

### 13. Receive filter differences (suspect, weak)

| Register | rtw88 | Haiku | Where |
|---|---|---|---|
| RCR bit 6 CBSSID_DATA | never | set | `if_rtwn_rx.c:574-575` |
| 0x6a0 RXFLTMAP0 | 0xffff | 0x7f7f minus 3 subtypes | `if_rtwn_rx.c:482-494` |
| 0x6a2 RXFLTMAP1 | 0xffff | BAR only, 0 until joined | `if_rtwn_rx.c:520`, `if_rtwn.c:1381` |
| 0x620 MAR | all ones | hash | `if_rtwn_rx.c:470-471` |

They mean less Rx work for Haiku, the opposite of what would worry. rtw88's filter includes BA, and whether the MAC's Tx side depends on the filter is unknown; the name says it filters what reaches the host.

### 14. Advertised capabilities (suspect, weak)

| | rtw88 | Haiku | Where |
|---|---|---|---|
| HT A-MSDU | 7935 | 3839 | `if_rtwn.c:247` |
| A-MPDU density | 2 us | 16 us | `if_rtwn.c:638` |
| VHT beamformee | SU, MU, STS 3 | none | `r22bu_attach.c:221-232` |

These change what the AP sends. Beamforming was ruled out. Under Linux the AP sounded, so rtw88 also makes CSI reports Haiku never makes: more hardware-made frames under Linux, not fewer.

### 15. IQK once per primary channel (suspect, weak)

- **rtw88:** any channel set outside a scan marks it needed, and it runs at the next `mgd_prepare_tx`.
- **Haiku:** skipped if the primary channel matches (`r22b_phy.c:1064`), so a width change alone doesn't recalibrate. Worth a look for Tx quality on 80 MHz, not for a freeze.

### 16. Things rtwn writes that rtw88 doesn't, with unknown effect (suspect, weak)

| Reg | Haiku | Where | Note |
|---|---|---|---|
| 0x423 | 0xff | `if_rtwn.c:2051` | Hardware sequence; rtw88 writes it only on older chips |
| 0x438-0x43b | 1, 2, 3, 4 | `if_rtwn.c:1610` | rtw88 leaves them; `r22b_init.c:1221-1225` restores only the rest |
| 0x553 | TSF reset at RUN | `if_rtwn.c:1026` | rtw88 doesn't |
| 0x554 | beacon interval | `if_rtwn.c:1377` | rtw88 doesn't, as a station |
| 0x00, 0x02, 0x08, 0xcf | at the efuse read | `if_rtwn_efuse.c:53-79` | Clock and isolation bits rtw88 doesn't touch, in the always-on section |

Read all of these on both systems if a register-level comparison becomes possible.

## Differences that can't matter

| Difference | Where | Why not |
|---|---|---|
| No LED, so no 0x4c traffic or its 0x4e0 writes | `r22b_init.c:1301-1304` | Software LED mode. The 0x4e0 stream under Linux is only a by-product; its absence is the one thing to keep in mind, as what 0x4e0 does is unknown |
| Rx aggregation always on | `r22bu_init.c:254-257` | The same setting as rtw88 under load. Ruled out |
| 0x510 = 0x660f | `if_rtwn.c:1922` | Beacon queue timing; a station doesn't beacon |
| 0x550 between init and RUN | `if_rtwn.c:1917` | Ends at 0x08 in RUN, as in rtw88 |
| RRSR not written before a station exists | `r22b_ra.c:554` | Before the join. Ruled out |
| 0x668 short preamble, 0x6a8 upper bits, 0x480 | `r22b_ra.c:589-606` | Restored after rtwn writes them. `r` healthy shows 0x150 and 0x930c0400 |
| USB 3 mode switch | `r22bu_init.c:274` | Linux at USB 2 with it off had no stall |
| Channel set before coex init | `if_rtwn.c:2030`, `r22b_init.c:1275` | Scan and join set the channel again, which restores 0xcbc bits 9:8 |
| No LPS, IPS, scan offload | `if_rtwn.c:221-225` | Not active under load in rtw88 either |
| `H2C_CMD_RSSI_MONITOR` value from max(A, B), not the linear average | `r22b_rx.c:240-252` | Slightly higher RSSI, the same scale |
| 0xa5c and 0xf04-0xf14 not read | `r22b_phy.c:746-748` | Nothing shows that reading has an effect; rtw88 uses them for stats |
| Register write timeouts 250 ms, not 500 ms | `usb/rtwn_usb_reg.c:70`, `r22bu_attach.c:90` | No such failures logged |
| Scoreboard not updated on traffic | absent | Only BT firmware reads it |
| Haiku's own Tx path: one frame per USB transfer | `r22bu_attach.c:257`, `usb/rtwn_usb_tx.c:182` | Costs speed. Queue depth is lower than Linux, not higher |

**Not different, checked:** power sequence, MAC init values and order, MAC, BB, AGC and RF tables and their parser, TRX mode, channel BB, MAC, RF, DFIR, CCA and RFE, BF init, DIG logic and bounds, false alarm reset order, RA mask, RA, RSSI and media status H2C layouts, RRSR computation, reg_sec, coex init including GNT and scoreboard, EOF bit, Rx aggregation values, burst size, RX descriptor parsing, Tx checksum, security setup.

---

# Where the code and the logs disagree

1. **Scoreboard during startup.** The capture has `W 0x00aa 0382` (0x8203, TDMA bit set) just after the coex table. `rtw_coex_tdma` sets that bit only if `RTW_FLAG_BUSY_TRAFFIC` is set, and at bring-up the flag should be clear. It would fit a flag left from an earlier joined run, as `rtw_core_stop` doesn't clear it. Not proven. It doesn't matter on a chip with no BT.
2. **Open Rx URBs.** Line 4 of `wifi-rtw88-c2-usb-summary.txt` says "most open at once 1, typically 0", 224 per second, 1.6 MB, for "25 s, half up, half down". The download section says 4 URBs, typically 3 open, 28 MB/s. The code says four, always. Line 4 can't cover the 12 s download (it would be over 300 MB), so it looks like the upload part only, with the open count not reliable there. I used the download section.
3. **An earlier reading (M80)** listed "the EDCA parameters (0x500-0x50c, 20 times)" among the 2 s work. The counts are right and the attribution isn't (item 4).

**Not covered by any capture, so from the code only:** channel setup (item 2) and everything at association (item 3). The startup capture reaches IPS before any channel set, and the periodic capture starts after the join.

# What couldn't be determined

- The reset value of 0x42a, 0x426, 0x438-0x43b, 0x423 and 0x76e.
- What 0x4e0 does, and what bit 3 of 0x76e does beyond its name.
- Whether the firmware or MAC honours W5 STBC on a 2SS or 1SS rate, and what value 2 does.
- Whether this AP's own A-MPDU density is below 16 us. The logs only show Haiku's floor.
- What rate or frame type was on the air at the stall. No driver-visible register named by rtw88 tells.
- The meaning of the 18 unknown registers in item 12, including 0x400 and 0x5f8.
- Which EDCCA mode applied in the Linux captures (regulatory domain not recorded).
- What the firmware does with H2C 0x58 and with reserved pages it wasn't told about.
- Whether anything reports Rx FIFO overflow.

# Sources not on disk: what they'd have answered

| Source | Questions |
|---|---|
| **mac80211** | The order of `sta_add`, BSSID and ASSOC callbacks. What calls `conf_tx` 5 times in 42 s. When idle and `cfg.ps` are set. `skb->priority` for each frame type. When `use_rts` is set, and the default `rts_threshold`. What it does for EAPOL (status request, rate). How A-MSDUs are built. How BA sessions start and when BAR is sent. The connection monitor. |
| **Linux dwc2 host driver** | How it schedules bulk in, bulk out and control on the C2. How it retries a NAKed bulk IN. How many channels it uses at once. Whether a bulk transfer is ever cut for another. This is what Armbian's 0 stalls ran on, and rtw88 says nothing about it. |
| **Realtek's out-of-tree driver (never to be used)** | Would probably name the unknown registers and the 0x42a default, and might show a hang check. Left unread, as required. A datasheet or a vendor statement would be the clean way to get these. |

# Notes for whoever changes the driver

- **The STBC soak was still running when this was written.** By 11:18 `logs/wifi-soak-295-stbc.log` had 25 rounds and no "no link" line, with rounds 11-13 much slower than the rest. Its witness log has 5 lost pings from 11:01:08 to 11:01:16. That is about the length of the TEMP 8 s silence limit, so it may be a stall and a restart, which would count against suspect 1. The board's syslog would tell, and this study was read-only. Read the finished logs before ordering the rest of this list.
- If it holds, suspect 2 is still worth doing for speed alone.
- Change one suspect per soak. A single 15-minute soak is a weak test here: the stall has come 1 per 1 to 5 GB, and M80's earlier conclusions were overturned more than once.
- Remove the TEMP code first or keep it deliberately: `temp_watch` reads RF 0 and writes 0x522, 0x02, 0x100 when it sees silence, so it can change what happens after a stall.
