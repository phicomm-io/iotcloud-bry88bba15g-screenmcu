# Screen appearance / 屏幕外观

WebUI 的「屏幕 → 外观」提供森林、墨色、海洋、琥珀、梅紫、纸白六套主题。
切换主题会清除全局自定义颜色；组件自己的覆盖色保留，可逐项或一次恢复跟随主题。
组件的背景、标题、数值、单位、正文分别配色。透明度只作用于背景与边缘，文字保持不透明。
布局仍使用 SCN3，配色单独保存，不增加页面或替换用户布局。
顶部换页条可选 6、12、20 px，默认 12 px；较粗时标题自动下移，组件网格不变。

预设壁纸为山峦、极光、沙丘。上传接受 PNG/JPEG/WebP（最大 5 MiB、2000 万像素）
或 `.bwp`。浏览器居中裁剪到 240×427，输出 RGB565；MCU 放大到实际 480×854。
压暗和组件透明度另存设置，不写进图片。`phicomm-community-firmware/tools/screen_wallpaper/` 中的 Pages 转换工具
使用与 WebUI 相同的转换代码，图片只在浏览器处理。

固件保留启动时 30 秒最小串口 OTA 服务，应用起点仍为 `0x14000`。
来电自动启动若依赖原厂引导区，按本次要求不实施；不改引导区。

## Contract

Capability bit 9 (`512`) enables APP1, bit 11 (`2048`) enables wallpaper
transactions, and bit 12 (`4096`) enables numeric TVOC and mixed-unit local
charts. Bit 17 (`131072`) enables selectable page rails; bit 18 (`262144`)
enables resource I/O telemetry. The complete current offer is `0x7fffd`
(524285). No acknowledgement
keeps factory UART behavior. Earlier community peers keep their negotiated
subset. Appearance is persistent on the ESP32 and replayed after MCU sessions
change, with at most three transmissions per saved revision/session.

`0x30` carries one 704-byte APP1 document and receives a seven-byte `0x2f`
acknowledgement with request opcode, result, and applied revision. APP1 has
magic at 0, revision/LE32 at 4, global color mask/LE16 at 8, theme at 10,
wallpaper at 11, card opacity at 12, dimming at 13, override count at 14,
twelve RGB24 roles at 16, image payload CRC/LE32 at 52 and thirty-two 20-byte
widget overrides at 64. Byte 56 selects the language. Byte 57 selects the
page rail height: 0 means the default 12 px, or use 6, 12, 20. Nonzero values
require capability bit 17; older documents with zero retain their byte encoding.
Bytes 58–63 are zero. A widget has ID/LE16, five-role mask, opacity
(`255` inherits), then five RGB24 colors. Reserved fields are zero.

`0x31` uses a twelve-byte header: opcode, version 1, operation, reserved zero,
request/LE32 and offset/LE32. Operations are query 0, begin 1 plus a 32-byte
image header, chunk 2 plus at most 512 bytes, commit 3, cancel 4. `0x32`
replies with twenty bytes: opcode, version, result, resource state, request,
received bytes, payload CRC and generation (the last four fields LE32).
Resource state 7 means preparing a bank; query until state 6 before sending chunks. Commands run on the existing UART owner; ESP32 never buffers a full image.

`.bwp` is 204,992 bytes: magic `IOTW5651`, generation/LE32 at 8 (assigned
by MCU), width 240/LE16 at 12, height 427/LE16 at 14, payload CRC32 at 16,
payload length 204,960/LE32 at 20, zero at 24, header CRC32 over bytes 0–27
at 28, then little-endian RGB565. Both CRCs use CRC32/ISO-HDLC.

## Resource failure behavior / 资源故障

The two 256 KiB NOR banks are `[0xa00000,0xa40000)` and
`[0xa40000,0xa80000)`. Ownership is claimed only after the whole bank is
confirmed erased. Each bank reserves an ownership record at offset 0,
committed header at 4096, and pixels at 8192. Factory/font/OTA ranges are
outside these bounds. A new image uses the other bank, verifies each write,
checks the full payload CRC, and writes its header last. Interrupted writes
retain the previous complete image.

启动只查找有效目录；选择图片后最多加载一次。坏数据、断线、超时均退回纯色。
图片完整缓存占 SDRAM 819,840 字节，绘制回调不访问 NOR。自定义图片加载最多
60 秒，每轮只读取一行；预设每轮只生成四行。空白区每轮仅检查 512 字节（总时限 60 秒）。上传连续 15 秒无操作则终止，恢复模块运行。加载期间可继续处理串口查询，
不会重入加载或启动第二个图片事务。OTA 与壁纸上传互斥，期间 PD12 保持低电平。

Local chart mode `SC_CH_MULTI` supports three independent unit ranges. Each
curve and legend keep the same color; three-unit legends include each range.
TVOC curves use ppb. Unsupported formaldehyde input and a third unit in
two-axis mode are rejected when saving. History remains one sample per minute,
stretched over the available time until the selected window fills.

## Usage

Select a theme under **Screen → Appearance**, optionally override individual
colors in the component inspector, and apply appearance. Preset images need
no external NOR. Upload a PNG/JPEG/WebP or a converted `.bwp` for a custom
wallpaper. Set dimming and card opacity independently; text remains opaque.
The separate APP1 save leaves existing SCN3 pages unchanged.
Page rails offer 6, 12 and 20 px heights, defaulting to 12 px. The page title
moves below a thicker rail without changing widget coordinates.

The application retains its 30-second minimal UART OTA startup window and
starts at `0x14000`. Cold power autostart that requires a factory bootloader
change is intentionally outside this implementation.
