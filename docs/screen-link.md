# Screen extension transport v1 — development contract

Factory UART remains 115200 8N1. Stock commands and replies keep their original owner. The development bridge is not a replacement for the stock command state machine.

## Negotiation

After the normal factory version exchange the screen sends **one** offer per boot/link reset:

```text
screen_caps 1 <session:8 lowercase hex> <offered capabilities:8 lowercase hex>\r\n
+screen_caps:1 <same session> <accepted capabilities>\r\n
```

Capabilities are a bitmask: bit 0 = bounded MessagePack widgets, bit 2 = SCN2 layouts/external data, bit 3 = weather/reminders/clock, bit 4 = backlight telemetry, bit 5 = display policy, bit 6 = SCN3 layouts/multi-series data/resource telemetry, bit 7 = per-value display mode, bit 8 = responsive 4×1 values and compact 4×2 charts, bit 9 = appearance, bit 10 = PD12 module control, bit 11 = wallpaper, bit 12 = numeric TVOC/mixed-unit local charts. The current offer is **0x1ffd**. Bit 1 is reserved and is **not advertised**. Unknown schema/invalid session/unsupported bits do not enable extensions. The ESP ACK queue must accept the complete line before enabling the session. The screen enables its decoder only after receiving a matching ACK whose accepted bits are a subset of its offer. No ACK means factory fallback, no periodic retry, no extension data. Old 0x1d peers retain SCN2 and factory brightness behavior.

A normal `version?` request clears the ESP bridge's stale negotiation before forwarding that request to the factory owner. Integrators must also call reset on transport restart. A repeated identical offer does not rewind either sequence. A new boot must use a fresh nonzero session nonce from the platform RNG. The factory ESP's response to the added offer still needs stock replay/hardware validation; a bounded unknown-command exchange is not proof of mixed-firmware compatibility.

## Extension framing

```text
screen_ext 1 <session:8 lowercase hex> <sequence:8 lowercase hex> <crc32:8 lowercase hex> <base64>\r\n
```

CRC32/ISO-HDLC covers decoded bytes, not the header. This is corruption detection, not authentication. Both directions keep independent nonzero monotonic sequences; no wrap in a session. Maximum decoded payload: 768 bytes. Base64 padding and unused bits must be canonical. Receiver line storage: 1536 bytes. Overflow discards through the next newline; malformed lines never enable extensions.

Factory OTA `+bin:` data is length delimited and **must bypass the line parser**, including any CR/LF/NUL inside the block. `src/maintenance.c` owns this independent receive state machine ahead of text parsing. It implements staging, streaming MD5 and full NOR readback. Activation requires the reviewed local loader digest, matching application MD5 and erased copy tail; the matching double boot backup can be reused. Erase, write, readback and transfer timeout failures send `cancel_ota` without activation/reset. The ESP allows 90 seconds for the initial erase request and 60 seconds per subsequent packet. See [recovery and watchdog](RECOVERY-WDT.md).

An independent `screen_recovery 3` session is available before SDRAM/UI startup
and during normal operation. It reuses the CRC framing for bounded read-only
boot-flash export and recovery controls, without advertising UI capabilities.
See [recovery and watchdog](RECOVERY-WDT.md) for the complete contract and gates.

Factory callbacks receive text without CRLF; extension callbacks receive borrowed stack bytes. Copy retained content before returning. UART owners are single-threaded; transport reset and callbacks must be serialized. send callbacks enqueue the whole line or fail atomically.

## Widget payload

The decoded payload is this MessagePack array:

```text
[1, slot, type, source, ttl_seconds, title, unit, text, values]
```

| Field | Constraint |
| --- | --- |
| slot | 0 or 1; update exactly one card atomically |
| type | 0 number, 1 text, 2 line, 3 bar, 4 pie |
| source | 0 API, 1 MQTT, 2 authorized LAN; provenance only, not authorization |
| ttl_seconds | 1–86400; monotonic uptime with wrap-safe expiry |
| title / unit / text | nonempty title; at most 48 / 16 / 96 UTF-8 bytes |
| values | 1 for number, 0 for text, 2–16 for line/bar, 2–4 for pie |
| numeric values | float32 or unsigned integer; finite, absolute value <= 1000000 |
| pie values | nonnegative, positive total |

