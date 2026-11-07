# UI、动态亮度与可选 NOR 资源

## 页面和 WebUI

2026-11-07 布局源码更新：原生 LVGL 界面为 480×854，内容起点 (16,66)，
内容区域 448×772，4×6 网格，横纵步距 114／130 像素、间隔 8 像素，
左右和底部边距 16 像素。顶边为铺满屏幕宽度的 6 像素半透明分段换页条；
已启用页面等分，当前段采用主题强调色 90% 不透明度，其余采用次要文字色 20%。
横条不占底部空间。时钟、数值、文字、图表、天气和提醒可自由组合。
最多 8 页、32 组件、每页 8 组件；普通组件至少 2×2、图表至少 4×2。
满宽数值支持 4×1。紧凑图表和单行数值需要双方协商能力位 `0x100`；
旧 MCU 保留原有尺寸，网页及 ESP32 会拒绝无法应用的新布局。
默认六页为时钟、空气数据、趋势、天气、提醒和外部内容，共 15 个组件。
深绿底色、大时钟和数值直接呈现内容；默认页面不放操作说明或介绍卡片。

本次同步更新 `/dakin_lvgl/` 独立 Web 预览与 LVGL 源码，未烧录设备。
逻辑行列、跨度和场景协议不变，旧布局自动使用新的像素区域。
ESP32 公共管理编辑器未在本次调整像素几何；下面的编辑、下发能力描述沿用既有实现。

ESP32 的公共管理页按同一比例预览；“＋组合页面”增加页面，“编辑布局”启用
拖动、缩放及方向键调整。撤销保留最近 20 次布局操作。浏览器按设备保存草稿，
设备布局版本未改变时自动恢复；保存到设备另需完成布局提交及 MCU 版本应答。
页面删除通过稳定页面 ID 重映射休眠目标，失去目标时回退为保留当前页。

方形数值在同一排显示较大的标题和较小的单位，下方读数随组件放大。
4×1–3 数值左上显示标题、下方显示单位，右侧为自动适配宽高的大数字。
4×2／4×3 图表左侧一列放标题、当前读数和单位，右侧三列绘制图形。
天气页采用大温度、矢量天气图标、体感／湿度／风速和未来两天预报；小组件
保留温度与天气，较高组件增加次日预报。图标不依赖外部 NOR 图片。

## 绘制和内存

界面按可见内容指纹更新：同一显示数值的采样刷新、未显示页面的数据和天气
获取时间戳不重建对象；分钟、数值、有效期、天气、提醒和布局变化仍会刷新。
休眠时跳过绘制，唤醒后更新。字体按需缩放，单个位图缓冲区为 4608 字节，
不为每个字号复制整套字库；图表只缓存当前页，保留八个兼容槽位。

LVGL 池从 768 KiB 缩为 128 KiB。本次 ARM ELF 的 BSS 从 1,980,968 降为
1,318,064 字节，净减少 662,904 字节（约 647 KiB）。原生测试覆盖数值缩放、
七种紧凑图表、天气、过期和走时；4000 次无变化调用不重建，200 次重绘后
可用内存不下降。主机耗时不能作为 MCU 帧率。

图表支持折线、多曲线、双轴、柱状、堆叠柱、堆叠面积和环图。内置历史覆盖本次
开机的 1440 个分钟点，窗口 1／6／24 小时；外部图表最多三组、每组 32 点，
带名称、单位、缺测掩码和 TTL。缺测断线，过期不显示旧值；堆叠和环图要求同单位、
非负数据。TVOC 组件可选 ppb 浓度或原厂优／良／差，默认保留原厂等级。
外部数值组件可选数字或 API 提供的等级文字；选择随布局保存。其他本地测量
保留数值显示，未添加未经核实的等级阈值。ESP32 继续发布 ppb 浓度。

## 动态亮度

正常亮度沿用原厂 `display.luminance`，网页连续操作合并最新请求，等待同一 token
的应答、新快照和实际 PWM 回读。策略由 ESP32 持久化，MCU 重连后重新下发：

- 外部供电和电池分别设置无操作分钟数，0 表示禁用无操作休眠。
- 运行上限 1–100%，默认 100%；休眠亮度 0–100%，仍受上限约束。
- 休眠可指定任意已启用组合页；255 表示保持当前页，唤醒恢复原页。
- 最多八项每周计划，按列表顺序匹配；支持跨午夜，星期归属开始当天。
- 起始分钟包含、结束分钟不包含；触摸首次只唤醒，并覆盖本次计划时段。
- OTA 暂时使用 10% 亮度，受显示上限约束，不修改保存的亮度；完成、取消或超时后恢复原策略。安装经过原厂引导重启时会短暂黑屏。
  OTA temporarily uses 10% PWM within the configured ceiling; saved brightness and sleep policy resume afterward. Loader activation still briefly restarts the display.
- 收到有效显示设置前，启动亮度持续限制在 10% 以内；
  原厂保留设置只读，不修改电源管脚。

接口为 `screen.display.get/set`；完整参数及旧 MCU 行为见 ESP32 仓库
`BRY88BBA15G/SERVICES.md`。PWM 百分比不是光照计测得的亮度。

## 外部 NOR：资源可缺失，运行不依赖它

2026-11-07：下列 `IOTFNT01` 和单字形缓存说明保留旧版背景。当前版本使用
双区 IOTFNT02 高清字库、16 项灰阶缓存及资源上传，详见 [字库、语言与布局扩展](EXTERNAL-FONT.md)。

外部 NOR 原有用途仍包括原厂资源和 UART OTA 暂存。新增的是**可选 32 像素字库**，
读取地址 `0x900000`、最大 128 KiB。它避开原厂资源、OTA 镜像、复制尾区与标志区。
布局、计划和提醒保存在 ESP32；内置字体保留在 MCU 内部 Flash。

