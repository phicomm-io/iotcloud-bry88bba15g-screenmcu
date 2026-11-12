# Media rendering / 媒体绘制

JPEG uses the SWM34S baseline hardware decoder. The camera and notification
images use opaque RGB565 with nearest-neighbor scaling. Their draw context
computes horizontal sampling once per strip and copies repeated enlarged rows;
it matches LVGL 8.3.11 reciprocal zoom and half-pixel rounding. Rotation,
antialiasing, opacity, recoloring, blend modes and active masks use the normal
LVGL software renderer. Native frame comparisons cover these fallbacks.

Two independent 480×40 LVGL draw buffers let the CPU draw the next strip while
DMA2D moves the previous one into the existing LCD scan framebuffer. A real
startup test checks RGB565 order, row stride and untouched guard pixels before
enabling DMA. Completion releases each borrowed draw buffer exactly once.
A 50 ms timeout resets DMA and copies the pending rectangle on the CPU; later
flushes keep using the CPU. The final strip completes before foreground media
decoding or notification presentation proceeds. This adds 38400 bytes of SDRAM.

The display diagnostic records each flush through actual DMA completion,
including CPU overlap. Sum of flush durations is therefore not additive with
drawing time. The board summary adds render_flags at byte 120 (bit 0 row
scaler, bit 1 DMA enabled, bit 2 two strips, bit 3 DMA failure) and dma_flushes
at byte 124. Older MCU versions return zero for these optional fields.

当前采用 JPEG 硬解码、按行快速缩放、两份 40 行绘制缓冲和 DMA2D 搬运。缩放结果
与生产 LVGL 的整帧像素比较；透明度、旋转、圆角遮罩等情况自动走通用绘制路径。
启动时验证真实 DMA 的颜色、行跨度和保护像素，失败或超时后回退 CPU。新增 SDRAM
38400 字节。LCD 仍连续扫描同一帧缓冲，逐块更新可能产生撕裂；双份绘制条带并非
整帧 VSYNC 交换。诊断中的 DMA 完成时间包含与 CPU 绘制重叠的时间。

The ESP remains a bounded TCP/TLS byte relay. More queued video frames can
increase latency, and extra RGB buffers do not remove UART bandwidth or JPEG
delivery time. Compare actual completed-frame rates and control/sensor health
before choosing further buffering or a full-frame swap architecture.
