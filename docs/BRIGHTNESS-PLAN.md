# 屏幕亮度调节 / Screen brightness

后续状态：本文保留早期 10% 诊断版记录。换电源后的 20%／40% 独立观察通过；
动态策略、默认 40% 运行上限和启动 10% 限制见 [UI 与资源](UI-AND-RESOURCES.md)。
Earlier 10% diagnostic results are retained below. Later independent 20%/40%
observations passed with the changed supply; current dynamic policy and startup
limits are documented in [UI and resources](UI-AND-RESOURCES.md).

2026-11-06 已完成公共 WebUI、ESP32 持久化和屏幕 MCU 亮度回读，并安装到
BRY88BBA15G。网页入口为“设置 → 设备 → 屏幕亮度”。当前镜像保留 10% PWM 上限。

## 已实现

- 在公共 WebUI 的屏幕设置中提供 0–100% 亮度调节、保存、设备应答及回读状态。
- 复用 `screen.settings.get/set` 与原厂 `display.luminance`，提交时保留
  `data_type`、`power_screen_time`、`battery_screen_time` 三个现有字段。
- 沿用 ESP32 设置持久化及 MCU 重连恢复，不因调亮屏幕而改动熄屏时间。
- 显示保存的亮度和实际生效上限；诊断上限生效时明确提示，不能将保存的 60%
  显示为实际已达到 60%。当前硬件稳定性验证完成前保留 10% 上限。
- 熄屏时实际输出为零；触摸唤醒恢复受上限约束的亮度，首触只唤醒。
- 经能力位 0x10 协商后，MCU 每秒报告设定值、编译上限、PWM3A 实际占空比及
  熄屏状态；超过 15 秒、会话改变或进入维护模式后，ESP 将实际输出标为未知。
- 保留 30 秒 SRAM 恢复窗口、串口 OTA、80 KiB 引导区导出及安装校验门控。

## 验证与剩余限制

MCU 原生测试 13 项、ESP 桥接测试 10 项全部通过；公共前端测试和中英文桌面／手机
Chromium 测试通过，覆盖每次点击只提交一条设置、应答、超时、草稿及窄屏显示。
实机已确认 0% 和 5% 设置与回读、ESP 重启恢复，以及高于上限的 60% 设定值保存。
用户确认 5% 时画面变暗、触摸能翻页。安装后检查过恢复窗口暂停、引导区读取和恢复
业务；安装前两次 80 KiB 引导区读取的 SHA-256 一致。

接入 AM1008L 的连续亮度修改测试仍出现 ESP32 欠压复位（reset reason 9），期间
屏幕 MCU 会话未变化，也未报告异常或 SDRAM 错误。保持 10% 实际输出上限，
供电与带载稳定性仍需继续排查；提高上限前需逐级实机验证。

## English

Implemented and installed on BRY88BBA15G on 2026-11-06. The shared WebUI offers
a 0–100% control under Settings → Device → Screen brightness, with matching
acknowledgement and readback. It reuses `screen.settings.get/set` and stock
`display.luminance`, preserves the other three display fields, and restores
saved settings after ESP restart or MCU reconnection.

The editor separates requested brightness, the effective ceiling and the actual
PWM3A duty. Capability 0x10 enables one-second MCU reports, which expire after
15 seconds or a session/recovery change. Sleep outputs zero; the first touch
wakes only and restores brightness within the ceiling. The 30-second SRAM
recovery window, UART OTA and 80 KiB boot export/install gates remain available.

All 13 MCU and 10 ESP native tests pass, together with the shared frontend and
English/Chinese Chromium desktop/mobile scenarios. Hardware checks confirmed
0% and 5% save/readback, restart persistence and a saved 60% request above the
ceiling. The user confirmed a dimmer picture at 5% and normal touch paging.
Recovery hold/read/resume was checked after installation; both pre-install
80 KiB boot reads had identical SHA-256 hashes.

Repeated brightness saves with AM1008L connected still encountered ESP32
brownout resets (reason 9). The MCU retained its session without fault or SDRAM
errors. Keep the 10% output ceiling while investigating power stability, and
validate higher levels on the actual board before enabling them.
