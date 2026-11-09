# RTL8822BU (RTL8812BU "AC1300") in realtekwifi: port notes

Written 2026-11-02. Phase 1 adds the chip to the fork's `realtekwifi` driver (Haiku's port of FreeBSD rtwn) so that it comes up, loads its firmware and can **scan** on 2.4 and 5 GHz. **Status: compile-tested only.** Nothing has run on hardware yet. Committed in the fork as f9873097d8 (+265).

Phase 2 (same day, not committed yet) adds what station mode needs to associate and run WPA2: media status, firmware rate adaptation, IQK, the rtw88 hardware-crypto setup and the association registers. It is also compile-tested only; see "Phase 2: association and WPA2" at the end. Sections before it describe phase 1 and say where phase 2 changed things.

- Driver directory: `haiku/src/add-ons/kernel/drivers/network/wlan/realtekwifi/` (below, paths are relative to it).
- Reference: Linux rtw88 v7.0 in `deps/src/rtw88-v7.0/`, used **only under its BSD-3-Clause option**. Realtek's out-of-tree 88x2bu driver (GPL only) was not used or looked at.
- Firmware: linux-firmware `rtw88/rtw8822b_fw.bin` (161,240 bytes; version 30.20.0, H2C format 14). Install it as `/boot/system/data/firmware/realtekwifi/rtw8822b_fw.bin`. The driver asks for `rtwn-rtl8822bufw`, and `glue.c` maps that name to the file.
- Build: `cd generated.arm64 && jam -q realtekwifi` exits 0 with no warnings in the new or changed code (see "Build" below).

## Licensing

- Every file with rtw88-derived code or data (all files in `dev/rtwn/rtl8822b/` and `dev/rtwn/rtl8822b/usb/`) starts with the required SPDX BSD-3-Clause header: Realtek copyright, port authors' copyright, "Derived from Linux's rtw88 driver (v7.0), used under its BSD-3-Clause option; see LICENSE.rtw88 in this directory."
- `dev/rtwn/rtl8822b/LICENSE.rtw88` has the full BSD-3-Clause text with "Copyright(c) 2018-2019 Realtek Corporation". An identical copy is in `dev/rtwn/rtl8822b/usb/LICENSE.rtw88`, so the header's "in this directory" is literally true there as well.
- There are no wholly new files without rtw88 content; even the USB glue files mirror rtw88's usb.c. So none of them uses the MIT header.
- The changes to existing FreeBSD rtwn files are small, and those files keep their own licence headers.

## Files

### New: `dev/rtwn/rtl8822b/` (line counts from `wc -l`)

| File | Lines | Contents | rtw88 v7.0 sources |
|---|---|---|---|
| LICENSE.rtw88 | 32 | BSD-3-Clause text | - |
| r22b.h | 210 | Chip constants, 5 GHz channel lists, prototypes | rtw8822b.c `rtw8822b_hw_spec`, main.h, mac.h |
| r22b_reg.h | 399 | MAC/BB/RF/USB register definitions (rtw88 names with an `R22B_` prefix) | reg.h, rtw8822b.h, bf.h, mac.h, usb.h, sec.h (one bit) |
| r22b_var.h | 165 | `struct r22b_softc`, rqpn/page table types | main.h (`rtw_rqpn`, `rtw_page_table`, `rtw_txpwr_idx`, hal/efuse/fifo fields) |
| r22b_priv.h | 125 | Masked register helpers with rtw88 semantics (every write is a read-modify-write), H2C bit setter | hci.h (`rtw_read32_mask`, `rtw_write32_mask`, `rtw_write8_mask`, set/clr helpers) |
| r22b_table.h | 70 | Table descriptor (replaces `struct rtw_table`), pg/limit pair types | main.h, phy.h |
| r22b_table.c | 22265 | MAC/AGC/BB/RF A/RF B/by-rate/limit tables: **verbatim data**, generated (see "Regenerating r22b_table.c") | rtw8822b_table.c |
| r22b_tx_desc.h | 126 | 48-byte Tx descriptor, QSEL and RATEID values | tx.h (`struct rtw_tx_desc`, `RTW_TX_DESC_*`), main.h |
| r22b_rx_desc.h | 59 | 24-byte Rx descriptor fields, PHY status accessors | rx.h, rtw8822b.h (`GET_PHY_STAT_*`) |
| r22b_fw_cmd.h | 96 | Firmware header, H2C/C2H IDs, firmware constants | fw.h, mac.h |
| r22b_phy.c | 864 | RF read/write (direct read, SIPI write), conditional table parser, table cfg delays, LDO25, RFE/CCA, IGI toggle, TRX mode, phy init, DIG write, adaptivity, RFE init, power tracking init, BF init, thermal meter | phy.c (`rtw_phy_read_rf`, `rtw_phy_write_rf_reg_sipi`, `rtw_parse_tbl_phy_cond`, `check_positive`, `rtw_phy_cfg_*`, `rtw_phy_init`, `rtw_phy_dig_write`, `rtw_phy_set_edcca_th`), util.c (`check_hw_ready`), bf.c (`rtw_bf_phy_init`), rtw8822b.c (`rtw8822b_config_trx_mode`, `rtw8822b_toggle_igi`, `rtw8822b_set_channel_cca`, `rtw8822b_set_channel_rfe_efem/_ifem`, `rtw8822b_phy_rfe_init`, `rtw8822b_pwrtrack_init`, `rtw8822b_phy_bf_init`, `rtw8822b_adaptivity_init`, `rtw8822b_cfg_ldo25`, thermal read of `rtw8822b_pwr_track`) |
| r22b_init.c | 1289 | Power sequences, power on/off, chip ID/efuse power, TRX FIFO/priority queues, H2C queue, MAC init, MAC/BB/RF tables, post-init | mac.c (`rtw_mac_pre_system_cfg`, `rtw_pwr_seq_parser`, `rtw_pwr_cmd_polling`, `rtw_mac_power_switch`, `__rtw_mac_init_system_cfg`, `rtw_mac_power_on/off`, `txdma_queue_mapping`, `rtw_set_trx_fifo_info`, `__priority_queue_cfg`, `init_h2c`, `rtw_drv_info_cfg`, `rtw_mac_init`), rtw8822b.c (power sequences, `rtw8822b_mac_init`, `rtw8822b_phy_set_param`, `page_table_8822b`, `rqpn_table_8822b`), main.c (`rtw_power_on`, `rtw_core_start`, `rtw_chip_parameter_setup`, `rtw_chip_efuse_enable/disable`), sec.c (`rtw_sec_enable_sec_engine`) |
| r22b_fw.c | 813 | Firmware download (reserved page + DDMA), H2C mailbox commands and H2C packets, general/phydm info, LTE coex indirect registers | mac.c (`__rtw_download_firmware`, `check_firmware_size`, `wlan_cpu_enable`, `download_firmware_reg_backup`, `download_firmware_reset_platform`, `send_firmware_pkt`, `iddma_enable`, `iddma_download_firmware`, `check_fw_checksum`, `download_firmware_to_mem`, `start_download_firmware`, `download_firmware_validate`, `download_firmware_end_flow`), fw.c (`rtw_fw_write_data_rsvd_page`, `rtw_fw_send_h2c_command`, `rtw_fw_send_h2c_packet`, `rtw_fw_send_general_info`, `rtw_fw_send_phydm_info`), util.c (`ltecoex_read_reg`, `ltecoex_reg_write`, `rtw_restore_reg`), main.c (`__update_firmware_info`) |
| r22b_coex.c | 557 | Wi-Fi-only coexistence setup | coex.c (`rtw_coex_power_on_setting`, `__rtw_coex_init_hw_config`, `rtw_coex_init_coex_var`, `rtw_coex_monitor_bt_enable`, `rtw_coex_read/write_scbd`, `rtw_coex_write_indirect_reg`, `rtw_coex_coex_ctrl_owner`, `rtw_coex_set_gnt_bt/wl`, `rtw_coex_set_ant_path`, `rtw_coex_set_wl_pri_mask`, `rtw_coex_table`, `rtw_coex_set_table`, `rtw_coex_tdma`, `rtw_coex_set_tdma`, `rtw_coex_wl_slot_extend`, `rtw_coex_query_bt_info`, `rtw_coex_power_off_setting`), rtw8822b.c (`rtw8822b_coex_cfg_init`, `_cfg_ant_switch`, `_cfg_gnt_debug`, `_cfg_rfe_type`, `table_sant_8822b`), fw.c (`rtw_fw_coex_tdma_type`, `rtw_fw_bt_wifi_control`, `rtw_fw_query_bt_info`) |
| r22b_rom.c | 241 | Logical efuse map parsing, tx power index nibble layout, sanitizing, RFE to table selection, MAC address | rtw8822b.h (`struct rtw8822b_efuse`, `struct rtw8822bu_efuse`), main.h (`struct rtw_txpwr_idx`), rtw8822b.c (`rtw8822b_read_efuse`, `rtw8822bu_efuse_parsing`, `rtw8822b_rfe_defs`), main.c (`rtw_chip_efuse_info_setup`, `rtw_check_supported_rfe`) |
| r22b_txpwr.c | 746 | Tx power: by-rate and limit tables, per-rate index, TXAGC writes | phy.c (`rtw_get_channel_group`, `rtw_phy_get_2g/5g_tx_power_index`, `rtw_phy_rate_to_rate_section`, `rtw_phy_get_tx_power_limit`, `rtw_get_tx_power_params`, `rtw_phy_get_tx_power_index`, `rtw_phy_set_tx_power_level*`, `rtw_phy_get_rate_values_of_txpwr_by_rate`, `rtw_phy_store_tx_power_by_rate`, `rtw_parse_tbl_bb_pg`, `rtw_channel_to_idx`, `rtw_phy_set_tx_power_limit`, `rtw_xref_*`, `rtw_parse_tbl_txpwr_lmt`, `rtw_phy_tx_power_by_rate_config`, `rtw_phy_tx_power_limit_config`, `rtw_phy_init_tx_power`), rtw8822b.c (`rtw8822b_set_tx_power_index`, `_by_rate`) |
| r22b_chan.c | 474 | Channel/bandwidth switching (20/40/80 MHz), scan start/end IGI | main.c (`rtw_get_channel_params`, `rtw_update_channel`, `rtw_set_channel`, `rtw_core_scan_start/complete`), mac.c (`rtw_set_channel_mac`), rtw8822b.c (`rtw8822b_set_channel`, `_set_channel_bb`, `_set_channel_rf`, `_set_channel_rxdfir`), phy.c (`rtw_phy_dig_set_max_coverage`, `rtw_phy_dig_reset`) |
| r22b_rx.c | 289 | C2H dispatch (log only), C2H filter in aggregates, RSSI from PHY status pages 0 and 1, Rx bandwidth/stats | rx.c (`rtw_rx_query_rx_desc`, `rtw_rx_fill_rx_status`), rtw8822b.c (`query_phy_status`, `_page0`, `_page1`), fw.c (`rtw_fw_c2h_cmd_rx_irqsafe`) |
| r22b_tx.c | 454 | `fill_tx_desc` (data, mgmt, ctl), `_raw`, `_null`, radiotap flags, beacon desc init/rate | tx.c (`rtw_tx_fill_tx_desc`, `rtw_tx_pkt_info_update`, `rtw_tx_mgmt_pkt_info_update`, `rtw_tx_data_pkt_info_update`, `rtw_tx_pkt_info_update_rate`, `rtw_tx_rsvd_page_pkt_info_update`, `get_tx_ampdu_factor`), main.c (`get_rate_id`) |

