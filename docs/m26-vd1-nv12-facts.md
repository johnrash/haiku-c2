# VD1 with NV12 buffers: facts for an NV12 overlay

> **Status:** Built in M26 stage 4; since M31 the video layer shows the decoder's own frames.

Facts only, from Linux mainline's Amlogic display driver (GPL), for `meson_hdmi`'s video layer (M26 stage 4). No code was copied. `meson_hdmi` already shows packed 4:2:2 (B_YCbCr422) on VD1 (M9); this note has what differs for NV12.

**Source:** torvalds/linux master, `drivers/gpu/drm/meson/meson_overlay.c` (last change 7075ff51eb51, 2026-08-14) and `meson_crtc.c`, fetched raw on 2026-10-30.

## 1. The read interface (VD1_IF0_*)

| Register | Packed 4:2:2 (what meson_hdmi does now) | NV12 |
|---|---|---|
| VD1_IF0_GEN_REG (0x1a50) | bytes per pixel field [15:14] = 1 | bit 1 **separate planes** set; [15:14] = 0 |
| VD1_IF0_GEN_REG2 (0x1a6d) | 0 | colour map [1:0] = **1** for NV12 (Cb first), 2 for NV21 (Cr first) |
| VD1_IF0_CANVAS0/1 (0x1a51/0x1a52) | the one canvas in [7:0], [15:8] and [23:16] | [7:0] the **luma** canvas; [15:8] and [23:16] the **chroma** (CbCr) canvas |
| VD1_IF0_LUMA_X0/Y0 | start [14:0] / [12:0], end [30:16] / [28:16] | the same |
| VD1_IF0_CHROMA_X0 | start/2, end/2 | the same: start/2, end/2 (chroma columns, one per CbCr pair) |
| VD1_IF0_CHROMA_Y0 | start/2, end/2 | the same: start/2, end/2 (**chroma lines**) |

The other bits of VD1_IF0_GEN_REG (enable, hold lines, urgent, repeat last chroma line) are set as for the other formats.

## 2. The formatter (VIU_VD1_FMT_CTRL, 0x1a68)

| Field | Packed 4:2:2 | NV12 |
|---|---|---|
| horizontal Y/C ratio [22:21] | 1 (two luma per chroma) | 1 |
| horizontal format enable, bit 20 | 1 | 1 |
| repeat line 0, bit 16 | 1 | 1 |
| vertical initial phase [11:8] | 12 | 12 |
| vertical phase step [7:1] | **16** (one chroma line per luma line) | **8** (one chroma line per two luma lines) |
| vertical format enable, bit 0 | 1 | 1 |

VIU_VD1_FMT_W (0x1a69) is the same for both: luma width in [27:16], chroma width (end/2 − start/2 + 1) in [11:0].

## 3. Canvases

- One canvas per plane: plane 0 (Y) and plane 1 (CbCr), each with its own address, its own row pitch, and its own height. For NV12 the CbCr plane's height is half the picture's (rounded up), its pitch the luma pitch.
- Linux writes both canvases linear, without wrapping, with the same 64-bit byte swap it uses for 4:2:2 (MESON_CANVAS_ENDIAN_SWAP64), right before it writes VD1_IF0_GEN_REG, GEN_REG2 and the canvas registers.

## 4. Not covered here

The scaler (VPP_HSC_*/VPP_VSC_*), the postblend window and the OSD alpha keying are the same for every format; meson_hdmi's M9 setup stays.
