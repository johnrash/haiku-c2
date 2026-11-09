# Firmware

Binary firmware that the port's drivers load. It's packaged separately from
Haiku by `tools/firmware-package.sh` and `tools/wifi-firmware-package.sh`.
Both sets are the files from
[linux-firmware](https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git),
redistributed under their own licences, which are kept next to them.

| File | For | Licence | SHA-256 |
|---|---|---|---|
| `meson/vdec/gxbb_h264.bin` | the S905's video decoder: H.264 | `LICENSE.amlogic_vdec` | `c40b6e5baf36da4b7567553fbfd87b32b2495d49db53e39c164db9ac78ee8258` |
| `meson/vdec/gxl_hevc.bin` | the video decoder: HEVC | `LICENSE.amlogic_vdec` | `f13362950539e9e1594625908540dee54c143432b7900ee198d5ef17a62c58d1` |
| `meson/vdec/gxl_mjpeg.bin` | the video decoder: MJPEG | `LICENSE.amlogic_vdec` | `8a4997cd0c5c5cbe678c75c45ae66a64d1f9c1a0ba2d26d137f24d21f0ab6d53` |
| `meson/vdec/gxl_mpeg12.bin` | the video decoder: MPEG-1/2 | `LICENSE.amlogic_vdec` | `4a851302785a48e84cb846e5e974a4f51a4ca87c52d961aab4307b1c91daf2b2` |
| `realtek/rtw88/rtw8822b_fw.bin` | RTL8812BU/RTL8822B USB Wi-Fi | `LICENCE.rtlwifi_firmware.txt` | `a72da690597bfa99d8eb6fc2ab090d18d8ad92ac2befd35db1c9e3662d8d8418` |

The H.264 hardware **encoder**'s microcode (milestone M48) is not here. It
comes from Amlogic's vendor kernel under other terms. `tools/venc-firmware.sh`
builds a package from a copy you obtain yourself.
