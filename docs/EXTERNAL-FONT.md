# Fonts, language and layout extensions / 字库、语言与布局扩展

## Settings / 设置

The device WebUI has a separate Screen entry, with Layout, Appearance, Display,
Weather & reminders, Resources and Advanced tabs. General keeps the device name
and management password. Switching tabs preserves unsaved layout edits.

设备网页增加独立“屏幕”入口，分为布局、外观、显示、天气与提醒、资源、高级。
通用设置保留设备名称和管理密码；切换标签不会清除布局草稿。

The component Hide title checkbox reclaims heading space in values, text,
weather, reminders and charts. Local charts can retain 32, 64, 128 or 256
points across their configured time span. History still records once per minute;
short history stretches across the plot and missing samples remain gaps.
External API series retain the existing 32-point wire limit.
Local history is held in RAM and starts again after an MCU restart.

组件可隐藏标题，腾出的空间用于内容。设备本地曲线可选择 32、64、128、256 点，
仍每分钟记录一次；历史不足时按实际时长拉满，缺失值不连线。
外部 API 序列仍保留原协议每条 32 点上限。
本地历史保存在 RAM 中，MCU 重启后重新积累。

Display contains a persistent Chinese/English screen-language selector. It is
independent of the browser language. Standard UI labels, dates, grades and known
weather conditions are translated. User titles, locations and API text remain
as supplied. Supported weather providers are requested in the chosen language.

显示设置可保存中文/英文屏幕语言，独立于网页语言。内置标题、日期、等级和常见
天气状态自动翻译；自定义标题、地点和 API 正文按原文显示。
天气提供商支持的语言参数随选择更新。

## Resource format / 资源格式

`resources/zh32.iotfnt` contains 6863 native 32 px glyphs with 2 bpp grayscale.
Generate it with `python3 tools/generate_fonts.py`; generator version, source-font
hashes and output hashes are pinned in `dependencies.lock.json`.
Internal Flash retains numbers, units, common UI/weather text and maintenance
glyphs. Missing extended characters use a visible placeholder.

高清字库含 6863 个原生 32 像素、2 位灰阶字形。生成工具和字体校验值固定在依赖锁中。
内部 Flash 保留数字、单位、常用界面/天气文字及维护字体；扩展缺字显示占位符。

IOTFNT02 uses a 32-byte header: magic[8], total size u32, payload CRC32 u32,
glyph count u16, record size u16=24, line height u8=36, baseline u8=2,
bpp u8=2, pixels u8=32, reserved u32=0 and header CRC32 u32. Integers are little
endian. Each sorted 24-byte glyph record stores code point u32, bitmap offset u32,
bitmap length u16, advance u16, width u16, height u16, signed x/y offsets i16,
reserved u32=0. Bitmaps pack four pixels per byte, most significant pixel first.
Validation checks CRCs, Unicode, metrics, exact contiguous offsets and size.

All fonts are validated into an SDRAM cache before drawing. Lookup and scaling
never access NOR. A 16-slot, 144 KiB SDRAM glyph cache preserves grayscale.

字库完整加载并验证到 SDRAM 后才启用；绘制和缩放不访问 NOR。字形缓存固定为
16 项、144 KiB，保留灰阶。读取失败、超时或校验错误会锁定为内置字体回退，
正常绘制不会无限重试；通信、传感器和串口 OTA 继续工作。

## Upload and recovery / 上传与恢复

The Resources tab uploads `.iotfnt` in acknowledged contiguous chunks of at most
512 bytes. `screen.font.get/begin/chunk/commit/cancel` share the existing command
owner and UART session. Capability `FONT_BANK=16384` is required. Wallpaper,
font and MCU-update writers are mutually exclusive. AM1008L is inhibited during
resource writes and released afterwards.

资源标签上传 `.iotfnt`，每块最多 512 字节，逐块回读并应答。字库、壁纸与 MCU
升级互斥；写资源期间暂时关闭 AM1008L，结束后恢复。

Managed banks are `0xb00000..0xcfffff` and `0xd00000..0xefffff` in the 16 MiB NOR.
They do not overlap factory assets, OTA areas or wallpaper. Bank ownership is
claimed only after a bounded scan proves an unowned bank entirely erased;
unknown nonblank data is never erased. An existing owned bank can be replaced.
A full scan or individual bank load has a five-minute budget for the board's
conservative GPIO SPI. Work runs in 512-byte foreground steps while UART,
sensors and touch continue. The 128 KiB legacy font retains its five-second budget.

两个字库区位于外部 NOR 的 11～13 MiB、13～15 MiB，不占用原厂资源、OTA 或壁纸区。
首次使用会限时扫描空白区，遇到未知非空数据则拒绝写入。完整扫描或单区加载
各有五分钟预算，按 512 字节分步执行；等待期间继续处理通信、传感器和按键。
旧版 128 KiB 字库仍采用五秒预算。

Each bank has a checked owner tag in sector 0, a committed descriptor in sector
1 and file bytes from offset 8192. The target descriptor is erased first; its
new generation, length, file CRC and descriptor CRC are written last. The
validated previous bank remains untouched until commit. Startup tries the newest
descriptor, then the other complete bank once, then built-in fonts. Recovery OTA
does not need SDRAM or either font bank. The legacy `0x900000` font remains
read-only. No bootloader writes are added.

新字库的提交信息最后写入，掉电时保留上一个完整字库。最新字库损坏可回退到旧区，
两个字库都不可用时使用内置字体。最小恢复服务不依赖字库和 SDRAM，不改引导区。

## Compatibility / 兼容性

SCN3 keeps its 9520-byte size: item byte 83 is hide-title, bytes 84–85 are the
point count (0 keeps the original 32). APP1 stays 704 bytes: byte 56 is language
(0 Chinese, 1 English). Required capabilities are `HIDE_TITLE=8192`,
`LANGUAGE=32768` and `CHART_POINTS=65536`. Unsupported extensions are rejected;
zeroed older configurations and the factory UART protocol remain valid.

SCN3 和 APP1 长度不变，扩展使用预留字节并经能力协商。旧配置保持原行为；
未声明相应能力时拒绝发送扩展字段，保留原厂串口协议。
