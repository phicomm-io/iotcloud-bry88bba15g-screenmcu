# Media rendering / 媒体绘制

JPEG uses the SWM34S baseline hardware decoder. Camera and notification images
use opaque RGB565 pixels with nearest-neighbor scaling. Horizontal sampling is
computed once per strip, and repeated enlarged source rows use aligned copies.
The result matches LVGL 8.3.11 reciprocal zoom and half-pixel rounding. Rotation,
antialiasing, opacity, recoloring, blend modes and active masks use LVGL's normal
software renderer. Native frame comparisons cover these fallbacks.

The camera card and its letterbox are fully transparent, revealing the existing
page wallpaper or canvas. Image pixels remain opaque. The WebUI preview uses
the same geometry and transparency. Notification back cards precompose their
decorative alpha into solid RGB565 colors once at creation, retaining their
rounded stacked shape. The charging glyph has a transparent background.

Two independent full framebuffers separate rendering from LCD scanout. Only the
back frame is written. After the final strip, LCD VBPRELOAD latches the complete
back frame at frame start. The driver waits for hardware to clear VBPRELOAD
before reusing the old front. A 200 ms reload timeout enters internal-SRAM
recovery without writing the scanned frame.

Released frames keep conservative stale spans for each row. After a strip has
finished, the CPU copies any stale pixels outside that strip's newly drawn
span. Before presenting, it preserves remaining untouched rows. It never copies
pixels that the current refresh replaces. Consecutive full redraws or identical
camera rectangles therefore avoid the former post-swap mirror entirely.
Disjoint and overlapping partial updates retain their unchanged pixels. Native
tests compare 150 randomized refreshes against an independent full-frame model,
check frame ownership at every presentation and require zero preservation copies
for complete redraws and repeated camera bounds.

`SCREEN_DMA2D` defaults ON. One active 480×40 LVGL draw buffer serializes CPU pixel
drawing and DMA reads. DMA2D waits 512 HCLK cycles between 64-word blocks, allowing
the LCD to refill its FIFO; its completion interrupt wakes the waiting CPU.
Preservation uses aligned CPU copies after DMA completion. Earlier concurrent
rendering and large DMA mirrors shook during real updates. The revised serial
path was reported stable on the test display; the stale-span optimization keeps
that transfer interval, ownership and serialization unchanged. Startup checks
RGB565 order, row stride and guard pixels. A 50 ms timeout resets DMA and keeps
CPU fallback for subsequent flushes. Set `SCREEN_DMA2D=OFF` for comparison.
The second allocated strip is startup scratch only.

The second full frame adds 819840 bytes and stale spans add 3416 bytes. The linked
image reserves a 128 KiB LVGL arena and 64 KiB libc heap, with another 896 KiB of
SDRAM required beyond them.

Diagnostics time each flush through real completion, including preservation and
the final frame-boundary wait. Flush time is not pure DMA transfer time. Board
summary byte 120 is render_flags: bit 0 fast row scaler, bit 1 DMA available,
bit 2 two active strips, bit 3 DMA failure, bit 4 full frames with frame-start
reload, bit 5 DMA2D clock gated, bit 6 JPEG clock gated. Byte 124 is dma_flushes.
Older MCU versions return zero for these optional fields.

LCD scans at 120 MHz / 8 / (560 × 894), approximately 29.96 Hz. DMA2D is clock
gated after transfer, and JPEG from startup and after decoding; each is enabled
before its next job. Unchanged content does not invalidate the display. CPU
idle uses WFI, retaining the 1 kHz timebase, LCD, SDRAM refresh and UART. Electrical
power savings require a board-current measurement.

DMA2D also supports fill, conversion and blending in the official SDK. These
operations share SDRAM bandwidth with scanout. Enabling LVGL's generic SWM341
backend would require the same bounded waits, clock ownership and fallback as
this driver; it is not a drop-in acceleration switch. Current changes reduce
redundant reads and writes while preserving the validated arbitration interval.
The ESP remains a bounded TCP/TLS byte relay. Extra queued frames increase
latency without removing UART delivery time; compare completed camera frames,
page latency and control/sensor health when assessing further acceleration.

摄像头卡片和图片四周留白完全透明，透出页面壁纸或底色；画面像素仍为不透明
RGB565。网页预览同步透明度和尺寸。通知叠层在创建时预先计算实色，保留圆角
和层次；充电闪电保持透明背景。

当前默认启用 DMA2D，单份 40 行活动缓冲串行完成绘制和搬运，每 64 字保留
512 个时钟间隔，完成中断唤醒等待的 CPU。实机已反馈这种串行路径不再抖动。
保持约 30 Hz 扫描、完整双帧和帧开始交换。旧前台帧释放后只标记每行脏区，
下一轮优先让新画面直接覆盖；只用 CPU 补齐未重画但需要保留的像素，再请求
交换。连续全屏换页及相同摄像头区域不再做额外整帧同步。

150 轮随机局部更新在每次提交前与独立整帧模型比对，包含不相交、重叠及同行
分离区域；完整重画和重复摄像头区域要求旧帧复制量为零。50 ms DMA 超时回退
CPU，200 ms 帧交换超时进入恢复服务。可用 `SCREEN_DMA2D=OFF` 做 CPU 对照。

画面不变时不重复绘制、搬运或解码，JPEG 和 DMA2D 在空闲时关闭时钟，CPU
使用 WFI。LCD、SDRAM 刷新、UART 和 1 kHz 时基继续工作，实际省电幅度需测电流。
SDK 还提供填充、格式转换和混合，但都竞争 SDRAM 带宽；直接启用通用 LVGL
后端还需处理同步、时钟、超时和回退，不能仅凭支持硬件加速就判断更快。
