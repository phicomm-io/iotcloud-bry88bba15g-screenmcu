# Display diagnostics and remote paging / 显示诊断与远程换页

网页入口为“设置 → 设备 → 屏幕 → 显示 → 远程换页”，提供上一页、下一页和指定
页面。命令经 ESP32 转发，MCU 确认实际页码；跳过禁用页，换页时唤醒屏幕，不改变
布局、休眠策略或 Flash 配置。原厂 MCU 未声明能力时控件禁用。

“诊断”标签可采集当前页、指定页或下一次换页。选择下一次换页后可短触正面按键，
也可通过远程换页触发；采集后点击读取记录，支持下载完整记录。

The Display tab provides previous, next and go-to controls. Commands wake the
screen and use MCU acknowledgements rather than inferred page state. The
Diagnostics tab captures current/selected pages or the next page transition,
reads bounded trace batches and exports the collected report locally.

## Measurement / 测量

- TIMR4 uses the system clock divided to 1 MHz. Its 24-bit down-counter wraps
  every 16.777216 seconds; reads extend wraps in internal SRAM, sampled by the
  frame interrupt. Timer initialization enables no timer interrupt.
- LCD frame-end IRQ reports scan count and measured average/minimum/maximum
  period. This is the controller's scan cadence, not an oscilloscope measurement
  of the physical VSYNC pin. PB2 is the reconstructed VSYNC route and PB5 DCLK;
  check the actual board before probing.
- The `flush_cb` records its rectangle, starting microseconds, completion
  duration, pixel count and last-flush marker. Two 40-row draw buffers overlap
  CPU drawing and DMA2D copies; completion releases each borrowed buffer once.
  No per-block UART logging is added. See [media rendering](MEDIA-PERFORMANCE.md).
- `monitor_cb` records LVGL refresh milliseconds and pixels after each round.
  SysTick advances LVGL during drawing; incrementing its clock only from the
  foreground loop would make an uninterrupted draw appear to take zero time.
- The MCU stores at most 96 records in SDRAM. Totals continue after the trace
  fills and `dropped` reports omitted records. ESP32 caches only one reply page
  of up to eight records. Capture closes after 120 ms with no further recorded
  activity; that trailing quiet time is excluded from elapsed time.
- Fault recovery stops LVGL's interrupt tick before accessing recovery state.
  Frame IRQ counters remain in internal SRAM and touch no LVGL data or heap.

刷新为同步拷贝时，flush 耗时包含实际拷贝。两次 flush 之间的间隔包含绘制和调度，
不能直接当作 VSYNC 等待。多轮 monitor 需合计，并检查 `dropped` 后再分析区域。
媒体版本使用双条带缓冲和 DMA2D；下方早期测量保留当时的同步 CPU 拷贝条件。
扫描时钟及 LCD 连续扫描策略保持原配置。

## Negotiated wire extension / 协商协议

Existing factory frames remain unchanged. The capabilities are `0x80000`
(diagnostics) and `0x100000` (page control). The shared binary payloads remain
inside the existing session/sequence/CRC ASCII UART envelope.

| Payload | Purpose |
| --- | --- |
| `0x35`, 16 bytes | Capture/read/stop request, version 1, request ID, offset/limit, capture mode/page |
| `0x36`, 128 + 24×N bytes | Summary and up to 8 trace records; at most 320 bytes |
| `0x37`, 12 bytes | Query/go-to/next/previous, version 1, request ID and one-based page |
| `0x38`, 16 bytes | Result, asleep flag, request ID (zero for unsolicited telemetry), actual page, enabled mask, count and applied layout revision |

Request and reply fields are defined in `protocol/display_diag.h`,
`include/display_diag.h` and `protocol/page_control.h`; integers use little endian.
The protocol snapshot checker also rejects duplicate extension opcode definitions.

## Driver comparison / 与原厂驱动对照

当前为一份 480×854 RGB565 扫描缓冲，40 行 LVGL 绘制缓冲。每块直接拷到正在扫描
的缓冲，没有 VSYNC 等待；全屏通常需要 22 块。画面可能随绘制逐段出现。

原厂 `SENSOR3_MCU SV.04.00.04.00` 的初始化函数 `0x3de84` 为 LVGL 配置两份完整
帧缓冲 `0x80000000`、`0x800c8280`，每份 409920 像素。flush 函数 `0x2dc10`
写 LCD layer 0 地址 `0x4000204c`，设置 CR 的 VBPRELOAD（bit 15），等待该位自清，
再通知 LVGL 完成。按手册，该位在帧开始装载影子寄存器。这是整帧交换路径。

The current driver copies 40-row chunks into the active scan framebuffer and
does not wait for VSYNC. The reviewed stock driver uses two full framebuffers,
updates the LCD layer address, requests a frame-boundary shadow reload and waits
for completion. Its unbounded busy loop should not be copied into recovery-aware
community firmware.

## Device measurements / 真机测量（2026-11-07）

