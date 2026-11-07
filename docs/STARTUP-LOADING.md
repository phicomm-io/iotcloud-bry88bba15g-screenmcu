# 启动加载与传感器接收

普通启动维护窗口结束后，LVGL 顶层显示旋转加载动画、字库读取进度和五项传感器
就绪数量。加载文字属于内部精简字体，不依赖外部 NOR。动画不占用触摸事件，休眠
和 OTA 时隐藏，仍可正常翻页。

完整字库经过 CRC 和字形检查才显示就绪；读取失败使用内置字体。传感器等待最多
60 秒，未安装的甲醛扩展不计入五项。等待结束后，降级说明停留 5 秒再消失，不复位
设备或重启加载循环。

UART1/2/3 分别接收 CO₂、AM1008L 和甲醛扩展，每路 256 项内部 SRAM 中断缓冲。
字节携带真实接收时刻产生的帧间隔标记，后台 NOR 读取和 LVGL 绘制延迟不会被误当
作线上帧间隔。溢出时丢弃整段积压及未完成帧，下一完整且校验正确的报文恢复接收。
传感器换算和校准顺序保持原实现。

外部 NOR 的 PD5/PD6/PD8/PD7/PD3/PD4 使用芯片专用 SFC 硬件控制器，不是
SPI0/1。读取先与 GPIO 数据比较；已有 QE 配置时尝试四线 `0xEB`，否则使用
单线 `0x03`。时钟为系统时钟的 1/8（240 MHz 时为 30 MHz）。初始化不改写
NOR 状态寄存器或 QE；硬件比较或忙状态检查失败后退回 GPIO，本次启动不再反复
尝试硬件。控制器忙检查上限为 3 ms，另有有限循环预算。

字库每轮最多读取 8 KiB，或在 8 ms 后让出前台；CRC 随读取增量计算，完成时不再
重扫整个字库。绘制只访问校验后的 SDRAM 数据。资源报告包含实际读取模式、已读
字节和加载耗时，便于区分硬件路径、GPIO 降级和坏字库；NOR 出错不触发设备复位。

# Startup loading and sensor reception

After the normal maintenance window, a nonblocking LVGL overlay shows a spinner,
font progress and readiness of the five standard sensors. Its text uses internal
subset fonts. Paging remains available; the overlay hides during sleep and OTA.

Only validated glyph data and CRC completion mark the font ready. Failed NOR
loads fall back to built-in fonts. Sensor startup waits at most 60 seconds and
does not require the optional formaldehyde module. A limited-data notice lasts
five more seconds, then disappears without resetting or restarting the loader.

UART1/2/3 use independent 256-entry interrupt queues in internal SRAM. Wire-gap
markers use interrupt receive times, so delayed foreground work does not split
valid frames. Overflow discards the backlog and partial frame; the next complete
checksum-valid message recovers normally. Sensor conversion and calibration
ordering are unchanged.

The NOR pins PD5/PD6/PD8/PD7/PD3/PD4 use the dedicated SFC controller rather
than SPI0/1. The first hardware read is compared with GPIO data. Quad `0xEB`
is attempted only when QE is already enabled; otherwise standard `0x03` is
used. SFC runs at system clock / 8 (30 MHz at 240 MHz). Initialization does
not change NOR status or QE. A comparison or controller busy failure latches
GPIO fallback until reboot. Busy checks have both a 3 ms deadline and a finite
iteration budget.

Font loading processes at most 8 KiB or 8 ms per foreground slice. CRCs are
updated incrementally rather than rescanning the completed font. Rendering
uses validated SDRAM data only. Resource telemetry reports the selected read
mode, received bytes and load duration. NOR errors do not request a device reset.
