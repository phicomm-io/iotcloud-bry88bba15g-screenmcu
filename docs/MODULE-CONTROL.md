# AM1008L / PD12

原厂应用在启动门控后配置 PD12 为输出高电平（0x4aa3c，由 0x4cd74 调用）。
本板已通过持续 UART2 查询的低／高电平对照确认：低电平停止 AM1008L 有效应答，
高电平恢复应答，MCU 与 ESP32 未重启。低电平不写入配置。

能力 bit 10（0x400）控制此扩展；原厂 ESP32 和未声明能力的对端保持原协议。
`protocol/module_control.h` 为字段和边界的唯一协议定义。

| 请求字节 | 类型 | 内容 |
| --- | --- | --- |
| 0..3 | u8 × 4 | 0x2b、版本 1、操作、保留 0 |
| 4..7 | LE u32 | 非零请求编号 |
| 8..11 | LE u32 | 持续时间毫秒 |

操作 0 查询、1 恢复高电平、2 暂时拉低；查询和恢复的持续时间必须为 0，拉低
允许 1..60000 毫秒。手动租约过期或链路会话重置时恢复高电平。正常业务下，
手动拉低期间继续发送 UART2 查询，不操作其他传感器的电源或整机锁存脚。

应用接管 GPIO 后将 PD12 拉低，整个 30 秒维护窗口、SDRAM、传感器与屏幕初始化
期间保持低电平，正常业务就绪后拉高。故障恢复只保留最小 OTA 服务并保持模块关闭。
MCU OTA 在元数据请求之前拉低，成功暂存、失败、取消或超时后恢复；维护状态的
硬件互锁不会被手动高电平、租约超时或链路重置解除。ESP32 上传自身或屏幕镜像前
等待低电平确认，以 60 秒租约、每 15 秒续租控制模块；上传失败释放，ESP32 升级
成功则保持到重启，重新建立 MCU 链路后恢复。旧屏幕固件不具备 bit 10 时保留原通道。
此控制从应用开始执行，未修改原厂引导区。

应答固定 20 字节：0x2c、版本 1、结果（0 成功／1 参数错误／2 不可用）、标志；
随后依次为请求编号、剩余毫秒、有效 UART2 帧数、电平变化次数，均为 LE u32。
标志 bit 0 已初始化、bit 1 GPIO 回读为高、bit 2 有拉低租约。正常业务每秒上报，
主动上报的请求编号为 0；ESP 的缓存 5 秒过期，会话变化后失效。
“已初始化”表示控制回调已就绪，不把首次上升沿的瞬时低电平锁定为永久失败。
实际电平始终单独回读；控制请求仍检查目标电平，不匹配时返回不可用。

## English

The factory application initializes PD12 high after its startup gate. On this
board, a low/high comparison with uninterrupted UART2 queries confirmed that
PD12 low stops AM1008L replies and high resumes them without resetting either
processor. A low level is never persisted.

Capability bit 10 gates the 12-byte request (0x2b) and 20-byte response (0x2c).
The table above and `protocol/module_control.h` define the exact fields. Request
IDs are nonzero; action 0 queries, 1 restores high, and 2 leases a low level for
1..60000 ms. Query/high require zero duration. Expiry and link reset restore
high during ordinary business; UART2 queries continue during a manual lease.
No other sensor power rail or system power latch is changed.

The application drives SET low before its 30-second recovery window and keeps
it low until SDRAM, sensors and the panel are ready. Fault recovery stays low
with UART OTA available. MCU OTA inhibits the module before metadata/erase and
releases it on completed staging, cancellation, failure or timeout. Manual high,
lease expiry and link reset cannot override this interlock. ESP upload handlers
wait for low readback before flash writes, renew a 60-second lease every 15
seconds, and release on failure/completion. A successful ESP application upload
retains the lease through reboot; link reinitialization restores high. Peers
without bit 10 retain their original OTA path. The original bootloader is unchanged.

The response contains result, initialized/high/leased flags, request ID,
remaining milliseconds, accepted UART2 frames and actual level transitions.
Periodic reports have request ID zero. ESP readback expires after five seconds
or a session change. Web controls use the existing openiot MessagePack API.
Initialized means the GPIO callbacks are installed; a low read immediately
after the first rising edge cannot permanently disable the interlock. Pin
status remains an independent readback, and requests still reject a mismatch.
