# Media rendering / 媒体绘制

JPEG uses the SWM34S baseline hardware decoder. The camera and notification
images use opaque RGB565 with nearest-neighbor scaling. Their draw context
computes horizontal sampling once per strip and copies repeated enlarged rows;
it matches LVGL 8.3.11 reciprocal zoom and half-pixel rounding. Rotation,
antialiasing, opacity, recoloring, blend modes and active masks use the normal
LVGL software renderer. Native frame comparisons cover these fallbacks.

Two independent 480×40 LVGL draw buffers let the CPU draw the next strip while
DMA2D moves the previous one into a separate back framebuffer. A real
startup test checks RGB565 order, row stride and untouched guard pixels before
enabling DMA. Completion releases each borrowed draw buffer exactly once.
A 50 ms strip timeout (250 ms for a full-frame mirror) resets DMA and copies
the pending rectangle on the CPU; later
flushes keep using the CPU. After the final strip, LCD VBPRELOAD latches the
completed back frame at frame start. Only after hardware clears VBPRELOAD may
the old front frame be reused. DMA2D synchronizes the dirty bounding rectangle
into that released frame, preserving unchanged content across partial redraws.
A 200 ms reload timeout enters internal-SRAM recovery without overwriting the
scanned frame. Foreground decode and notification presentation wait for this
sequence. The second strip adds 38400 bytes; the second frame adds 819840 bytes.
The linked image reserves its 128 KiB LVGL arena and 64 KiB libc heap, with
another 896 KiB of SDRAM required beyond them.

The display diagnostic records each flush through actual DMA completion,
including CPU overlap. Sum of flush durations is therefore not additive with
drawing time. The board summary adds render_flags at byte 120 (bit 0 row
scaler, bit 1 DMA enabled, bit 2 two strips, bit 3 DMA failure, bit 4 two full
frames with frame-start reload) and dma_flushes
at byte 124. Older MCU versions return zero for these optional fields.

当前采用 JPEG 硬解码、按行快速缩放、两份 40 行绘制缓冲和 DMA2D 搬运。缩放结果
与生产 LVGL 的整帧像素比较；透明度、旋转、圆角遮罩等情况自动走通用绘制路径。
启动时验证真实 DMA 的颜色、行跨度和保护像素，失败或超时后回退 CPU。新增 SDRAM
38400 字节，第二块完整帧缓冲增加 819840 字节。绘制只写后台帧，完整刷新后通过
VBPRELOAD 在下一帧开始时切换；硬件确认后，将脏矩形同步到已释放的旧前台帧。
不改像素时钟或连续扫描配置。200 ms 未完成交换则进入恢复服务。诊断中的最后
一次 flush 包括帧边界等待和脏区域同步，不能直接当成 DMA 纯搬运耗时。

Camera cards and image letterboxes use opaque backgrounds. Notification back
cards precompose their decorative alpha into solid RGB565 colors once at
creation. They retain the rounded stacked shape without full-card alpha
blending during redraw. The charging glyph remains transparent.

摄像头卡片和图片留白区域使用不透明底色；通知叠层在创建时预先算好实色，保留
圆角和层次，避免整张卡片的重复透明混合。闪电图标继续使用透明背景。

The ESP remains a bounded TCP/TLS byte relay. More queued video frames can
increase latency, and extra RGB buffers do not remove UART bandwidth or JPEG
delivery time. Compare actual completed-frame rates and control/sensor health
before choosing further buffering or a full-frame swap architecture.