应用 MD5 `414de8b29dbca04856fe2e3a583025c5`；加载并确认保存的 SCN3 rev 23
之后，使用 ESP32 的远程换页逐个测量六个启用页。每页 22 次 flush，区域为
`y=0..39, 40..79, …, 840..853`，一次 monitor，23 条记录，无丢失。

| Page | LVGL refresh / ms | Capture elapsed / ms | Copy total / ms |
| --- | ---: | ---: | ---: |
| 1 | 684 | 725.458 | 141.091 |
| 2 | 693 | 735.719 | 140.573 |
| 3 | 613 | 635.647 | 140.933 |
| 4 | 613 | 647.298 | 140.717 |
| 6 | 661 | 695.293 | 141.040 |
| 7 | 661 | 705.103 | 141.176 |

帧结束中断统计平均周期约 25.09 ms（约 39.9 Hz），最小 25.031 ms、最大
26.518 ms。该范围包含中断响应延迟，尚无外部探头测量。单次拷贝最大约
8.2 ms；相邻拷贝之间的间隔中位数约 21–25 ms。当前代码及计时不支持
“每块等待约 25 ms 的 VSYNC”这一解释。拷贝占 LVGL 刷新约 20–23%，其余
时间包含软件绘制与调度；本次没有进一步把字体、透明混合和调度逐项分开。

The measured scan cadence is approximately 40 Hz, while complete page refreshes
take 613–693 ms in LVGL. Twenty-two synchronous copies consume approximately
141 ms in total, rather than one frame wait per chunk. Long intervals between
copies require drawing/scheduling comparisons. Framebuffer swaps can address
progressive presentation and copying, but cannot remove the remaining drawing
cost by themselves. These results describe this saved layout, with its existing
fonts, theme and wallpaper; they are not a bound for every user layout.

原始区域、时间和实际 MCU 页码确认保存在固件仓库的
`private/bry88-tls-display-diagnostics-20261107/page-*.json`。启动过程中布局
尚未同步时会产生新 generation；读取端必须拒绝拼接不同 generation 的记录。

Use measured data to choose the next change:

1. Many fast flushes with long gaps: optimize font scaling, alpha blending and
   drawing first. Larger draw buffers alone do not remove the pixel work.
2. Chunk durations dominated by frame periods: locate an actual synchronization
   wait. The current copy loop has none.
3. Adequate scan rate but visible progressive drawing: evaluate two independent
   full framebuffers, one swap per complete refresh, a bounded latch timeout and
   recovery on failure. Partial updates must synchronize unchanged areas of the
   buffers; merely allocating two arrays is insufficient.
4. If drawing dominates, compare opaque fill, predecoded RGB565 and the complete
   page before enabling DMA2D. Measure through actual DMA completion and check
   SDRAM contention with LCD scanning and sensor/OTA responsiveness.

本次数据优先支持以下实验：先比较纯色页、预解码图片页与完整页的绘制时间，
再验证独立完整 A/B 缓冲和一次帧边界交换；最后根据实测瓶颈评估 DMA2D 填充、
拷贝或混合。当前链接 SDRAM 约占 4.06 MiB / 8 MiB，额外一帧 RGB565 约
801 KiB，在容量上可行，但仍须验证地址不重叠、缓冲同步、锁存超时和恢复通道。

诊断元数据随后修正为从 CR/CRH/CRV 寄存器解码：手册第 575 页说明同步脉宽
已包含在后沿内，不能重复相加。当前实际总量为 560×894，120 MHz / 6 的
像素时钟对应 `560×894 / 20 MHz = 25.032 ms`，与稳定测量一致。修正版应用
MD5 为 `1012893aecc7368909de69aeb274126c`，只修正统计元数据，扫描配置不变。

Timing metadata is decoded from the actual registers. Sync width is already
included in the back porch: 560×894 clocks at 20 MHz predicts 25.032 ms.
The metadata correction leaves the LCD scan configuration unchanged.

后续已完成 108 次换页/重绘及外观对照。关闭全屏暗化时刷新中位数由 681 ms
降至 482 ms，取消卡片透明度后为 326 ms，拷贝仍约 141 ms。详见
[换页性能分析](PAGE-SWITCH-PERFORMANCE.md)，其中区分了实测收益、保留视觉效果
的实现方案和仍需验证的双缓冲/DMA2D 路径。

A subsequent 108-capture study compares redraws and transitions with controlled
appearance changes. Removing fullscreen dimming lowers the refresh median from
681 to 482 ms; opaque cards lower it to 326 ms while copies remain about 141 ms.
See [page-switch performance](PAGE-SWITCH-PERFORMANCE.md) for the evidence and
the distinction between measured contrasts and proposed implementations.

手册 v1.39 的 TIMER LOAD/VALUE 定义为 24 位；LCD 初始化中 SDK 的 `IntEOTEn=1`
会清除连续扫描位。本实现保留原初始化，再单独打开帧结束中断。
LVGL driver reference: [LVGL 8.3 display port](https://docs.lvgl.io/8.3/porting/display.html).
