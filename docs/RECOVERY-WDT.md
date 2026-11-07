# 启动维护窗口、WDT 和引导区读出

最小维护应用已通过原厂 UART OTA 在板启动，两份 80 KiB 引导备份一致。
当前安装检查 NOR 应用摘要、回读和原厂复制尾区，不再要求引导备份、
引导 MD5 白名单或首装恢复镜像。原厂引导区不改写。实际进度见 [烧录工作流](FLASH-TEST-20261105.md)，
引导搬运范围见 [本板引导分析](BOOTLOADER-20261105.md)。

## 原厂 WDT 证据

本次核对的是 `SENSOR3_MCU-SV.04.00.04.00.bin`，SHA-256 为
`1d0b848af6195b39849260a3c9ff2a020d574ca9b98c2c2665d2278f349796c2`。
首次替换前设备回报的原厂版本是 `SV.04.00.01.00`，尚未逐字节比对这两个应用版本。

| 原厂地址 | 行为 |
| --- | --- |
| `0x4ccde` → `0x20110` | 初始化 WDT，复位计数值 `0x1400`（5120），分频 32，复位开启、IRQ 关闭 |
| `0x4cce4` → `0x20184` | 启动 WDT |
| `0x20108` | 向 `0x400a0810` 写入 `0x55` 喂狗，不是 GPIO toggle |
| `0x4cd62` | 启动按键等待期间喂狗 |
| `0x4d40a / 0x4d442 / 0x4d516` | 业务循环喂狗 |
| `0x2d8fc / 0x2d944 / 0x51f28 / 0x51f70` | OTA Flash 擦除路径喂狗 |

原厂应用确实配置并启动 WDT；其初始化没有重选 `SYS.CLKSEL[13:12]`
时钟源，系统时钟初始化也保留这些位。因此存在继承引导程序时钟设置的情况。
若时钟是 32768 Hz，5120 × 32 / 32768 = 5 秒；在取得寄存器快照前不能固定认定为 5 秒。
已取得本板引导程序；首次社区启动的继承 CR/RSTVAL 均为 0，说明该次交接时 WDT
未运行。它不能证明所有引导分支都不使用 WDT，后续应用仍显式接管。

## 社区启动路径

1. 入口屏蔽中断，设置 MSP/CONTROL/BASEPRI，建立内部 SRAM 数据和向量表。
2. 保存继承的 WDT CR、RSTVAL、CLKSEL 和 RSTSR。按官方手册先停止 WDT 再切换时钟；处理已有窗口模式、非零计数器及喂狗后的同步等待。
3. 明确选择内部 LRC 32 kHz、32 分频、8192 计数，约 8.2 秒硬件复位；记录后仅 W1C 清除 WDT 复位位。
4. 保持 PM7 电源锁存，开放 UART0 115200 8N1 的 30 秒维护窗口；此时不初始化 SDRAM、LVGL、屏幕和传感器。
5. 窗口到期且没有 hold/OTA 后才启动业务。WDT 复位或故障锁存使维护模式持续等待。`SCREEN_RECOVERY_ONLY=ON` 镜像始终运行维护服务。

喂狗只由前台进展路径执行，间隔至少 250 ms；SysTick ISR 只计时。
Flash 每个擦除块前后喂狗，驱动等待有超时。主循环卡死不会因定时器仍运行而持续喂狗。
故障处理记录恢复请求后软件复位，硬件 WDT 作后备。

2026-11-06 修复普通复位误入强制维护的问题：旧版在进入正常业务前也写入了
“故障”魔数。SRAM 在复位后保留时，下一次启动因此被当作故障，永久停在维护
服务，传感器没有初始化。正常入口现在清除锁存；真实异常和显式恢复请求仍设置
锁存，WDT 复位仍根据硬件复位位进入维护。没有取消故障隔离或安装门控。
`tools/check_normal_reset.py` 执行已编译的 ARM 镜像，复现旧版普通入口留下
魔数并在保留 SRAM 的复位后进入 mode 3；修复版则清零并重新进入 mode 1。
该模型检查复位策略，不代替物理复位、供电或完整业务实机验证。

实机复位测试发现，低地址 `.noinit` 会被原厂 scatter loader 覆盖。实际引导
`0x6f4` 的两条初始化记录分别复制 128 B 到 `0x20000000`，再清零
`[0x20000080,0x20006230)`；初始 MSP 也是 `0x20006230`。因此恢复魔数、
反码和会话后备计数最初固定放在 `0x2000bff0..0x2000bfff`。诊断版将保留区
扩为 `[0x2000bfc0,0x2000c000)`，增加启动阶段和异常寄存器；应用的 16 KiB 栈
仍从 `0x2000c000` 开始，链接断言防止两边覆盖。故障汇编不依赖已有栈，
先保存寄存器、写魔数及反码，再请求复位。`tools/check_boot_retention.py` 可执行已备份
引导的真实 scatter 初始化并检查链接后的地址。修复后的完整镜像已在本板实际软件
复位并保持强制维护；随后双遍备份、重装和自然退出启动窗口也已通过。后续修改
保留区或引导路径时仍需重新执行对应实机检查。

