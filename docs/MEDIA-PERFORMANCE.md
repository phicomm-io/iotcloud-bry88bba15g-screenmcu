# Media rendering / 媒体绘制

JPEG uses the SWM34S baseline hardware decoder. The camera and notification
images use opaque RGB565 with nearest-neighbor scaling. Their draw context
computes horizontal sampling once per strip and copies repeated enlarged rows;
it matches LVGL 8.3.11 reciprocal zoom and half-pixel rounding. Rotation,
antialiasing, opacity, recoloring, blend modes and active masks use the normal
LVGL software renderer. Native frame comparisons cover these fallbacks.

Two complete independent framebuffers separate rendering from LCD scanout.
After the final strip, LCD VBPRELOAD latches the completed back frame at frame
start. Only after hardware clears VBPRELOAD may the old front frame be reused.
The CPU synchronizes the dirty bounding rectangle into that released frame
with LVGL's aligned word copy, preserving unchanged partial updates. A 200 ms
reload timeout enters internal-SRAM recovery without overwriting the scanned
frame. Decode and notification presentation wait for this sequence.

The production default uses CPU word copies. Earlier concurrent CPU/DMA2D
rendering and whole-frame DMA mirrors caused visible shaking on real updates;
a CPU fallback was reported stable. The diagnostic build option
`SCREEN_EXPERIMENTAL_DMA2D` enables a revised strip-only DMA2D path: one active
480×40 draw buffer prevents simultaneous CPU pixel drawing and DMA reads,
512 HCLK wait cycles separate 64-word blocks, and a completion interrupt wakes
the waiting CPU. Whole-frame synchronization uses CPU copies. DMA startup
checks RGB565 order, row stride and guard pixels; a 50 ms timeout resets DMA
and retains CPU copying for later flushes. The second allocated strip is used
only as startup scratch. This candidate requires physical display comparison
before becoming the default.

The second complete frame adds 819840 bytes. The linked image reserves its
128 KiB LVGL arena and 64 KiB libc heap, with another 896 KiB of SDRAM required
beyond them.

The display diagnostic records each flush through actual DMA completion,
including CPU overlap. Sum of flush durations is therefore not additive with
drawing time. The board summary adds render_flags at byte 120 (bit 0 row
scaler, bit 1 DMA available, bit 2 two active strips, bit 3 DMA failure, bit 4 two full
frames with frame-start reload, bit 5 DMA2D clock gated, bit 6 JPEG clock gated)
and dma_flushes
at byte 124. Older MCU versions return zero for these optional fields.

LCD keeps scanning the front framebuffer at 120 MHz / 8 / (560 × 894), about
29.96 Hz. Experimental DMA2D yields 512 HCLK cycles between 64-word blocks to reduce contention
with continuous LCD reads. DMA2D is clock gated after a flush completes; JPEG
is clock gated after decoding. Each peripheral is enabled before its next job.
Unchanged UI content does not invalidate the display, so static frames need no
repeated drawing, copying or decoding. CPU idle uses WFI, retaining the 1 kHz
timebase, LCD, SDRAM refresh and UART. These states do not measure electrical
power savings; board current must be measured separately.

当前使用 JPEG 硬解码和按行快速缩放，双完整帧通过 VBPRELOAD 在帧开始时切换。
硬件确认后，用 CPU 按字拷贝将脏矩形同步到已释放的旧前台帧。默认使用 CPU 搬运；
早期 CPU/DMA2D 并发绘制、整帧 DMA 同步在实机更新时抖动，而 CPU 回退版得到用户
不抖动的反馈。实验编译选项 `SCREEN_EXPERIMENTAL_DMA2D` 启用改进的条带 DMA：
只使用一份 40 行活动绘制缓冲、每 64 字插入 512 个时钟间隔、完成中断唤醒等待的
CPU，整帧同步继续按字拷贝。待实机对照后再决定默认路径。50 ms DMA 超时后回退
CPU，200 ms 未完成帧交换则进入恢复服务。最后一次 flush 包括帧边界等待和脏区域
同步，不能当成 DMA 纯搬运耗时。

LCD 保留连续扫描，像素时钟 15 MHz，约 29.96 Hz。画面不变时不重复绘制、搬运或
解码。DMA2D 每次完成刷新后关时钟，JPEG 从启动及每次解码完成后关时钟，下次工作
前恢复；CPU 空闲时保持 WFI。屏幕扫描、SDRAM 刷新、串口及 1 kHz 时基继续工作。
实际整机省电幅度需要单独测电流。

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