`IOTFNT01` 字库包包含 32 字节头、24 字节有序字形记录和 1bpp 位图，校验总长度、
CRC、Unicode、所有偏移和尺寸。初始化仅进行一次，256 字节分块、总预算 5 秒，
持续服务 WDT/UART。验证成功后缓存到 SDRAM；LVGL 绘制只读 RAM。子集缺字逐字
回退内置字体，加载后 NOR 失联不影响已缓存内容。

缺失、I/O 错误、损坏或超时会锁定本次启动的降级状态，不在绘制、定时任务或错误
分支中反复重试，不因资源故障复位。整颗 NOR 不可读时，引导标志确认失败也允许
进入正常业务；传感器、触摸、RTC 与串口维护继续运行。真实升级仍需 NOR 完整
擦写、回读和激活校验，失败发送取消，不写 A1、不主动重启。

`screen.resources.get` 及 `status.get.screen.resources` 返回
`available/state/external_font/fallback/glyphs/bytes/crc`。状态为
`unchecked/absent/ready/io_error/invalid/timeout`，回报按会话校验并在 15 秒后失效。

生成字库（需要 Pillow）示例：

```sh
python3 tools/pack_font.py --font /path/to/font.ttf \
  --text /path/to/characters.txt --output /path/to/optional-font.bin
```

打包工具只生成文件。当前交付没有新增资源擦写接口，也没有将可选字库写入实机；
当前界面可完整使用内置字库。未来写资源必须先核对目标范围和空白、写后全量回读；
不能用资源包覆盖工厂数据或 OTA 区域。

## English

As of 2026-11-07, [Fonts, language and layout extensions](EXTERNAL-FONT.md)
supersedes the legacy IOTFNT01-only and single-glyph-cache descriptions below.
The current application supports validated uploads into two optional NOR font
banks, with 16 grayscale glyph-cache slots in SDRAM.

The 2026-11-07 LVGL source and standalone `/dakin_lvgl/` preview use a 480×854
canvas with origin (16,66), a 448×772 widget region, 114×130 cell pitch and
8-pixel gaps. Side and bottom margins are 16 px. A full-width 6 px translucent
top rail divides enabled pages equally, highlighting the current page with the
theme accent at 90% opacity; other segments use secondary text at 20%. This
update does not flash the device or change the ESP32 management editor's pixel
geometry. Logical layouts and wire contracts remain compatible.
Up to eight pages, 32 widgets and eight
widgets per page combine clocks, measurements, text, charts, weather and notices.
Responsive peers negotiate bit 8 for 4×1 value cards and 4×2 charts. Square
readouts scale with their cells; wide readouts place a title/unit on the left
and a large fitted value on the right. Compact charts reserve the left column
for labels/readings and the other three for the plot. Weather combines vector
icons, a large temperature, three current conditions and two forecast days.
The editor supports pointer drag/resize, keyboard adjustment, 20 layout undo
steps and device-specific browser drafts. Saving a draft and applying a layout
to the MCU are separate operations. Stable page IDs preserve sleep-page targets
when pages are removed or reordered.

Seven chart modes include stacked bars and stacked areas. Internal history keeps
1440 minute samples per boot; external data supports three named series of up
to 32 points with units, gaps and TTL. Expired samples are not presented as live.
Additive charts require matching units and nonnegative values. TVOC widgets can
show ppb or the original excellent/good/poor grades; automatic mode retains the
factory grade. External value widgets can show a number or API-supplied grade
text. The choice is saved in the layout. Other local readings remain numeric.
The six default pages contain 15 widgets, without instructional cards, using
a dark green palette and large clocks/readings. ESP32 continues to publish ppb.

Unchanged visible content skips LVGL object rebuilding. Freshness, clock minutes,
layout edits and visible payload changes invalidate the cache. Chart storage is
limited to the visible page and font scaling shares one 4608-byte bitmap. The
LVGL pool is 128 KiB, reduced from 768 KiB. This ARM build uses 662,904 fewer BSS
bytes than the previous build. Native tests cover unchanged frames, repeated
redraws without heap growth, all chart modes, weather and expiry transitions.

ESP32 persists normal brightness and the negotiated display policy. The policy
provides separate power/battery idle timers, a runtime ceiling (default 100%),
dimmed sleep pages and eight ordered weekly plans, including overnight periods.
The first touch wakes without paging and overrides the current plan occurrence.
Until a valid display command arrives, startup brightness stays at or below 10%
without changing the retained factory record. OTA temporarily uses 10% brightness
within the configured ceiling, then restores the saved display policy.
PWM duty readback is measured from the MCU register, not inferred from settings.

An optional read-only font bank at **0x900000**, at most 128 KiB, is separate from
factory assets and OTA staging/flags. `IOTFNT01` validates the complete CRC, size,
sorted Unicode records, dimensions and all offsets. Loading happens once with
256-byte reads and a five-second total budget while servicing UART/WDT. Rendering
only accesses the validated RAM cache. Missing glyphs use built-in fonts.

Absent, corrupt, unreadable or timed-out resources latch a fallback for this
boot; redraws and periodic tasks never retry the bank or request a reset. A NOR
boot-flag acknowledgement failure also allows normal sensor/UI business to run.
OTA still requires verified NOR staging; any transfer/storage failure cancels
without activation or an intentional restart. Resource telemetry expires after
15 seconds or a session change.

The packer only creates files. **No resource bank was programmed into the live
NOR and no new resource erase/write API is exposed.** Built-in fonts provide the
complete current interface. Future provisioning must inspect and read back its
own region, preserving factory assets and the complete OTA copy range.