这条恢复路径要求引导程序成功跳转到 `0x14000` 的应用入口。首次最小应用已证实
搬运和 UART 重入；物理掉电中断搬运没有测试，也没有 MCU 双应用槽回滚。
安装只允许已核对的引导版本和空白 NOR 尾区，条件不满足时保持维护服务。

## 独立维护协议

每 5 秒输出以下通知；该会话独立于 UI 能力协商，原厂 ESP 无须认识新增指令：

```text
screen_recovery 3 <nonceHex8> <sequenceDecimal> <modeDecimal> <remainingMsDecimal> <oldCRHex8> <oldRSTVALHex8> <oldCLKSELHex8> <oldRSTSRHex8>\r\n
screen_image 1 <actualInternalApplicationMD5Hex32>\r\n
```

mode：0 正常业务，1 启动窗口，2 hold，3 故障／最小维护。
请求复用 `screen_ext 1` 的 CRC32/Base64 帧，以通知中的非零 nonce 和递增序号发送。

诊断版另发送 `screen_ext 1` 帧。v1 payload 为 `0xe2,1` 和 13 个 LE32 字段，
总长 54 字节：reason、当前阶段、上次故障阶段、IPSR、CFSR、HFSR、MMFAR、
BFAR、异常 LR、异常 MSP、RTC 状态、SDRAM 自检错误、MCU 毫秒计数。v2 使用
`0xe2,2`，保留前 13 个字段，追加故障 PC、栈内 LR 和 xPSR，总长 66 字节。
v3 使用 `0xe2,3`，再追加失败比较读取到的实际值、预期值、字段地址和传感器
初始化检查点，共 20 个 LE32 字段、82 字节。这四项在内部 SRAM 保存；实际值来自
失败的同一次 volatile 读取，不重新读取受损字段。ESP 的 `diagnostic_max_version`
声明解析上限，仍接受 v1/v2 通知。
它是独立通知，不消耗请求序号，也不完成或取消待处理请求。旧 ESP 忽略未知类型。
ESP 仅接受 CRC、会话、版本、长度和字段范围正确的通知；15 秒后过期，换会话清除。

reason 为 0 正常启动、2 保留的异常／显式恢复、3 WDT 复位、4 最小维护镜像、
5 引导标志确认失败、6 SDRAM 自检或控制指针检查失败。阶段 1–14 依次为早期入口、维护窗口、
SDRAM、BSS、原厂业务初始化、RTC、传感器、LVGL、面板、正常循环、RTC 轮询、
传感器轮询、电源处理、渲染。SDRAM 错误 1 为控制器等待超时，2 为首地址读回
失败，3 为地址线测试失败，4 为零地址哨兵受损。错误 5–14 分别表示 RTC read、
RTC write、RTC context、runtime send、runtime factory、runtime context、
runtime calibration_event、factory runtime、factory maintenance、factory power
指针与初始化值不同。检查直接读取 SDRAM；失败后关闭已初始化的背光和 LCD，保留
电源锁存，在内部 SRAM 的串口维护服务中停留，不尝试重写坏指针。
`SCREEN_STARTUP_DIAGNOSTIC=ON` 在 RTC、传感器、LVGL、面板和背光启动后各观察
5 秒，正常镜像不增加这些等待。RTC 状态 0 未知、1 正常、
2 无效时间、3 I/O 失败、4 写后验证失败。业务未启动时不会读取 SDRAM 内的 RTC 对象。

`SCREEN_UART2_QUIET=ON` 是 PM2.5/TVOC 模块接入故障的诊断选项，默认关闭。
它保留 UART2 引脚、波特率、供电 GPIO 和其他业务初始化，但禁止 UART2 查询、
校准命令和接收 FIFO 读取；校准发送返回失败。该通道的两个数据保持未收到状态。
镜像身份带 `uart2=quiet;`，构建清单的 `uart2_quiet` 为 true。30 秒最小维护窗口、
看门狗和串口 OTA 保留。此选项用于比较串口处理的影响，不等于物理隔离信号线或
降低模块功耗，也不是完整传感器运行固件。

