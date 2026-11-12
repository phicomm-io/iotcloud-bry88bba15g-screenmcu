# Rendering and memory / 绘制与内存

换页、唤醒、布局或配色版本变化在前台循环内触发刷新，不再等待一秒的周期更新。
PD11 仍按 33 ms 周期识别，熄屏后的第一次触摸仍仅唤醒。普通数据继续按秒检查。

当前页的时钟和数值卡片保留对象，只改变文字、拟合字号、颜色和位置。
天气、提醒及图表只重建发生变化的卡片内容；网络和电池图标独立更新。
页面或布局、主题、壁纸变化时才重建整页，不预建不可见页面。
每页至多保留八个组件记录及八份固定图表数据，绘图回调只访问持久 RAM。
预设壁纸每轮仅生成四行，不递归处理串口；切换预设后立即上传图片可正常开始事务。

本地曲线有历史后按分钟更新，UART 高频读数不触发全历史重算；无历史时仍可立即
显示第一个读数。缺测断线，已有不足 24 小时的数据拉伸显示。不同组件的图表数据
使用独立固定槽，更新一个卡片不会覆盖另一个卡片的绘图回调数据。

缩放字体使用 16 个 LRU 槽，每槽最多 192×192×2/8 字节，像素总量 144 KiB。
键为字体（含字号、基字库）和字符，全角 ASCII 先归一化。缓存命中不再逐像素缩放；
不分配堆内存。板级链接将缓存放到 SDRAM，内部 SRAM 继续供通信、维护与栈使用。

## Validation

`ui_render_test` checks stable label/card identity over 200 value changes,
unchanged neighbouring charts, minute history updates, equal bitmaps after
LRU eviction, 200 consecutive hits for a ten-digit number, and constant LVGL
free heap. Existing pixel tests cover all six palettes, alpha surfaces,
weather symbols, and three-unit charts. Host timings are recorded separately;
they are not the MCU's page-switch latency.
Preset generation is limited to four rows per foreground turn; the wallpaper
test starts an upload immediately after the first batch and checks the final pixels.

The earlier glyph-cache stage reserved approximately 2.10 MiB of 8 MiB SDRAM.
With the full external font cached in RAM, the current allocation is approximately
4.06 MiB before media buffers; use the frozen map for exact figures. Current
media buffers and spare memory are described in [media rendering](MEDIA-PERFORMANCE.md).
The internal SRAM static data ends at approximately 10.4 KiB, with the separate
16 KiB stack and retained recovery record. Linker region usage also includes
the gap before that retained record, so it is not a runtime high-water mark.
Exact addresses and Flash usage are in each frozen ELF/map/manifest.

## Next resource stages / 后续资源阶段

完整中文字库已移至可选 IOTFNT02 资源包。内部只保留数字、单位、常用界面和维护
字体，应用链接占用约 190 KiB / 412 KiB。双 NOR 字库区经完整校验后放入 SDRAM；
坏包、读错或超时即降级，绘图只访问 RAM。格式、限时、上传与回退见
[字库说明](EXTERNAL-FONT.md)。

The optional 32 px, 2 bpp font bank replaces the full internal Chinese font. The
application uses approximately 190 of its 412 KiB Flash region; linked SDRAM
allocation is approximately 4.06 MiB. Drawing reads validated RAM only, and
resource failures latch a fallback. See [font resources](EXTERNAL-FONT.md).

媒体版本已启用 JPEG 硬解、DMA2D、双条带和完整双帧交换，并将连续扫描降至约 30 Hz；
见 [媒体绘制](MEDIA-PERFORMANCE.md)。硬件 FPU 尚未启用，传感器补偿运算顺序保持
原样。壁纸加载已分批进行并有总时限，见 `APPEARANCE.md`。下方保留早期版本测量。

已增加 1 MHz flush/monitor 记录和 LCD 帧结束中断统计。六个启用页的首轮真机
数据为约 40 Hz 扫描、每页 22 次拷贝、拷贝合计约 141 ms、LVGL 整轮刷新
613–693 ms。记录读取和原厂完整帧交换路径的对照见
[显示诊断](DISPLAY-DIAGNOSTICS.md)。

The display diagnostic records real copy durations, LVGL refresh rounds and
controller frame-end periods. The first six-page device run measured about
40 Hz scanning, 22 copies totaling about 141 ms per page, and 613–693 ms LVGL
refreshes. See [display diagnostics](DISPLAY-DIAGNOSTICS.md) for the limits and
the comparison with the stock framebuffer-swap path.

后续 108 次真机对照已量出全屏暗化约 199 ms 的重复开销，并确认 libc flush
拷贝为字节循环。保留视觉效果的优化顺序、拷贝执行对照及复杂图表分析见
[换页性能分析](PAGE-SWITCH-PERFORMANCE.md)。

The subsequent 108-capture comparison isolates about 199 ms of repeated
wallpaper dimming and identifies the byte-at-a-time libc copy in flush. See
[page-switch performance](PAGE-SWITCH-PERFORMANCE.md) for measured contrasts,
copy instruction/access comparisons and the implementation order.