### New: `dev/rtwn/rtl8822b/usb/`

| File | Lines | Contents | rtw88 v7.0 sources |
|---|---|---|---|
| LICENSE.rtw88 | 32 | Copy of the parent directory's file | - |
| r22bu.h | 53 | High priority queue transfer, reg_sec dummy register, prototypes | usb.h |
| r22bu_attach.c | 379 | Register access with the reg_sec quirk, all rtwn hooks and chip parameters, HT/VHT caps, scan hooks, private softc | rtw8822b.c (`rtw8822b_hw_spec`), usb.c (`rtw_usb_read`, `rtw_usb_write`, `rtw_usb_reg_sec`), main.c (`rtw_init_ht_cap`, `rtw_init_vht_cap`, `rtw_core_init`) |
| r22bu_init.c | 289 | Bulk-out endpoint and queue mapping, `sc_dma_init`, burst length, Rx aggregation, USB3 PHY parameter | usb.c (`rtw_usb_parse`, `dma_mapping_to_ep`, `rtw_usb_tx_queue_mapping_to_qsel`, `rtw_usb_init_burst_pkt_len`, `rtw_usb_interface_cfg`, `rtw_usb_dynamic_rx_agg_v1`, `rtw_usb_phy_cfg`, `rtw_usb_phy_write`), mac.c (`rtw_mac_init`, `txdma_queue_mapping`), rtw8822b.c (`usb3_param_8822b`) |
| r22bu_rx.c | 78 | Rx transfer classification (lone C2H vs data), 8-byte packet alignment | usb.c (`rtw_usb_rx_handler`) |
| r22bu_tx.c | 161 | Raw packets on the HIGH pipe: reserved pages (QSEL_BEACON, offset 48, LS) and H2C packets (QSEL_H2C, offset 0); descriptor dump | usb.c (`rtw_usb_write_data`, `rtw_usb_write_data_rsvd_page`, `rtw_usb_write_data_h2c`, `rtw_usb_write_port`), tx.c (`rtw_tx_fill_tx_desc`), rtw8822b.c (`rtw8822b_fill_txdesc_checksum`) |

The new files total 30,266 lines under `rtl8822b/`, of which 22,265 are the generated table, plus 992 under `usb/`.

### Changed existing files (`git diff --stat`: 8 files, +124 / -6)

| File | Change |
|---|---|
| Jamfile (+20) | `SEARCH_SOURCE` for `dev/rtwn/rtl8822b` and `dev/rtwn/rtl8822b/usb`; the 14 new `.c` files added to the `KernelAddon` list |
| glue.c (+1) | `{"rtwn-rtl8822bufw", "rtw8822b_fw.bin"}` in `HAIKU_FIRMWARE_NAME_MAP` |
| dev/rtwn/if_rtwnvar.h (+14) | New optional hooks `sc_fw_download` (inside `#ifndef RTWN_WITHOUT_UCODE`) and `sc_dma_init`. Both are documented, and NULL selects the old behaviour. |
| dev/rtwn/if_rtwn.c (+10/-4) | `rtwn_init()` calls `sc_fw_download` instead of `rtwn_load_firmware()`, and `sc_dma_init` instead of `rtwn_dma_init()`, when they are set |
| dev/rtwn/usb/rtwn_usb_var.h (+16) | `struct usb_config;` forward declaration; `int tx_no_own;` and the optional hook `uc_setup_ep_map` in `struct rtwn_usb_softc` |
| dev/rtwn/usb/rtwn_usb_ep.c (+9) | `rtwn_usb_setup_endpoints()` calls `uc_setup_ep_map` after the default mapping when it is set; on error it frees the config and fails |
| dev/rtwn/usb/rtwn_usb_tx.c (+2/-1) | `rtwn_usb_tx_start()` sets `RTWN_FLAGS0_OWN` only if `!uc->tx_no_own` |
| dev/rtwn/usb/rtwn_usb_attach.h (+52/-1) | `r22bu_attach` prototype, `RTWN_CHIP_RTL8822BU`, 36 USB IDs from rtw8822bu.c, attach table entry |

`usbdevs` was not changed: only the two D-Link IDs (DWA181A1 0x331e, DWA182D1 0x331c) have names there. The other 34 IDs are numeric (`RTWN_RTL8822BU_ID(vendor, product)`), each with a comment naming the device. No ID collides with an existing entry. No other chip's entries were touched.

## Core rtwn changes and why