Integers accept positive fixint / uint8 / uint16 / uint32. Strings accept fixstr / str8. UTF-8 rejects embedded NUL, ASCII controls, malformed/overlong sequences, surrogates and out-of-range scalars. Extra trailing bytes are rejected. Schema updates are transactional: failure preserves the previous card and its expiry time. Pie labels use ordered segment numbers. Line/bar values are normalized for LVGL coordinates and show the original numeric range.

The built-in 16 px font includes printable ASCII and GB2312. The 32 px font uses an optional validated RAM bank or doubles the built-in glyph; numeric fonts are 64/72/144 px. Missing external glyphs fall back individually. See [resource failure behavior](UI-AND-RESOURCES.md).

`tools/widget.py` is a dependency-free encoder for development frames. Its JSON input is a local CLI configuration format, not a new network JSON API. API/MQTT/LAN authorization and the public openiot API must be handled by the existing shared ESP services when they are integrated. Direct UART access is trusted in this development contract.

## Scene and service payloads

These binary payloads use the same negotiated framing. Integer fields are little endian, floats are finite IEEE-754 float32, and fixed text fields are NUL-terminated UTF-8. Authoritative field layouts and bounds are in `include/scene.h`, `include/services.h`, `src/scene.c` and `src/services.c`; the paired ESP snapshot is hash checked at build time.

| Opcode | Decoded payload | Behavior |
| --- | --- | --- |
| 0x20 | begin + revision/u32 + size/u32 + CRC/u32 | Starts a 3120-byte SCN2 or 9520-byte SCN3 transaction |
| 0x21 | chunk + offset/u32 + bytes | Contiguous chunks, at most 512 data bytes from the ESP owner |
| 0x22 | commit + revision/u32 | Validates all pages/items before replacing the live scene |
| 0x23 | item ID/u16, TTL, count, text and values | External text and up to 64 values; stale data expire |
| 0x24 | 353 bytes total | Current weather, optional fields and three forecast days |
| 0x25 | 204 bytes total | One notice, unread state and wake policy |
| 0x26 | 7 bytes total | UTC epoch/u32 and timezone minutes/i16 |
| 0x27 | 6-byte v1 or 13-byte v2 | MCU backlight report, only with capability bit 4 |
| 0x28 | 368 bytes total | Atomic display policy; requires bit 5 |
| 0x29 | 12-byte header + up to 3 series | Up to 32 points/series, validity masks and TTL; requires bit 6 |
| 0x2a | 16 bytes total | Optional font bank state/count/bytes/CRC; requires bit 6 |
| 0x2b | 12 bytes total | PD12 query, high or bounded low lease; requires bit 10 |
| 0x2c | 20 bytes total | PD12 pin readback, remaining lease, UART2 frame count; requires bit 10 |
| 0x2f | 7 bytes total | ACK: request opcode, result, applied revision/u32 |
| 0x30 | opcode + 704-byte APP1 | Theme, widget colors, background opacity and wallpaper selection; bit 9 |
| 0x31 | 12-byte header + up to 512 bytes | Sequential wallpaper transaction; bit 11 |
| 0x32 | 20 bytes total | Wallpaper request result, state, CRC and generation; bit 11 |

See [appearance and wallpaper](APPEARANCE.md) for binary offsets, bank ownership,
upload timeouts, failure fallback, and mixed-unit chart behavior.

SCN2 is a 16-byte header, eight 36-byte page records and thirty-two 88-byte item records. The grid is 4×6, at most eight items per page; duplicate IDs, overlaps, invalid bounds and invalid UTF-8 reject the complete transaction. The eight item types are clock, number, text, line, bar, pie, weather and reminders. The ESP reports saved revision separately from MCU-applied revision. A stock MCU remains on factory commands and cannot acknowledge a scene.