`SCREEN_BACKLIGHT_MAX_PERCENT` 默认 `100`，可构建 `0..100` 的诊断亮度上限。
`0` 在 LCD/SDRAM 与其他业务照常初始化、运行时始终写入零背光占空比；屏幕会保持
黑暗，触摸、设置和唤醒也不能越过此上限。它不修改保存的亮度，不操作猜测的模块
SET/电源引脚。小于 `100` 时镜像带 `backlight_max=N;`，清单记录
`backlight_max_percent`；正常默认镜像不增加身份标记。该选项用于对比背光开启的
影响，保留串口 OTA、30 秒最小维护窗口、WDT 和故障保护；不作为显示验收固件。
核心寄存器记录不依赖异常栈。v2 仅在 MSP 属于内部应用栈、8 字节对齐、完整帧
未越界、没有堆栈异常且 xPSR Thumb 位有效时读取附加字段；支持基本帧和浮点扩展
帧。PSP 或无效帧的附加字段为零，仍写入恢复锁存并请求复位。不会读取外部 SDRAM
异常栈；故障地址寄存器仅在对应有效位存在时有意义。

传感器检查点：1 进入；2–7 依次为 UART1 TX/RX、UART2 TX/RX、UART3 TX/RX
复用设置完成；8–10 为三路 UART 启动完成；11–12 为 SHT SDA/SCL GPIO 完成；
13 两线释放；14 PN4 输出模式；15 PN4 置高；16 电池 ADC 引脚；17 ADC 配置；
18 ADC 序列配置；19 ADC 开启；20 SHT 软复位命令返回。每个检查点检查全部十个
控制指针。该编号表示刚完成的操作；延迟出现的损坏不一定由该操作引起。

| 请求字段 | 内容 |
| --- | --- |
| 字节 0、1 | `0xe0`、操作码 |
| 字节 2–5 | offset，LE32 |
| 字节 6–7 | length，LE16 |
| 操作码 | 1 hold、2 resume、3 只读引导区、4 软件复位进入维护、5 校验后请求原厂引导安装 |
| 响应 | `0xe1, op, result, offset/LE32, total/LE32, bytes...` |
| result | 0 成功、1 越界／格式错误、2 OTA 忙、3 不支持、4 读取失败 |

只读范围 `[0,0x14000)`，每块 1–256 字节；直接总线读取，不调用 IAP，
不改 Flash、保护位或调试引脚。固定帧长度、CRC、会话、递增序号及 OTA 互斥均检查。
hold/resume 仅用于普通启动窗口；resume 会立即取消剩余等待，也可解除 hold；OTA 进行中返回 busy，强制故障维护不接受 resume。网页“屏幕 → 资源与升级”提供“立即继续启动”和“保持最小服务”。复位命令在应答发送成功后执行。

配套 ESP32 现在默认于 MCU 窗口开始约 5 秒后发送现有 resume 操作。网页可关闭
“启动 5 秒后自动继续”，持久保存后保留完整 30 秒窗口。手动 hold 优先，故障模式
不会自动放行，资源传输期间延后操作；MCU 本身继续保留完整窗口作为旧 ESP 的后备。
此功能没有修改原厂引导，也不绕过完全断电后的前按键开机门控。

The companion ESP32 now sends the existing resume operation after about five
seconds by default. Disable automatic continuation in Resources & update to
persist the complete 30-second window. Manual hold takes priority; fault mode
never resumes automatically, and active resource transfers defer the request.
The MCU retains its full window for older ESP32 applications. The factory
bootloader and its power-on key gate are unchanged.
安装前 MCU 复核暂存镜像全量 MD5 和 NOR 尾区全 FF，再写 A1、回读并复位，
由原厂引导复制应用。引导备份和引导 MD5 不参与安装门控。早期已安装的 MCU
仍执行其原有检查，需满足旧条件才能迁移到此版本；新的 ESP32 或脚本不能取消旧
MCU 内部的检查。原厂引导的搬运范围见分析文档；兼容性提示保留在文档中。

配套 ESP 提供已鉴权的 MessagePack 命令及双遍备份脚本，见其
`BRY88BBA15G/SERVICES.md`。备份必须得到两次完整一致的 81920 字节文件；
该接口需要先运行社区 MCU，不能用来在首次刷入前凭空取得原厂引导区。

## 验证范围

主机测试覆盖窗口边界、计时回绕、hold/resume、强制维护、越界、CRC、旧会话／重放、
OTA 互斥和两遍完整读出；ESP 与真实 MCU C 解析器进行了串口联测。
编译后的 ARM 镜像在不映射 SDRAM 的 CPU 模型中通过完整／最小启动、WDT 复位进入维护、
继承 WDT 状态上报以及地址零起始的真实读指令和串口返回。
异常记录 ARM 指令测试覆盖基本帧、浮点扩展帧、越界/不完整帧、堆栈异常、PSP
和无效 xPSR；无效栈仍能记录恢复请求并复位。
振荡器精度、物理按键／显示、电源和物理断电恢复的结果应以实际烧录记录为准。