1. **`sc_fw_download` (if_rtwnvar.h, if_rtwn.c).** `rtwn_load_firmware()` assumes an 8051 CPU: an `r92c_fw_hdr` signature, then page-by-page writes through `R92C_MCUFWDL` with `sc_fw_download_enable` and `sc_fw_reset`. The 8822B has a 3081 CPU, so its firmware has rtw88's 64-byte header with separate DMEM/IMEM/EMEM sections. Each section goes into the Tx packet buffer as beacon-queue "reserved page" packets (up to 4 KiB plus the 48-byte descriptor), and the chip's DDMA engine then copies it into the CPU memory with a checksum. That is a different mechanism, not a parameter change, hence a replaceable step. If the hook is NULL, `rtwn_load_firmware()` runs as before. The 8822B sets `sc_fw_reset` and `sc_fw_download_enable` to no-ops because nothing calls them for this chip.
2. **`sc_dma_init` (if_rtwnvar.h, if_rtwn.c).** `rtwn_dma_init()` programs the 92C-family queue model: `R92C_RQPN` page counts, `sc_llt_init`, `R92C_TRXFF_BNDY`, `R92C_TRXDMA_CTRL` QMAP and `R92C_TDECTRL`. The 8822B instead uses `REG_FIFOPAGE_INFO_1..5`, `REG_RQPN_CTRL_2`, auto-LLT (`REG_AUTO_LLT_V1`), `REG_TXDMA_PQ_MAP`, the H2C queue registers and an Rx FIFO boundary register at a different address (all from rtw88's `rtw_mac_init()`). Writing the 92C values there would corrupt the configuration. If the hook is NULL, `rtwn_dma_init()` runs as before.
3. **`uc_setup_ep_map` (rtwn_usb_var.h, rtwn_usb_ep.c).** On the 8822B every bulk-out endpoint feeds a fixed DMA priority (HIGH/NORMAL/LOW/EXTRA = endpoints 0/1/2/3). `REG_TXDMA_PQ_MAP` maps each hardware queue (QSEL) to a priority, and a frame sent on the wrong endpoint is not transmitted correctly. rtwn's default mapping (VO on endpoint 0, and so on) does not match rtw88's `rqpn_table_8822b`. The hook replaces the mapping after the default one has been computed, and it also selects the rqpn/page table for the endpoint count. If the hook is NULL, behaviour is unchanged.
4. **`tx_no_own` (rtwn_usb_var.h, rtwn_usb_tx.c).** `rtwn_usb_tx_start()` always set `RTWN_FLAGS0_OWN` (dword 0 bit 31). On the 8822B that bit is `DISQSELSEQ`, which must stay 0 for data frames that use software sequence numbers. The chip code now decides that bit itself. The default (0) keeps the old behaviour.

These are all the core changes. The register access wrappers, Tx/Rx descriptors, C2H handling, channel and power code all go through the existing rtwn hooks (`sc_read_*`/`sc_write_*`, `sc_fill_tx_desc*`, `sc_classify_intr`, `sc_check_frame`, `sc_handle_c2h_report`, `sc_set_chan`, `sc_power_on`, `sc_post_init`, ...).

## rtw88 power-on flow mapped onto `rtwn_init()`

| rtw88 (`rtw_core_start` → `rtw_power_on`) | rtwn |
|---|---|
| `rtw_mac_power_on()` (pre-system cfg, power sequence, system cfg) | `sc_power_on` = `r22b_power_on` (refuses an unsupported RFE type with `EOPNOTSUPP`) |
| `rtw_download_firmware()` | `sc_fw_download` = `r22b_fw_download` |
| (rtwn's `rtwn_mac_init()`: empty MAC table, `mac_size` 0) | - |
| `rtw_mac_init()` (TXDMA queue map, TRX FIFO info, priority queues, H2C queue, `rtw8822b_mac_init`, `rtw_drv_info_cfg`, `rtw_hci_interface_cfg`) | `sc_dma_init` = `r22bu_dma_init` → `r22b_mac_init` (fails with `EIO` if the firmware is not running), `r22bu_init_burst_pkt_len`, then the **MAC table** (`r22b_load_mac_table`) |
| rtwn generic: Rx drvinfo size, MSR, MAC address, Rx filter/RCR, retry limit, ACK timeout, beacon registers, CR MACTX/RXEN | unchanged rtwn code |
| `rtw8822b_phy_set_param()` (BB/AGC tables, RF tables, crystal cap, TRX mode, phy init, RFE, power tracking init, BF init) | `sc_init_bb` = `r22b_init_bb`, `sc_init_rf` = `r22b_init_rf` |
| `rtw_set_channel()` | `rtwn_set_chan()` → `sc_set_chan` = `r22b_set_chan` |
| rtwn generic: CAM clear, SECCFG, HWSEQ, BAR, NAV, GPIO, MRR | unchanged rtwn code; `r22b_post_init` then puts back the rtw88 values that rtwn overwrote (DARFRC/RARFRCH, BAR retry 0x0801, NAV 0xc8, DRVERLYINT 4) |
| `rtw_mac_postinit()` (nothing on the 8822B), `rtw_hci_start()` (nothing on USB), H2C general info, H2C phydm info, `rtw_coex_power_on_setting()`, `rtw_coex_init_hw_config()`; `rtw_core_start`: `rtw_sec_enable_sec_engine()` (CR bit 9) | `sc_post_init` = `r22b_post_init` (also chooses the rate control mode) |

At attach (`rtw_chip_info_setup` → `rtw_chip_efuse_enable`): `sc_read_chipid_vendor` = `r22b_read_chipid_vendor` reads the cut and RF path count from `REG_SYS_CFG1` (bit 27). It then powers the MAC on and selects efuse bank 0 with the 2.5 V LDO off, as rtw88 does before it dumps the physical efuse. Next, rtwn reads the efuse (`rtwn_read_rom`), then `sc_efuse_postread` powers the MAC off and `sc_parse_rom` parses the map. `sc_postattach` wraps `ic_scan_start/ic_scan_end` and applies the USB PHY parameters (`rtw_usb_phy_cfg`).

## USB specifics

- **Register access quirk** (`rtw_usb_reg_sec`). After every read or write in 0x00-0xff or 0x1000-0x10ff, the first byte of the data is written to register 0x4e0 with vendor request 0x05. This is a single `usbd_do_request_flags()` and its result is ignored, as in rtw88. It does not go through rtwn's 10-retry write.
- **Bulk-out mapping** (`r22bu_setup_ep_map`). The four rtwn Tx transfers are used per DMA priority:

  | rtwn transfer | endpoint | used for |
  |---|---|---|
  | `RTWN_BULK_TX_BK` | 1st bulk-out (HIGH) | H2C packets, reserved pages (firmware download), beacons, ACs mapped to HIGH |
  | `RTWN_BULK_TX_VI` | 2nd (NORMAL) | ACs mapped to NORMAL |
  | `RTWN_BULK_TX_BE` | 3rd (LOW) if there are 3 or 4 endpoints, otherwise the 2nd | ACs mapped to LOW |
  | `RTWN_BULK_TX_VO` | endpoint of the management queue's priority | all management/control frames (`rtwn_usb_tx_start()` always uses VO for them) |

  `wme2qid[]` follows rtw88's `rqpn_table_8822b`:

  | endpoints | VO | VI | BE | BK | MGMT | HI/H2C/BEACON |
  |---|---|---|---|---|---|---|
  | 2 | NORMAL | NORMAL | NORMAL | HIGH | HIGH | HIGH |
  | 3 | NORMAL | NORMAL | LOW | LOW | HIGH | HIGH |
  | 4 | NORMAL | NORMAL | LOW | LOW | EXTRA | HIGH |

  One endpoint is refused (rtw88 supports 2 to 4). The same table index selects `page_table_8822b`.
- **Raw Tx** (`r22bu_tx_raw`). It builds a 48-byte descriptor with checksum and waits up to 100 x 1 ms for a free buffer (completions run while the lock is dropped). It then queues the buffer on the `RTWN_BULK_TX_BK` transfer like a beacon (`rtwn_data` without node or mbuf), and the normal completion callback recycles it. `rtwn_usb_tx_start()` cannot be used because it overwrites the offset (H2C packets need offset 0) and chooses the pipe from the frame type.
- **Firmware chunk padding.** If (chunk + 48) is a multiple of 512, one extra zero byte is sent (rtw88 `send_firmware_pkt`). The DDMA copies only the chunk length.
- **Rx.** It uses rtwn's Rx code with 24-byte descriptors and the 32-byte driver info (PHY status, `RX_DRVINFO_SZ` 4). Packets are 8-byte aligned (`r22bu_align_rx`). A transfer is classified as C2H only if a lone C2H event fills it. C2H events aggregated with frames are caught per packet in `r22b_check_frame`, passed to the C2H handler and dropped.
- **USB PHY.** On D-cut chips, USB3 PHY register 0x01 = 0xa841 (`usb3_param_8822b`); `usb2_param_8822b` is empty.
- **Burst length.** `RXDMA_MODE` burst count/mode and burst size 1024/512/64 by USB speed; `TXDMA_OFFSET_CHK` `DROP_DATA_EN`.
- **Rx aggregation.** The low-traffic setting (size 0, timeout 1) of `rtw_usb_dynamic_rx_agg_v1(false)`.

## Firmware download (r22b_fw.c)

`firmware_get` runs with the lock dropped. The size and header are checked (`check_firmware_size`: header + DMEM + IMEM (+ EMEM) + 8-byte checksums each), and the version is printed ("firmware version 30.20.0, H2C version 14" expected). Then `__rtw_download_firmware` runs:

1. LTE coex 0x38 backup (indirect registers 0x1700/0x1704/0x1708).
2. CPU disable.
3. Register backup:
   - HIQ is mapped to HIGH.
   - `CR` = HCI_TXDMA_EN | TXDMA_EN.
   - `H2CQ_CSR` full.
   - `FIFOPAGE_INFO_1` = 0x200.
   - `RQPN_CTRL_2` | LD_RQPN.
   - `BCN_CTRL` without the beacon function.
4. Platform reset.
5. `MCUFWDL_EN`.
6. For DMEM and IMEM, in 4 KiB chunks:
   - The reserved page goes to page 0 (`FIFOPAGE_CTRL_2` = BCN_VALID | page; `CR` ENSWBCN; packet on HIGH with QSEL_BEACON, offset 48, LS), followed by a wait for `BCN_VALID`.
   - The DDMA copy runs from `0x18780000 + 48` to the section address with bit 31 cleared, with `CHKSUM_EN` and `CHKSUM_CONT` after the first chunk, and waits until `OWN` clears.
   - Finally the DDMA checksum status is checked.
7. The registers are restored.
8. End flow (`FW_DW_RDY`, `MCUFWDL_EN` off).
9. CPU enable.
10. LTE coex restore.
11. Wait for `MCUFW_CTRL & 0xc0ff == 0xc078`.
12. The H2C box and sequence are reset.

If the download fails, `rtwn_init` continues with `RTWN_FW_LOADED` clear (rtwn behaviour), and then `r22b_mac_init` fails with `EIO`, so init stops. Without running firmware, the chip is not usable.

H2C:
- 8-byte mailbox commands go through `HMEBOX0..3`/`HMEBOX_EX0..3` with the `HMETFR` busy poll (30 x 100 us).
- 32-byte H2C packets (category 1, cmd 0xff, sub ID, total length, sequence) go as QSEL_H2C on the HIGH pipe with offset 0.
- `GENERAL_INFO` (FW Tx boundary = `rsvd_fw_txbuf_addr - rsvd_boundary`) and `PHYDM_INFO` (RFE, RF type, cut, antenna) are sent in post-init.

## Tables and the conditional parser

- `r22b_table.c` is rtw88's `rtw8822b_table.c`, data unchanged. `r22b_load_table()` dispatches on the table kind instead of rtw88's function pointers:
  - MAC: byte writes.
  - BB/AGC: 32-bit writes; the special addresses 0xf9..0xfe are delays.
  - RF: 20-bit writes; 0xffe/0xfe are delays.
  - BB_PG: tx power by rate.
  - TXPWR_LMT: limits.
- The condition decoding is a port of `rtw_parse_tbl_phy_cond()` and `check_positive()`. Driver cut = `cut_version` (15 if the cut is 0), package 15, interface USB (2), and the RFE type is matched exactly, as rtw88 does. "Branch" and "else" handling also follows rtw88.
- The decoding was cross-checked offline against an independent model of the rtw88 bitfield layout (GCC little-endian bitfields). For each supported RFE and cut, both produce the same number of writes: MAC 125, AGC 521, BB 1492, RF A 411, RF B 356.
- Supported RFE types are the ones rtw88 defines for the 8822B:

  | RFE | front end | by-rate table | limit table |
  |---|---|---|---|
  | 2 | efem | pg type 2 | lmt type 2 |
  | 3 | ifem | pg type 3 | lmt type 0 |
  | 5 | ifem | pg type 5 | lmt type 5 |

  Any other RFE value makes `sc_power_on` fail with `EOPNOTSUPP`, and a message is printed at attach (same as rtw88's `rtw_check_supported_rfe`).

### Regenerating r22b_table.c

Run the script below as `python3 -I gen_table.py deps/src/rtw88-v7.0/rtw8822b_table.c .../dev/rtwn/rtl8822b/r22b_table.c`. It replaces the GPL/BSD SPDX line and the Linux includes with the BSD-3 header and a type/macro shim, and copies the data verbatim. After a regeneration, check `diff <(sed -n '9,$p' rtw8822b_table.c) <(sed -n '<data range>p' r22b_table.c)`: the data must not differ. rtw8822b_table.c v7.0 has md5 f35f83114ab96fb13685b9f4f581dbda.

```python
#!/usr/bin/env python3
# Generates r22b_table.c from rtw88's rtw8822b_table.c: the data is copied
# unchanged, only the license header and the Linux include lines are
# replaced by a small type/macro shim.
import sys

src, dst = sys.argv[1], sys.argv[2]
lines = open(src).read().split('\n')

# Expect the rtw88 file to start with the SPDX line, the Realtek notice
# and three includes, followed by a blank line.
assert lines[0].startswith('// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause'), lines[0]
assert lines[1] == '/* Copyright(c) 2018-2019  Realtek Corporation', lines[1]
assert lines[2] == ' */'
assert lines[4] == '#include "main.h"'
assert lines[5] == '#include "phy.h"'
assert lines[6] == '#include "rtw8822b_table.h"'
body = lines[8:]

header = r'''/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright(c) 2018-2019  Realtek Corporation
 * Copyright 2026, haiku-odroid-c2 port authors.
 *
 * Derived from Linux's rtw88 driver (v7.0), used under its BSD-3-Clause option;
 * see LICENSE.rtw88 in this directory.
 */

/*
 * RTL8822B MAC, AGC, BB and RF (path A/B) register tables and the tx power
 * by-rate and limit tables.  The data below is a verbatim copy of rtw88's
 * rtw8822b_table.c (v7.0); only the Linux include lines were replaced by
 * the small shim that follows.  Do not edit the data by hand: regenerate
 * it from rtw8822b_table.c instead (see docs/rtl8822b-port.md).
 */

#include <sys/param.h>
#include <sys/types.h>

#include <dev/rtwn/rtl8822b/r22b_table.h>

/* Shim for the rtw88 type and macro names used by the table data. */
#define u32				uint32_t
#define rtw_phy_pg_cfg_pair		r22b_phy_pg_cfg_pair
#define rtw_txpwr_lmt_cfg_pair		r22b_txpwr_lmt_cfg_pair

#define R22B_NITEMS(_a)			(sizeof(_a) / sizeof((_a)[0]))
#define R22B_TBL_rtw_phy_cfg_mac	R22B_TABLE_CFG_MAC
#define R22B_TBL_rtw_phy_cfg_agc	R22B_TABLE_CFG_AGC
#define R22B_TBL_rtw_phy_cfg_bb		R22B_TABLE_CFG_BB

#define RTW_DECL_TABLE_PHY_COND(name, cfg_fn)				\
const struct r22b_table name ## _tbl = {				\
	.data = name,							\
	.size = R22B_NITEMS(name),					\
	.cfg = R22B_TBL_ ## cfg_fn,					\
	.rf_path = 0,							\
}

#define RTW_DECL_TABLE_RF_RADIO(name, path)				\
const struct r22b_table name ## _tbl = {				\
	.data = name,							\
	.size = R22B_NITEMS(name),					\
	.cfg = R22B_TABLE_CFG_RF,					\
	.rf_path = R22B_RF_PATH_ ## path,				\
}

#define RTW_DECL_TABLE_BB_PG(name)					\
const struct r22b_table name ## _tbl = {				\
	.data = name,							\
	.size = R22B_NITEMS(name),					\
	.cfg = R22B_TABLE_BB_PG,					\
	.rf_path = 0,							\
}

#define RTW_DECL_TABLE_TXPWR_LMT(name)					\
const struct r22b_table name ## _tbl = {				\
	.data = name,							\
	.size = R22B_NITEMS(name),					\
	.cfg = R22B_TABLE_TXPWR_LMT,					\
	.rf_path = 0,							\
}
'''

footer = r'''
#undef u32
#undef rtw_phy_pg_cfg_pair
#undef rtw_txpwr_lmt_cfg_pair
'''

# Drop trailing empty lines of the body, then append the footer.
while body and body[-1] == '':
    body.pop()

out = header + '\n' + '\n'.join(body) + '\n' + footer
open(dst, 'w').write(out)
print('wrote', dst, len(out.split('\n')), 'lines')
```

## Channel switching (r22b_chan.c)

- `r22b_set_chan` derives the center channel, bandwidth (20/40/80, from net80211's HT40U/D and VHT80 flags), primary channel index and `cch_by_bw[]` as `rtw_get_channel_params` does. It then runs `rtw8822b_set_channel` in rtw88's order: `set_channel_bb`, `rtw_set_channel_mac`, `set_channel_rf`, `set_channel_rxdfir`. After that come `toggle_igi`, CCA and RFE (efem/ifem by RFE type), and finally the tx power for the new channel.
- The scan hooks set IGI 0x1c (`rtw_phy_dig_set_max_coverage`) at scan start and restore the IGI read after BB init (`rtw_phy_dig_reset`) at scan end.
- There is no IQK on a channel change yet (see TODO).

## Tx power (r22b_txpwr.c): full computation, simplified regulatory part

This is the full rtw88 computation: per-rate index = efuse base (channel group, bandwidth and stream-count differences) + min(by-rate offset, limit). It is packed four rates per dword into TXAGC 0x1d00 (path A) and 0x1d80 (path B), as in `rtw8822b_set_tx_power_index`. It differs from rtw88 in these ways:

- **Only worldwide limits.** The `RTW_REGD_WW` entry is the minimum over all regulatory domains in the table; rtw88 picks the domain from the country code.
- **No SAR** (rtw88 without a SAR configuration behaves the same).
- **No power tracking remnant** (always 0 on the 8822B in rtw88 as well).
- **Rate sections.** Only the ones the 8822B writes (CCK, OFDM, HT 1SS/2SS, VHT 1SS/2SS).
- **Clamping.** The result is clamped to [0, 63] (rtw88 clamps only the upper end).
- **Unprogrammed efuse.** An unprogrammed (0xff) efuse base index is replaced by 0x20 rather than used as is.

The efuse values are sanitised as in rtw88 (`rtw_chip_efuse_info_setup`):
- crystal cap, PA/LNA types, board option and regd 0xff → 0
- channel plan 0xff → 0x7f

## Coexistence (r22b_coex.c): Wi-Fi-only subset

**Ported** (what rtw88 runs on a device without a BT core before it sets `coex->stop_dm`):
- `rtw_coex_power_on_setting`:
  - `cfg_init`: PTA, `BT_DIS` monitoring, the BT/WL scoreboard.
  - Antenna path `COEX_SET_ANT_POWERON`.
  - Table case 1 only when the antenna is shared.
  - `bt_wifi_control` H2C.
- `__rtw_coex_init_hw_config(wifi_only)`:
  - `cfg_rfe_type`, `cfg_ant_switch` (BBSW control only).
  - GNT_BT forced low (SW), GNT_WL forced high (SW), through the LTE indirect registers.
  - WL priority mask.
  - Scoreboard "active | onoff".
  - Antenna path `WONLY`.
  - TDMA off (scoreboard; H2C 0x60 only if the antenna is shared).
  - WL slot extend (H2C 0x69 {0x0c, 0x01}).
  - BT info query (if BT is not disabled).
- `rtw_coex_power_off_setting` (scoreboard cleared, GNT back to HW).

**Left out:**
- The coexistence state machine.
- BT info/C2H handling.
- TDMA and table switching on scan, connect and band changes.
- LPS interaction.
- BT Rx gain and Tx power control.
- The other antenna switch control types.

A chip whose efuse says BT coexistence (board option bits 7:5 == 1) gets the same Wi-Fi-only setup and a warning.

## Tx descriptor (r22b_tx.c)

Data frames:
- QSEL = TID from the QoS header (0 for non-QoS data), like rtw88's `skb->priority`.
- MACID.
- RATE_ID from `get_rate_id` (band, bandwidth, stream count).
- Rate from rtwn/net80211 with `USE_RATE` (and `DISDATAFB` when the rate is forced).
- Bandwidth/data_sc, short GI.
- A-MPDU (`AGG_EN`, density, max agg).
- RTS at 24 Mbit/s above the RTS threshold.
- Software sequence number: from the header, or from `ni_txseqs` for A-MPDU.
- The checksum and length/offset are filled in by `rtwn_usb_tx_start`.

Management and control frames, as in `rtw_tx_mgmt_pkt_info_update`:
- QSEL_MGMT.
- Forced rate without fallback.
- Hardware sequence number (`EN_HWSEQ`, `DISQSELSEQ`, `HW_SSN_SEL` = port).

`fill_tx_desc_raw` and `_null` follow the same rules. The beacon descriptor (QSEL_BEACON) is set up for completeness, but beaconing is off.

Phase 2 changed the data path (firmware rate adaptation) and treats (QoS) Null frames like management frames; see "Phase 2".

## Simplified or stubbed in phase 1

(As of phase 1. Phase 2 implemented rate control, media status, RSSI, C2H rate reports and IQK; see "Phase 2".)

- **Rate control.** No firmware rate adaptation. `r22b_post_init` picks `RTWN_RATECTL_NET80211` (like the RTL8812AU code), but there are no C2H tx reports (`sc_handle_tx_report*` are no-ops), so net80211 rate control gets no feedback.
- **Media status H2C** (`r22b_set_media_status`): logs only.
- **RSSI to firmware** (`sc_set_rssi`): no-op.
- **Reserved pages and power save** (`r22b_set_rsvd_page`, `r22b_set_pwrmode`): return `EOPNOTSUPP`. They are not reached because `IEEE80211_C_PMGT` is off.
- **C2H events**: parsed and logged only (ID and sequence).
- **IQK** (`r22b_iq_calib`) and **LCK** (`r22b_lc_calib`): stubs with TODOs. rtw88 runs IQK through `H2C_PKT_IQK` after a channel change; it has no separate LCK for the 8822B. Thermal measurement works (`r22b_temp_measure`/`r22b_temp_read`, RF 0x42), but power tracking does not act on it.
- **LED** (`r22b_set_led`): empty, with the register recipe in a comment.
- **Tx aggregation** (`tx_agg_desc_num` 1, `sc_init_tx_agg` no-op) and **dynamic Rx aggregation** (fixed low-traffic setting).
- **Beamforming.** Only the static `rtw_bf_phy_init`/`rtw8822b_phy_bf_init` registers; no beamformee capabilities are advertised.
- **HT/VHT capabilities.**
  - Advertised: HT 40 MHz, short GI 40, Rx LDPC, Rx STBC 1; VHT 11454 MPDU, short GI 80, Rx STBC 1, HTC-VHT, Rx LDPC, max A-MPDU exponent 7, Tx STBC with 2 chains.
  - Not advertised: Tx LDPC, Tx STBC (HT), beamformee.
- **Operating modes.** IBSS and HOSTAP are removed: beacons need the reserved-page download path. Monitor mode was not looked at.
- **DIG / false alarm / CCK PD / EDCCA.** Static: IGI from the tables, 0x1c while scanning, and the static adaptivity init. (Since +290 DIG and the false alarm counters run every 2 s while associated: `r22b_phy_dynamic_mechanism()`. On Linux's watchdog and not here: power tracking, CFO tracking, dynamic Rx aggregation.)
- **Regulatory.** Worldwide limits only; the efuse country/channel plan is parsed but not used.
- **Efuse capabilities.** No HW feature report (C2H 0x19) at attach; the capabilities come from `REG_SYS_CFG1`.
- **USB3 mode switch** (`rtw_usb_switch_mode`): not done.
- **Rx radiotap flags.** 0, and no A-MPDU/short GI/LDPC/STBC Rx flags (rtw88 does not parse them either).
- **CTS-to-self** (`IEEE80211_BPF_CTS`, protection mode): TODO.

## Phase 2 TODO: association and WPA2 (station mode)

(The list as planned after phase 1; the "Phase 2" section says what was done for each item.)

1. **Media status report** in `r22b_set_media_status`: `H2C_CMD_MEDIA_STATUS_RPT` (0x01), dword 0 BIT(8) = connected, [23:16] = macid (`rtw_fw_media_status_report`). Called from rtwn's newassoc and node-free paths.
2. **Rate control.** Pick one:
   - (a) Firmware RA: `H2C_CMD_RA_INFO` (0x40, `rtw_fw_send_ra_info`: rate mask from the node's rates/HT/VHT MCS, bandwidth, SGI, VHT enable, RATE_ID, `is_disable_ra` false), `H2C_CMD_RSSI_MONITOR` (0x42) periodically from rtwn's watchdog, `C2H_RA_RPT` handling, and `USE_RATE` left clear for unicast data when `sc_ratectl == RTWN_RATECTL_FW`.
   - (b) net80211 rate control with tx reports: `SPE_RPT` + `SW_DEFINE` in the Tx descriptor, and `C2H_CCX_TX_RPT` (0x03, or the HALMAC 0xff sub-ID) parsed in `sc_handle_tx_report*` / `r22b_handle_c2h_report`.

   rtw88 does (a). (b) also gives EAPOL tx status.
3. **IQK** after every channel change before transmitting (`rtw8822b_do_iqk`):
   - Send `H2C_PKT_IQK` (sub ID 0x0e) with `clear` = 0 and `segment_iqk` = 0.
   - Poll RF A 0x08 (`RF_DTXLOK`) until it reads 0xabcde (up to 300 x 20 ms).
   - Write 0 back to that register.

   Without it, Tx EVM may be poor, especially at 5 GHz.
4. **Security.**
   - CR bit 9 (security engine) is set in post-init as of phase 1 (`rtw_sec_enable_sec_engine`).
   - Check that rtwn's CAM writes (`R92C_CAMCMD` 0x670, `R92C_CAMWRITE` 0x674, 8 dwords per entry, 32 entries) match rtw88's sec.c layout. They should: the same register addresses and entry format.
   - rtwn's SECCFG for `RTWN_CRYPTO_PAIR` (`TXUCKEY_DEF | RXUCKEY_DEF | TXENC | RXDEC | MC_SRCH_DIS`) differs from rtw88 (default key search also for broadcast: `TX_BC_USE_DK | RX_BC_USE_DK`); test group-key decryption.
   - The Tx `SEC_TYPE` (dword 1 bits 23:22) comes from rtwn's common code (1 = WEP/TKIP, 3 = CCMP), the same values as rtw88.
5. **BSS registers** on association. rtw88's `rtw_vif_port_config` (port 0) writes:
   - Net type: `REG_CR` + 2, the same register as rtwn's `R92C_MSR` 0x102.
   - BSSID: 0x618.
   - AID: 0x6a8 bits 10:0.
   - `BCN_CTRL` 0x550 = `EN_BCN_FUNCTION` for a station; rtwn's `r92c_sta_beacon_enable` is used now.

   rtw88's `rtw_conf_tx` writes the slot time and EDCA. rtwn's generic association code in `if_rtwn.c` writes:
   - `R92C_MSR`.
   - `R92C_BCN_PSR_RPT` (0x6a8) = 0xc000 | AID. rtw88 writes only bits 10:0, so check what bits 15:14 do on the 8822B.
   - BSSID.
   - Beacon interval.
   - `R92C_RRSR` 0x440.
   - `R92C_SLOT` 0x51b.

   Compare these with a Linux register dump after association.
6. **EAPOL.** Keep it at a low forced rate (rtwn does) and confirm that the frames go out on the right endpoint (data QSEL = TID; the AC chosen by `M_WME_GETAC` must agree with the TID, see the rtwn "consistent AC/TID" comment in `rtwn_usb_tx_start`).
7. **Multicast/broadcast data.** rtw88 uses QSEL_HIGH on the HIGH endpoint. In station mode addr1 is the BSSID, so this only matters for AP/IBSS; keep it in mind.
8. **Watchdog-driven dynamic Rx aggregation** (`rtw_usb_dynamic_rx_agg_v1(true)` at >= 1 Mbit/s) and, for throughput, USB Tx aggregation.

## Later phases

(Updated in phase 2.)

- Thermal Tx power tracking (`rtw8822b_pwr_track`: BB swing / TXAGC offset tables). Phase 2 has only the thermal IQK trigger.
- DIG / false-alarm statistics, CCK PD, dynamic EDCCA.
- LPS / power save with reserved pages (null data, PS-Poll, QoS null).
- LED.
- Beamforming (`bf.c`).
- AP/IBSS beacons through reserved pages.
- Regulatory domain selection from the efuse/country.
- HW feature report at attach.
- USB3 mode switch.
- CTS-to-self/protection.
- Full BT coexistence for combo chips.

## Uncertainties (check first on hardware)

1. **Efuse read at attach.** `r22b_read_chipid_vendor` powers the MAC on (mirroring rtw88's `rtw_chip_efuse_enable`), and `r22b_efuse_postread` powers it off. Between the two, rtwn's generic `rtwn_efuse_read` runs, which also does rtwn's `rtwn_efuse_switch_power()` (writes `R92C_EFUSE_ACCESS` 0xcf = 0x69 and touches `SYS_ISO_CTRL`/`SYS_FUNC_EN`/`SYS_CLKR` bits). Those 92C-era bits may not mean the same on the 8822B. If the efuse map reads as all 0xff or garbage, look here first.
2. **The 0x4e0 dummy writes** also happen before the chip is powered on (rtw88 does the same); any failure is ignored.
3. **Endpoint mapping.**
   - The order is assumed to be bulk-out endpoints in descriptor order = HIGH, NORMAL, LOW, EXTRA (rtw88 `dma_mapping_to_ep`).
   - The 4-endpoint case sends management frames to EXTRA through the VO transfer.
   - Adapters are expected to have 3 or 4 bulk-out endpoints; check with `listusb -v`.
4. **Ordering against rtwn's generic writes.**
   - The MAC table is loaded in `sc_dma_init`, that is before rtwn's own register writes. rtwn then writes: RL 0x3030, `HWSEQ_CTRL` 0xff, `BCNTCFG` 0x660f, `TBTT_PROHIBIT` 0x6404, `BCNDMATIM` 2, `RARFRC` 0x438-0x43b = 1..4, `GPIO_MUXCFG` ENBT cleared, `FWHW_TXQ_CTRL` AMPDU_RTY_NEW, `ACKTO` 0x40.
   - `r22b_post_init` restores only DARFRC/RARFRCH, `BAR_MODE_CTRL`+2, `NAV_UPPER` and `DRVERLYINT`. The others keep the rtwn values. Compare a register dump with Linux.
5. **H2C packet descriptor.** Offset 0 and no LS flag, as rtw88's `rtw_usb_write_data_h2c`. If H2C packets do not arrive, compare with a Linux USB capture.
6. **`DISQSELSEQ` handling.** Set (with `EN_HWSEQ`) only for frames that use the hardware sequence number; data uses software sequence numbers.
7. **Zero-length packets.** rtwn's Tx transfers use `force_short_xfer`. The compatibility layer ignored that until +296, and now sends one after a multiple of the packet size. Rare (3 in 22 GB).
8. **Rx shift** is assumed 0 (`RXSHFT_EN` is not set); the parser still honours the shift field.
9. **C2H in aggregated Rx transfers** is handled per packet. If rtwn drops short C2H packets (< 10 bytes) before `sc_check_frame`, they are lost, which is harmless while C2H is only logged. (Phase 2 handles the short ones in `r22bu_classify_intr`.)
10. **RFE types.** Only 2, 3 and 5 are supported (as rtw88). Typical RTL8812BU adapters report RFE 3 or 5; others refuse to start.
11. **Firmware download on first boot.** `rsvd_boundary` is 0 until `r22b_mac_init` runs, so the "restore" write after each reserved page writes 0 | BCN_VALID to `FIFOPAGE_CTRL_2`. rtw88 does the same on the first download.
12. **Polling timeouts** are longer than rtw88's in some places (USB register reads are slow): `check_hw_ready` 1000 x 10 us, power sequence polls 1 s.
13. **Endianness.**
   - Descriptors are built with `htole32`/`htole16`, H2C payloads with `le32enc`/`le32dec`, and tables are host-order `uint32_t` arrays.
   - The efuse map is bytes.
   - The firmware header fields are converted with `le16toh`/`le32toh`.
   - The sequence number is read with `le16dec` (no unaligned access).
   - arm64 is little-endian, so mistakes here would not show up yet.
14. **Firmware failure is fatal** for `rtwn_init` (an `EIO` from `r22b_mac_init`). rtwn's other chips keep running without firmware.

## Build

```
cd /build/haiku-odroid-c2/generated.arm64 && jam -q realtekwifi
...updated 8 target(s)...   (exit 0)
```

- No warnings or errors in the new files or in the changed lines.
- The `if_rtwn.c` sign-compare warnings are pre-existing: the same 4 as at HEAD, with line numbers shifted by +6.
- The `rtwn_usb_rx.c` warnings are pre-existing; that file is unchanged.
- "package zstd not available" is normal.

## Test hints (phase 1)

- **Attach (syslog).** Look for:
  - The chip name "RTL8822BU".
  - Cut and 1T1R/2T2R.
  - The efuse info line (RFE type, crystal cap, MAC address).
  - "firmware version 30.20.0, H2C version 14".
- **Debug output.** `RTWN_DEBUG_RESET`, `_FIRMWARE`, `_USB` and `_INTR` show the power sequence, download, endpoint mapping and C2H IDs.
- **Comparison with Linux.** Use usbmon on the same adapter for:
  - the vendor-request register write sequence after `rtw_power_on`
  - the first H2C packets (general/phydm info)
  - the TXAGC values per channel
- **Scan test.** `ifconfig <dev> scan` on 2.4 and 5 GHz; beacons and probe responses should appear with sensible RSSI.

## Phase 2: association and WPA2 (station mode)

Phase 2 adds what station mode needs to associate and use WPA2 with hardware crypto. It builds on f9873097d8 in the same files plus one new file. **Compile-tested only.** It is not committed.

Out of scope, as agreed: AP/IBSS, more beamforming, power save (stays off), Tx/Rx aggregation tuning (Rx aggregation as in phase 1, USB Tx aggregation off) and LED.

### Files (phase 2)

| File | Lines now | Change (`git diff --numstat`, +/-) |
|---|---|---|
| rtl8822b/r22b_ra.c | 845 | **new**: station state, firmware RA, media status, RSSI monitor, C2H rate report, RRSR, association registers |
| rtl8822b/r22b_tx.c | 537 | +109/-26: Tx descriptor for firmware RA; Null frames like management; MAC id before association |
| rtl8822b/r22b_rx.c | 379 | +126/-36: C2H walk/dispatch (rate report), SWDEC handling |
| rtl8822b/r22b_phy.c | 931 | +80/-13: IQK (`r22b_do_iqk`), IQK policy (`r22b_iqk_check`), `sc_iq_calib`; LCK documented as not needed |
| rtl8822b/r22b.h | 251 | +42/-1: rate/wireless-set/STBC/LDPC constants, prototypes |
| rtl8822b/r22b_fw_cmd.h | 137 | +41: H2C field masks (RA info, RSSI, media status, default port, IQK), C2H RA report layout |
| rtl8822b/r22b_var.h | 206 | +41: `struct r22b_sta_info`, station table, saved registers, IQK record |
| rtl8822b/r22b_init.c | 1304 | +20/-5: post-init saves registers, resets station state, selects firmware RA |
| rtl8822b/r22b_reg.h | 414 | +15: RRSR, INIRTS, port-0 AID, rtwn's short-preamble bit |
| rtl8822b/r22b_chan.c | 485 | +14/-3: IQK after a channel change, registers again after a scan |
| rtl8822b/r22b_fw.c | 815 | +12/-10: media status stub replaced by `r22b_fw_do_iqk` (H2C_PKT_IQK) |
| rtl8822b/usb/r22bu_attach.c | 399 | +24/-4: `sc_set_rssi`, crypto mode, CAM group bit, temperature delta |
| rtl8822b/usb/r22bu_rx.c | 81 | +17/-14: transfer classification by walking all packets |
| Jamfile | | +1: `r22b_ra.c` |
| dev/rtwn/if_rtwnvar.h | | +6: `cam_group_bits` (core) |
| dev/rtwn/if_rtwn_cam.c | | +3/-1: use `cam_group_bits` (core) |

Every file under `rtl8822b/` keeps the BSD-3 rtw88 header. `r22b_ra.c` has it too, because it ports rtw88 code. The two core files keep their FreeBSD headers.

### 1. Media status (r22b_ra.c)

- rtwn calls `sc_set_media_status` from its command queue in two cases:
  - **Connect:** `rtwn_newassoc()` passes `id | RTWN_MACID_VALID`. For a station this happens when net80211 enters RUN, right after `rtwn_run()`.
  - **Disconnect:** `rtwn_node_free()` passes the plain id.
- **Connect**, as rtw88's `rtw_sta_add()` does:
  1. Look up the node under the node-table lock.
  2. Build the station's RA state (`r22b_update_sta_info`, below).
  3. Send `H2C_CMD_RA_INFO` with the mask reset and `init_ra_lv` 1.
  4. Send `H2C_CMD_MEDIA_STATUS_RPT` (0x01, BIT(8) = connected, [23:16] = MAC id).
  5. For a station: write the association registers (item 5) and send `H2C_CMD_DEFAULT_PORT` (0x2c, port and MAC id; rtw88's `rtw_fw_default_port`).
- **Disconnect** (rtw88's `rtw_sta_remove()`):
  1. Send the media status with BIT(8) clear.
  2. When the last station has left, clear the AID in 0x6a8 (rtw88's `rtw_vif_assoc_changed(NULL)`) and forget the IQK channel.
- MAC ids are rtwn's node ids. The AP gets 0, the same as rtw88's interface MAC id for a station.
- Before association rtwn has no id. Management frames then use MAC id 0, as rtw88 does (in phase 1 they used 0x7f).

rtw88 sources: main.c `rtw_sta_add`, `rtw_sta_remove`, `rtw_vif_assoc_changed`; fw.c `rtw_fw_media_status_report`, `rtw_fw_default_port`; mac80211.c `rtw_ops_bss_info_changed`.

### 2. Rate control (r22b_ra.c, r22b_tx.c, r22b_rx.c)

**Firmware rate adaptation, as rtw88 does it.**

`r22b_update_sta_info()` is `rtw_update_sta_info()` with the net80211 node as input:

| rtw88 input | net80211 equivalent |
|---|---|
| VHT Rx MCS map | `ni_vht_mcsinfo.rx_mcs_map` |
| HT MCS set | `ni_htrates` |
| legacy `supp_rates` | `ni_rates` |
| HT/VHT capabilities (STBC/LDPC/SGI) | `ni_htcap` / `ni_vhtcap` |
| bandwidth | net80211's `ieee80211_vht_check_tx_bw()` / `ieee80211_ht_check_tx_ht40()` |
| band | the node's channel |

Ported as in rtw88:
- The rate-mask rules: masks per NSS and band, `RA_MASK_*_IN_*`, the RSSI-level mask (`rtw_rate_mask_rssi`) and `rtw_rate_mask_recover`.
- `get_rate_id` with `tx_num` = the chip's stream count.
- The RRSR init value per band.
- The "highest" initial data rate from `rtw_tx_data_pkt_info_update`.

Not ported: rtw88's configured rate mask (`rtw_rate_mask_cfg`); net80211 has no per-station mask, and a fixed rate goes through rtwn's `ucastrate`. Two quirks are kept on purpose:
- VHT on 2.4 GHz gives rate id 0, as in rtw88. net80211 does not negotiate it.
- The VHT initial rate is taken from the station's *Tx* MCS map, as in rtw88.

**H2C commands.**
- `H2C_CMD_RA_INFO` (0x40): MAC id, rate id, init RA level, SGI, bandwidth, LDPC, no-update, VHT, `DIS_PT` = 1, and the low 32 bits of the mask (the 8822B has no `RA_INFO_HI`).
- `H2C_CMD_RSSI_MONITOR` (0x42): MAC id, RSSI, STBC.

**Periodic work** (`sc_set_rssi` = `r22b_set_rssi`). rtwn runs it every 2 s from its calibration callout while `sc_ratectl` is `RTWN_RATECTL_FW`; that matches rtw88's watchdog period. For every station it:
1. Computes the RSSI level (`rtw_phy_get_rssi_level`).
2. Sends the RSSI monitor command.
3. On every fourth run, rebuilds the RA info and sends it with `no_update` set (`rtw_phy_ra_info_update`).
4. Updates the RRSR.

The RSSI is rtwn's per-node `avg_pwdb` (signal dBm + 100, 0..100), the same scale as rtw88's average of `pkt_stat->rssi`. With no samples yet it is 0, like rtw88's EWMA at start.

**C2H rate report** (`C2H_RA_RPT` 0x0c: rate/SGI, MAC id, bandwidth). `r22b_handle_ra_report()` stores the rate for the RRSR and sets net80211's transmit rate of the node:
- legacy: `ieee80211_node_set_txrate_dot11rate`
- HT: `ieee80211_node_set_txrate_ht_mcsrate`
- VHT: `ieee80211_node_set_txrate_vht_rate`

`ifconfig` (media status of the BSS node) therefore shows the rate the firmware uses.

**Rate control mode.** `r22b_post_init` selects `RTWN_RATECTL_FW` whenever the firmware runs and the `ratectl` setting is not 0. rtwn's default setting (1, net80211) is also mapped to firmware RA, because net80211 rate control would need per-frame Tx reports, which are not requested. Setting 0 (fixed rates) is honoured.

**Tx descriptor**, following `rtw_tx_pkt_info_update`:
- **Unicast data with firmware RA** (station known to the firmware, rate not forced):
  - `USE_RATE` and `DISDATAFB` clear, so the firmware picks the rate and SGI.
  - `DATARATE` = the initial rate.
  - The station's rate id, bandwidth and STBC.
  - The LDPC bit only for HT LDPC; rtw88's `le32_encode_bits()` drops the VHT value (2) in the 1-bit field.
  - The short-preamble bit is not set.
  - In the window between RUN and the media status command, data still goes out with rtwn's fixed rate and `USE_RATE`.
- **Forced rate** (`USE_RATE` + `DISDATAFB`):
  - **EAPOL:** rtwn forces the management rate (lowest rate of the band) and these frames use the management rate id, B_20M on 2.4 GHz or G on 5 GHz, as in `rtw_tx_pkt_info_update_rate`. rtw88 v7.0 has **no** special EAPOL case in tx.c: EAPOL goes out like other data, with firmware RA on the VO queue. Our choice (lowest rate, no fallback) is the conservative one; it is rtwn's behaviour and rtw88's management-frame rule.
  - **Multicast:** rate id BG, as rtw88's defaults.
  - **Fixed `ucastrate`.**
- **Management, control and (QoS) Null frames** (rtw88's `ieee80211_is_nullfunc()` path):
  - Management rate without fallback.
  - Hardware sequence number.
  - Null frames keep the TID queue (on USB only management and control frames go to QSEL_MGMT).

**C2H reception.** rtwn's Rx code drops packets shorter than an ACK frame (10 bytes) before `sc_check_frame` sees them, and a rate report is 9 bytes. The handling is therefore split:
- `r22bu_classify_intr` walks all packets of a transfer:
  - **only C2H packets:** `RTWN_RX_OTHER`; `r22b_handle_c2h_report` handles each of them.
  - **mixed transfer:** the short C2H packets are handled here, the rest in `r22b_check_frame`.
- Each packet is handled exactly once.
- rtwn still counts the dropped C2H packets as input errors (cosmetic).

rtw88 sources: main.c `rtw_update_sta_info`, `get_rate_id`, `get_vht_ra_mask`, `rtw_rate_mask_rssi`, `rtw_rate_mask_recover`; fw.c `rtw_fw_send_ra_info`, `rtw_fw_send_rssi_info`, `rtw_fw_ra_report_handle`, `rtw_fw_ra_report_iter`, `rtw_fw_c2h_cmd_handle`; phy.c `rtw_phy_get_rssi_level`, `rtw_phy_stat_rssi`, `rtw_phy_ra_info_update`; tx.c `rtw_tx_pkt_info_update`, `rtw_tx_data_pkt_info_update`, `rtw_tx_mgmt_pkt_info_update`, `rtw_tx_pkt_info_update_rate`, `get_highest_ht_tx_rate`, `get_highest_vht_tx_rate`; usb.c `rtw_usb_tx_queue_mapping_to_qsel`, `rtw_usb_rx_handler`.

### 3. IQ calibration (r22b_phy.c, r22b_fw.c, r22b_chan.c)

**`r22b_do_iqk()`** is `rtw8822b_do_iqk()`:
1. Send `H2C_PKT_IQK` (sub ID 0x0e, `clear` 0, `segment_iqk` 0).
2. Poll RF A 0x08 until it reads 0xabcde, up to 300 x 20 ms. The delay drops the driver lock.
3. Write 0 to that register.
4. Read `REG_IQKFAILMSK` (0x1bf0) for the debug output.

A timeout prints "IQK did not finish"; rtw88 only logs at debug level.

**When it runs** (`r22b_iqk_check`). rtw88 sets `need_rfk` on every channel change outside a scan and calibrates in `rtw_chip_prepare_tx()`, before authenticating. Here the IQK runs once per primary channel, only when the driver is running, the firmware is up and net80211 is not scanning. In practice:
- **At join:** net80211 tunes to the BSS channel from its channel task, after the scan has cleared `IEEE80211_F_SCAN`. `r22b_set_chan` → IQK. That is before authentication, like rtw88.
- **Scan hops:** no IQK. Returning to the BSS channel after a background scan happens while `F_SCAN` is still set, and the channel is already calibrated anyway.
- **At init:** no IQK (`RTWN_RUNNING` is not set yet).
- **Fallback at connect:** `r22b_set_media_status` runs it if the channel was not calibrated.
- **Later bandwidth change:** net80211's HT/VHT adjustment after association changes the width but not the primary channel, so there is no second IQK during the EAPOL exchange.
- **Temperature:** rtwn's `rtwn_temp_calib()` calls `sc_iq_calib` when the thermal meter moved by more than `temp_delta` from the last calibration. `temp_delta` is set to 7 so that this means >= 8, rtw88's `iqk_threshold` (`rtw_phy_pwrtrack_need_iqk`). `r22b_iq_calib` runs the IQK then while a station is connected.
- **Reset:** the record is cleared at power on and when the last station leaves.

**LCK:** rtw88 has none for the 8822B, so `sc_lc_calib` stays empty, now documented as such rather than as a TODO.

### 4. Security: rtw88's hardware crypto setup (r22bu_attach.c, core)

rtw88 (sec.c, mac80211.c `rtw_ops_set_key`) uses "default key search":
- **Group keys:** in CAM entries 0..3 (by key index), broadcast address, `group` bit (BIT(6) of the first word).
- **Pairwise keys:** from entry 4, with the peer address.
- **Entry layout:** 8 words per entry: control word, MAC, 4 key words, 2 zero words. The control word is written last.
- **`REG_SECCFG`** = `TX_UNI_USE_DK | RX_UNI_USE_DK | TX_DEC_EN | RX_DEC_EN | TX_BC_USE_DK | RX_BC_USE_DK` = 0x00cf.
- **`REG_CR` BIT(9)** (security engine), which is set since phase 1.

That is rtwn's `RTWN_CRYPTO_FULL` mode:
- `rtwn_key_alloc` puts global keys at their key index and pairwise keys from entry 4.
- `rtwn_key_set_cb0` writes the same registers (0x670/0x674) with the same control-word layout (key id [1:0], algorithm [4:2], valid BIT(15), MAC). The algorithm values match too: WEP40 1, TKIP 2, AES 4, WEP104 5.
- `rtwn_init_seccfg` writes 0x00cf.

Changes:
- **`r22bu_attach`** switches rtwn's default `RTWN_CRYPTO_PAIR` to `RTWN_CRYPTO_FULL`, so group keys are decrypted in hardware too. An explicit `hwcrypto` = 0 (software) is kept. An explicit 1 cannot be told apart from the default, so it becomes 2.
- **Core:** the `group` bit is the only layout difference. The new `rtwn_softc.cam_group_bits` is ORed into the control word of global keys in FULL mode. It is 0 for every other chip and 0x40 on the 8822B.
- **Rx (`r22b_check_frame`):** rtw88 treats a frame as hardware-decrypted only if `SWDEC` (Rx dword 0 BIT(27)) is clear and the encryption type is not none. rtwn's `rtwn_rx_common()` looks only at the type. For `SWDEC` frames the cipher field is now cleared, so rtwn does not mark them `M_WEP` and net80211 decrypts them in software instead of dropping them.
- **Tx:** `SEC_TYPE` comes from rtwn's common code with rtw88's values (1 = WEP/TKIP, 3 = CCMP). The IV/CCMP header is built by net80211, as rtw88 asks mac80211 to do (`GENERATE_IV`). TKIP MIC is in software in both.

Hardware group-key decryption could be made to match rtw88, so software crypto for group keys is **not** needed. Setting `hwcrypto` to 0 gives full software crypto if it misbehaves.

### 5. Association registers (r22b_ra.c `r22b_assoc_regs`)

rtwn's association writes compared with rtw88 (`rtw_vif_port[0]`, `rtw_vif_port_config`, `rtw_conf_tx`, `rtw_phy_rrsr_update`):

| Register | rtwn (`rtwn_run` etc.) | rtw88 | Phase 2 |
|---|---|---|---|
| MSR / net type (0x102 bits 1:0) | `R92C_MSR`, INFRA = 2 | `REG_CR` mask 0x30000, MGD_LINKED = 2 | same, kept |
| BSSID (0x618) | `rtwn_set_bssid` | port 0 bssid 0x618 | same, kept |
| MAC address (0x610) | `rtwn_set_macaddr` | 0x610 | same, kept |
| BCN_CTRL (0x550) | DIS_TSF_UDT cleared, EN_BCN set (0x08) | EN_BCN_FUNCTION (0x08) | same at RUN, kept; before association rtwn has 0x10 where rtw88 already has 0x08 |
| Slot (0x51b), EDCA (0x500..0x50c) | `rtwn_update_slot_cb`, `rtwn_wme_update`: AIFS = AIFSN x slot + SIFS (10/16), ECWmin/max, TXOP | `rtw_conf_tx`: same formula and fields | same, kept |
| Beacon interval (0x554) | written | not written for a station | kept (harmless) |
| AID (0x6a8) | 16-bit write 0xc000 \| AID | bits 10:0 = AID only | **changed:** bits 15:11 put back to their post-init value, AID in 10:0; AID 0 at disconnect |
| 0x668 bit 17 (`R92C_WMAC_TRXPTCL_SHPRE`) | set for short preamble | not written | **changed:** put back to its post-init value |
| RRSR (0x440) | basic-rate bitmap (bits 19:0) from the BSS rates | 32-bit write `rrsr_val_init` (2.4 GHz 0x15f, 5 GHz 0x150) & mask up to the slowest station's current rate | **changed:** rtw88's value at connect, every 2 s and after a scan |
| 0x480 (`R92C_INIRTS_RATE_SEL`) | lowest basic rate | not written | **changed:** put back to its post-init value |

`r22b_post_init` saves 0x6a8 (low 16 bits), 0x480 and bit 17 of 0x668 after initialization. rtwn's `rtwn_scan_end()` rewrites the RRSR and 0x480, so `r22b_scan_end` reapplies those two after a background scan while associated.

### 6. Power tracking and LCK

- **Thermal IQK trigger:** done (item 3).
- **LCK:** not needed on the 8822B (item 3).
- **Thermal Tx power tracking:** left as a TODO. That is `rtw8822b_pwr_track`: BB swing and TXAGC offsets from the swing tables by thermal delta. It needs the type-0 swing tables (about 130 lines of data), `rtw_phy_pwrtrack_*` (averaging, delta, table lookup), `rtw8822b_txagc_swing_offset` and the per-rate power index of the current Tx rate, which is not small. The thermal readings and the RA-reported rate it would use are now available.

### Core rtwn changes in phase 2

- **`rtwn_softc.cam_group_bits`** (if_rtwnvar.h) and its use in `rtwn_key_set_cb0` (if_rtwn_cam.c): extra control-word bits for global keys in FULL mode. Needed to write rtw88's CAM layout (the `group` bit) without a chip hook. It is 0 on all other chips, so nothing changes for them.

Nothing else in the core changed. Media status, RSSI, C2H, IQK and the crypto mode all go through existing hooks:
- `sc_set_media_status`, `sc_set_rssi`
- `sc_classify_intr`, `sc_handle_c2h_report`, `sc_check_frame`
- `sc_iq_calib`, `sc_lc_calib`
- `sc_post_init`, `sc_set_chan`, `sc_hwcrypto`

### Simplified or left out in phase 2

- No per-frame Tx reports (`SPE_RPT`, `C2H_CCX_TX_RPT`); no EAPOL Tx status. net80211's rate control is not used.
- `H2C_CMD_WL_PHY_INFO` (`rtw_fw_update_wl_phy_info`: throughput, Rx rate, EVM) is not sent; rtwn does not collect those statistics.
- No configured per-station rate mask (`rtw_rate_mask_cfg`).
- RA info is resent every 8 s from the current node state, as rtw88 does. There is no immediate update when the peer's capabilities change (rtw88's `rtw_sta_rc_work`), so such a change waits up to 8 s.
- Not needed on the 8822B: rtw88's `rtw_set_ampdu_factor` (the 8822B's chip op is NULL) and the beacon filter configuration (needs `FW_FEATURE_BCN_FILTER`; the feature word of rtw8822b_fw.bin 30.20.0 is 0).
- Reserved pages, LPS, beamforming at association (`rtw_bf_assoc`) and coexistence notifications are left out, as agreed.
- Thermal Tx power tracking: TODO (item 6).
- Multicast data uses the TID queue, not QSEL_HIGH; this only matters for AP/IBSS.

### Uncertainties to check on hardware (phase 2)

1. **RA reports arriving.**
   - After association, `RTWN_DEBUG_RA` should show "macid 0, rate 0x.." lines every few seconds, and `ifconfig` the matching rate.
   - If nothing arrives, check that short C2H packets reach `r22bu_classify_intr` (`RTWN_DEBUG_INTR` prints unhandled C2H IDs).
   - Check that the firmware accepted `RA_INFO` (H2C mailbox, `HMETFR` timeouts print "failed to send h2c command").
2. **Initial descriptor rate with `USE_RATE` clear.** rtw88 puts its "highest" rate there and relies on the firmware. If the first frames after association go out too fast, compare with a Linux capture.
3. **0x6a8 bits 15:11, 0x668 bit 17, 0x480.** These are restored to post-init values on the assumption that rtw88's "not written" means "reset value". Check against a Linux register dump after association.
4. **RRSR** is written as a full 32-bit value, as rtw88 does, which clears bits 23:20. rtwn's 0x440 upper bits (RSC sub-channel, short) are therefore 0.
5. **IQK timing.**
   - If the join-time IQK delays authentication noticeably, it may need to move. The IQK typically takes a few hundred ms (Linux log "iqk counter=").
   - The register reads and delays drop the driver lock, so a channel change from another thread during the IQK is possible in theory. rtw88 holds its mutex for the whole IQK.
6. **FULL crypto.**
   - **GTK rekey:** the new group key goes to the other index, and rtwn's `rtwn_key_alloc` refuses a global slot that is still marked used. Check that a rekey after the AP's group-rekey interval works (or set the AP's interval short to test).
   - **If group traffic breaks**, `hwcrypto=0` is the fallback. Haiku may not pass tunables, in which case change the default in `r22bu_attach`.
7. **SWDEC semantics.** Taken from rtw88's rx.c. If the hardware sets the cipher field without `SWDEC` for frames it could not decrypt (no key), they would be passed as decrypted. rtw88 has the same assumption.
8. **Null frames with hardware sequence numbers** use a counter separate from net80211's data sequence numbers (also true in rtw88).
9. **`avg_pwdb` vs rtw88 RSSI.** rtwn adds a CCK correction (92C-era) and uses the stronger path instead of rtw88's linear average of two paths. The level thresholds (20..50) may shift slightly.

### Build (phase 2)

```
cd /build/haiku-odroid-c2/generated.arm64 && jam -q realtekwifi
...updated 107 target(s)...   (exit 0; the if_rtwnvar.h change rebuilds the whole driver)
```

- No warnings or errors in `rtl8822b/`, `rtl8822b/usb/` or `if_rtwn_cam.c`.
- All other warnings in the full rebuild are pre-existing in files phase 2 did not change: `if_rtwn.c`, `if_rtwn_fw.c`, `if_rtwn_rx.c`, `pci/*`, `usb/rtwn_usb_rx.c`, `rtl8188e/*`, `rtl8192c/*`, `rtl8192e/*`, `rtl8812a/*`.

### Test hints (phase 2)

- **Association with WPA2 (CCMP).** Check that:
  - the 4-way handshake completes;
  - DHCP works (broadcast and unicast);
  - `ifconfig` shows an MCS/VHT rate that changes with distance;
  - `RTWN_DEBUG_KEY` shows the group key at index 1 or 2 and the pairwise key at index 4 or above.
- **Group rekey.** Set a short GTK interval on the AP and check that broadcast traffic (e.g. ARP) keeps working.
- **Debug output.**
  - `RTWN_DEBUG_CALIB` shows "IQK on channel N" once per join, not during scans.
  - `RTWN_DEBUG_RA` shows the RA info at connect, every 8 s, and the rate reports.
  - `RTWN_DEBUG_RSSI` shows the RSSI monitor commands.
- **Comparison with Linux.** Dump 0x440, 0x480, 0x550, 0x668, 0x680 (SECCFG = 0xcf), 0x6a8 and the CAM entries (0x670/0x678 reads) after association, on Linux and Haiku.