SCN3 keeps that header and page stride, adds stable 24-bit page IDs in the page
record, and expands each item to 288 bytes: chart mode, 1/6/24-hour history,
three bindings and 192-byte static text. Items need at least 2×2 cells, except
full-width values can be 4×1 and charts need 4×2. Charts below four rows and
one-row values require bit 8; the ESP rejects saving or sending them to older
peers. The binary codec is authoritative.
Local sensor charts record one sample per minute and stretch the available
history across the plot until the selected 1/6/24-hour maximum is available.
The displayed range follows that window. Longer histories are averaged into
at most 32 display points; one sample draws a point and missing intervals remain
gaps. External series require fresh API data and never use synthetic measurements.
本地传感器每分钟记录一个采样点；历史不足所选的 1/6/24 小时上限时，
按已有历史横向铺满，并显示实际窗口时长。长历史平均聚合成最多 32 个绘图点，
单点立即可见，缺测保持断点；外部曲线需要有效的 API 数据。
An existing SCN2 layout is transmitted as SCN2 even to a new peer; upgrading
geometry is explicit. The new default layout is SCN3.

SCN3 item byte 82 is `value_mode`: 0 automatic, 1 numeric, 2 grade. Nonzero
modes require bit 7 and a value widget. TVOC numeric mode shows the concentration
in ppb; grade mode preserves the original hysteresis and 优/良/差 labels.
External grade mode uses the fresh API-provided text. Other local measurements
remain numeric; no unverified grading thresholds are introduced. SCN2 decodes
to automatic mode. Peers without bit 7 reject a new nonzero mode at the ESP
boundary instead of silently displaying a different presentation.

Display policy is revisioned, durably owned by ESP, and acknowledged separately
with opcode 0x2f/request 0x28. It specifies power/battery idle minutes, PWM ceiling,
idle brightness/page and up to eight ordered weekly plans. Monday is bit 0;
overnight plans use the starting weekday. Intervals include start and exclude
end; first matching plan wins. Page 255 preserves the current page. A short touch
wakes without paging and overrides the current plan occurrence. OTA has priority.

## Backlight readback / 背光回读

V1 sends `{0x27,1,requested,ceiling,applied,asleep}`. V2 sends
`{0x27,2,requested,ceiling,applied,asleep,target,reason,page,revision:LE32}`.
Reports arrive once per second during negotiated business. All percentages are
0–100 PWM duty. `applied` is read from PWM3A; v2 sleeping may retain a dimmed
page. Reason is 0 awake, 1 idle or 2 weekly plan. OTA temporarily reports target 10, awake and reason 0 while preserving the saved policy; the configured ceiling still applies. Invalid versions/lengths/
bounds are rejected. Reports expire after 15 seconds or a session/recovery change.

Normal brightness writes still use factory `display.luminance`. The runtime
policy adds a default 100% ceiling, dimming and page selection without controlling
power rails. Before policy negotiation the fallback ceiling is 100%. Startup
output is at most 10%; touch restores the capped normal setting.

协商能力扩展为 0x1ffd；旧对端仍使用 SCN2 与原厂设置。SCN3、休眠策略、多曲线和
可选资源状态均有独立能力门控。亮度仍用原厂设置写入，v2 回报另外给出当前目标、
休眠原因、页面和策略版本，支持休眠页保留低亮度。默认运行上限 100%，启动最多
10%；OTA 暂时使用 10%，不改保存值，更新结束后恢复原策略。超时或会话变化时实际输出标为未知。字库故障只降级显示，OTA 的 NOR
校验失败则取消本次升级；两者均不触发自动复位重试。

SCN3 组件第 82 字节保存显示模式，0 为默认、1 为数值、2 为等级，非零模式须协商
bit 7。TVOC 可选 ppb 浓度或原厂优／良／差；外部等级取 API 提供的有效文字。
其他本地测量保留数值显示，旧 SCN2 默认行为不变。
