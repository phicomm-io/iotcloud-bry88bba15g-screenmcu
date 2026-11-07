# Implementation status

> Historical foundation record. Current implementation and acceptance supersede this snapshot: [IMPLEMENTATION-20261105.md](IMPLEMENTATION-20261105.md).

ESP baseline: `rc4-policy/main` at `a41f4d5d2d8a9c2592b2ca64b02e0bfc212c3939`, rechecked on 2026-11-05. Worktree: `../iotcloud-bry88bba15g-firmware`, branch `feat/bry88bba15g-screen`.
Screen source: independent local Git repository `iotcloud-bry88bba15g-screenmcu`. No remote publication, commit or hardware flashing was performed.

Network requirement, updated 2026-11-05: all community network services use **iotcloud/openiot**. Factory UART JSON/topics are a compatibility boundary, translated by the community ESP into shared native services. Do not port Daikin login/token, broker or OTA-query endpoints. A stock ESP image retains its own networking behavior; local UART compatibility with it does not mean that image supports iotcloud authentication.

## Recovered board and business contracts, 2026-11-05

The sibling analysis run `private/daikin-bry88bba15g-screen-analysis-20261104/IMPLEMENTATION-GAPS-ANALYSIS.md` records reproducible extraction from the factory application:

- 207 panel command/data words (38 commands) using GPIO 9-bit serial: CS=PA6, data=PC6, clock=PD10, reset candidate=PA7. Original reset/inter-command delays still need a timed port.
- LCD RGB565 480×854; H front/back/sync=40/40/10; V=20/20/10; divider parameter=6. Twenty LCD and 39 SDRAM pin mux calls are recovered.
- SDRAM early initialization precedes C runtime memory initialization. Factory six-byte SDRAM ABI differs from the current seven-byte SDK structure: use named fields and RefreshTime=64, not a raw structure copy.
- PM10 is PWM3A backlight (divider=8, period=10000), not an unresolved charging-control output. SFC uses PD3..PD8 with recovered individual mux settings.
- Factory `service/set` processes only one top-level settings group per request, in priority order: push, warning, DND, display. Display requires all four fields. Field layout and original validation weaknesses are documented; community public API must validate before applying.
- NOR commit byte A1 requests MCU installation and A2 requests gallery installation. The staging windows overlap; these are not independent application A/B slots. Application startup clears the flag, but the installation code is in the still-missing first 80 KiB bootloader.

These are recovered contracts, not a connected BSP or implemented business handlers.

## Reviewable implementation

- The actual LVGL 8.3.11 C UI renders 480×854 RGB565, exported as PNG. Five pages: clock, six readings, CO₂ history, two external cards, settings layout. Chinese/ASCII font subsets and a large clock font have recorded source/generator hashes and licenses.
- Single/double click navigation and wake-only first click have native model tests. Browser preview supports click/double click, keyboard, empty/demo modes and all five component types. PD11 is the user-confirmed touch key, PD13 the rear pinhole button; GPIO sampling is not connected to LVGL input yet.
- Samples expire after 20 seconds. The RAM-only history keeps 1440 minute points from this boot, records the first sample in each minute, inserts invalid points for missed minutes and retains cadence across uptime wrap. It is not a minute average.
- Portable CO₂-style UART frame/checksum and SHT3x CRC/raw conversion, plus the recovered voltage-to-battery curve. Thermal compensation, calibrated HCHO and UART2 A/B mapping remain unresolved.
- A screen runtime receives byte streams, accepts matching capability ACKs, rejects replay/wrong session and applies bounded MessagePack widgets transactionally. Only the implemented widget capability is advertised.
- The ESP bridge offers factory-line callbacks, negotiated widget TX, send-failure sequence preservation and restart fallback. Both real endpoints are linked in a cross-repository test; the shared protocol snapshot is hash-checked at configuration time.
- The OTA staging module checks size/order, writes through a storage HAL, computes incremental MD5, validates vectors, re-reads NOR and verifies the stored digest. Tests cover storage failure, truncated images, out-of-order chunks, split vector words, readback corruption and invalid reset vectors. It has no boot-flag commit or installed-image activation.

## Remaining work — delivery is incomplete

1. Port the recovered panel command table, LCD timing and SDRAM early initialization with the correct SDK ABI; resolve final clock configuration and timed panel delays; identify touch signals/controller. Port official SDK startup, interrupts, linker sections and RTOS. The linker file currently only describes the application address contract.
2. Stock SHT compensation and CO2/TVOC calibration cores now have CPU-differential evidence (see SENSORS-CALIBRATION.md); connect real acquisition/configuration/UART owners and recover remaining status enums, GPIO power meanings and parameter serialization. Implement the 13 MCU handlers and 37 ESP command owners with stock fixtures; ordinary lines are currently callbacks, not completed business logic.
3. Build the real ESP32 runtime using the existing shared HTTPD, MQTT owner/session/queue and authorized LAN modules, backed by iotcloud. Translate factory business messages at the UART boundary; do not forward factory server addresses or arbitrary HTTP URLs. Add public frontend/model capabilities and pinned frontend provenance. No separate approximate network service is introduced.
4. Integrate the length-delimited stock OTA UART state machine, metadata and timeout/error handling. Recover and verify the missing original bootloader handoff before implementing boot flag commit. MD5 matches the factory transport convention and does not authenticate an image.
5. Implement optional read-only Flash diagnostics with an explicit whitelist, plus externally stored fonts/profile resources. No Flash-info capability is advertised yet.
6. Complete settings actions, persisted profiles, alarms/DND, sleep/backlight and power behavior. Current settings page is a layout, not working controls.
7. After the device boots again, verify all four factory/community combinations, stock ESP rollback with community screen, sensor numeric agreement, wake/sleep, power failure, OTA errors and post-update boot. The original device remains unable to boot, so none of these hardware gates has passed.

## Hardware boundaries

Internal application base: `0x14000`, maximum `0x67000` bytes. Factory NOR staging: `0x780000`; boot flags: `0x7f0000`; settings: `0x7e000`. The first 80 KiB bootloader is absent from the obtained OTA image. The staging HAL is not connected to real hardware. Do not turn the staging result into an install operation until the bootloader contract is recovered.

Keep PB12/SWCLK and PB14/SWDIO available. P50 is the user-confirmed nRESET test point. Allocate the runtime/history, LVGL heap and framebuffers in SDRAM; internal SRAM is only 64 KiB. Cross-link size, actual SDRAM startup and firmware packaging are still pending.

## Local verification, 2026-11-05

- Screen Release CMake build and three native suites: core, widgets, update.
- Same three suites with AddressSanitizer and UndefinedBehaviorSanitizer.
- ESP bridge suite and cross-repository endpoint integration suite.
- Two HTTP tests: all offline/demo pages, five component types, PNG CRC/dimensions and bad-route/parameter handling.
- Headless Chromium: five-page navigation, single/double click, keyboard, all component selections, source state changes and no page errors.
- Screenshots: `artifacts/`; implementation and asset checksums: `SHA256SUMS`.

No flashable ESP32/SWM34S artifacts, running-device acceptance, CI run, cloud deployment or public release is claimed. The implementation worktree remains active because the missing runtime/BSP/compatibility work still depends on it.

## Sensor/calibration follow-up

See [implementation and CPU evidence](SENSORS-CALIBRATION.md). The original remote calibration request DOES start the executor; the earlier bit2 interpretation was wrong and is superseded by 30 parsed-handler CPU cases. Typed runtime entry points are implemented; stock JSON decoding/serialization and hardware owners remain to be connected. PD11 user touch key and PD13 rear button are user-confirmed.