## English

The inspected factory application explicitly initializes, starts and feeds WDT,
but preserves its clock-source selection. Community startup records inherited
registers, safely reconfigures WDT, and provides a 30-second SRAM-only UART
maintenance window before display/sensor startup. Resume immediately ends the remaining normal startup window or releases a hold; active OTA and forced fault recovery reject it. The web UI exposes both controls under Screen → Resources & OTA. A separate recovery-only build
and read-only 80 KiB boot export are available. The recovery-only application has
booted on this board and produced two identical loader backups. Activation
checks the NOR application and erased copy tail; boot backups and boot-digest
allowlists are optional or removed. The paired new ESP32/tool can directly send
a complete application to stock firmware. An already installed older MCU still
enforces its own gates until replaced; the new tool cannot bypass those checks. Startup reports the actual
internal application MD5. Interrupted physical installation is not yet tested.
Normal application entry now clears the recovery latch: the previous version
marked healthy execution as a fault and could strand a retained-SRAM reset in
maintenance. Actual exception/request latches and WDT reset detection remain
enabled. The compiled-ARM regression tool reproduces the old behavior and
checks the corrected startup-window policy.
Diagnostic builds reserve 64 bytes below the unchanged internal stack. Core fault
recording does not depend on the interrupted stack. Telemetry v1 carries 13 LE32
values in 54 bytes; v2 preserves them and appends PC, stacked LR and xPSR in a
66-byte payload. Optional frame reads require a complete aligned internal MSP
frame, no stacking errors and a valid Thumb bit. Basic and extended FP frames are
supported; rejected frames leave the extra fields zero and still latch recovery
and reset. Both versions preserve request sequencing and OTA gates. The ESP
accepts v1/v2/v3; older firmware ignores an unknown diagnostic payload.
Version 3 appends the failed comparison's actual value, expected value, field
address and sensor checkpoint (20 LE32 values, 82 bytes). These values live in
internal SRAM; the actual value is the same volatile read used for comparison.
`diagnostic_max_version` reports the ESP parser's supported maximum.
The control guard reads ten callback/context pointers directly from SDRAM after
initialization and between business operations. Reason 6 includes failed memory
or control-pointer checks. Error codes 5–14 identify RTC read/write/context,
runtime send/factory/context/calibration_event and factory
runtime/maintenance/power, in that order. On failure, an initialized LCD and
backlight are stopped, the power latch stays asserted, and the internal-SRAM
UART maintenance service remains active without repairing the damaged values.
An optional `SCREEN_STARTUP_DIAGNOSTIC` build observes each peripheral stage
for five seconds; ordinary builds do not add those delays. Compiled-ARM tests
cover valid control state, each corrupted pointer and active-panel shutdown.

`SCREEN_UART2_QUIET=ON` is an optional diagnostic build for startup faults with
the PM2.5/TVOC module attached. It preserves UART2 pin/baud and power GPIO setup,
while blocking every UART2 read request, calibration transmission and receive
FIFO read. Calibration transmission fails and both channel samples remain
unreceived. The image contains `uart2=quiet;` and its manifest reports
`uart2_quiet: true`. The 30-second maintenance window, watchdog and UART OTA
remain active. This compares application traffic without electrically isolating
the pins or reducing module power; it is not a full sensor-operation image.

`SCREEN_BACKLIGHT_MAX_PERCENT` defaults to `100` and accepts an integer from
`0` to `100`. A `0` image initializes and runs LCD/SDRAM and business services
with zero backlight duty throughout startup, settings changes and touch wake.
The display stays dark. Stored brightness and unidentified module SET/power pins
are untouched. Limits below `100` add `backlight_max=N;` to the image identity;
the manifest records `backlight_max_percent`. UART OTA, the 30-second minimal
maintenance window, WDT and fault protection remain active. This diagnostic
compares backlight activation and is not a display-acceptance image.
Sensor checkpoints 1–20 cover entry, six UART pin-mux operations, three UART
starts, SHT SDA/SCL setup and release, PN4 direction/high, battery ADC pin,
ADC configuration/sequence/open, then SHT soft-reset return. Each checks all ten
control pointers. The checkpoint identifies the last completed operation and
does not by itself prove that operation caused delayed corruption. Actual linked
sensor GPIO/UART/ADC code is executed against modeled MMIO, including corruption
injected at every checkpoint before the SHT command.
