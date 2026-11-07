# 2026-11-06 维护锁存误判排查与恢复

当前策略说明：以下是 2026-11-06 的历史测试记录，所用版本保留引导摘要及备份门控。
后续应用已取消引导 MD5 白名单；新 ESP32／脚本取消强制备份、原厂版本白名单和
首装恢复镜像限制，原厂引导区仍不改写。见 [当前引导说明](BOOTLOADER-20261105.md)。

Current policy: this page records the earlier tests and their original gates.
The newer application removes the boot MD5 allowlist; its paired ESP32/tool
makes backup optional and permits complete applications directly on stock MCU
firmware. The existing boot region remains unchanged.

设备管理页报告没有传感器数据。连接时 ESP32 仍在线，屏幕 MCU 报告
`fault_recovery`，没有启动显示和传感器业务。连续 7 次采样中 MCU 会话不变，
UART 帧数持续增加，因此这段观察未发现反复复位。继承的 WDT CR、RSTVAL
均为 0，RSTSR 为 1，没有 WDT 复位标志。POR 位可能保留，不能由此确定最初
触发复位的原因。

ESP32 的 `diagnostics.get` 在安装前和安装后两次启动都报告字段 8 为 9。
该字段直接取 `esp_reset_reason()`；本工程固定 Arduino-ESP32/ESP-IDF 头文件
将 9 定义为 `ESP_RST_BROWNOUT`。这证实 ESP32 检测到过欠压，不能单凭 Web
状态确定是适配器、USB 线、板上稳压/负载，还是其他供电路径造成。

## 已复现的问题

旧版正常入口调用 `recovery_latch(true)`，将健康运行也标为故障。若下一次
复位保留了内部 SRAM，故障魔数和反码会使 MCU 永久停在维护模式；该模式按
设计不初始化传感器，而且不允许普通 resume 放行。维护上报不等于正常业务。

修复为正常入口清除锁存。异常处理和显式恢复请求继续设置锁存，硬件 WDT
复位继续进入强制维护；30 秒 UART 启动窗口、前台喂狗和安装门控均保留。
旧版记录不能区分该误判和真实异常，因此不能断言设备此前从未发生真实故障。

`tools/check_normal_reset.py` 在实际 ARM 指令模型中执行正常入口，再保留 SRAM
重新启动。旧版留下 `52454356/adbabca9` 并进入 mode 3；修复版留下 `0/0` 并
进入正常的 mode 1 启动窗口。外部 SDRAM 未映射，ROM/外设和摘要辅助函数建模；
这是复位策略回归证据，不是完整业务或物理复位验证。

## 镜像和安装前检查

| 项目 | 结果 |
| --- | --- |
| 旧版完整应用 SHA-256 | `91fc5f0c65ec1f2794d2efc358c5f3cb897eff22552f6cd5efab11a7b2e3a38f` |
| 修复版完整应用 SHA-256 | `6ef85a4b3de01dfbf2d5197c4f613969bfa11c20bf0690799bbd1fc74442cb13` |
| 修复版完整应用 MD5 | `8745f52a46e877350437f119db0be7cb` |
| 应用地址／长度 | `0x14000`／421888 字节，不是整片镜像 |
| 当前会话引导备份 | 两次完整 81920 字节，SHA-256 均为 `ccb9d414186c5ea47823badf5083f7ab5b1b61b02c6a8d0510af6ee31bebdf09` |
| 编译后 ARM 最小服务检查 | 完整、WDT 复位、有效故障锁存、损坏锁存、最小维护 5 种情况全部通过；均未映射 SDRAM，实际读指令和 UART 引导导出通过 |
| 引导保留区检查 | 已备份原厂引导的实际 scatter 初始化通过；栈和保留字不重叠 |

完整镜像同时包含本次已构建的 [RTC 驱动](RTC.md)。RTC 相关主机测试此前通过
12/12 Release 和 12/12 ASan/UBSan；板级 I²C 模型、独立 10002 个日历向量也
通过。它们不构成物理 RTC ACK 或断电保时证明。

冻结产物位于 `artifacts/rtc-20261106-recovery-fix/application/`；另保留同目录下
`recovery/` 最小维护镜像。安装通过 ESP32 的既有 UART OTA，先校验 ESP32
存储及 MCU 外部 NOR 回读，再核对引导摘要、当前会话完整导出和空白复制尾区。

## 实机安装结果与限制

通过当前会话的双遍引导备份、NOR 暂存回读和安装门控后，MCU 完成安装并回报
新的会话和正确内部 Flash MD5 `8745f52a46e877350437f119db0be7cb`，初始为
正常的 30 秒启动窗口。随后 ESP32 的 uptime 归零，MCU 也换了会话并报告
`fault_recovery`。正常业务验收未通过，五项传感器仍未收到数据。

窗口结束的预期时刻约为记录的 Unix 秒 `1793933590.88`；ESP32 uptime 推算
其重新启动约为 `1793933591.9`，相差约 1 秒。欠压发生在开始启用显示/外设的
时段，提示需要排查启动负载和供电路径，尚不能据此指定某个外设或元件故障。
随后 13 次、约 61 秒采样中，ESP32 uptime 从 165157 增至 226220 ms，MCU
会话不变，UART 帧数从 64 增至 90：当前是稳定维护，不是这段时间内持续复位。

因此本次只确认了镜像安装和串口维护服务，不能标记为整机恢复。修复版正常入口
不再设置故障锁存，仍出现强制维护需继续区分真实异常、显式恢复请求、SDRAM/
引导标志检查失败和供电问题；现有协议尚不报告异常 PC/详细原因。安装后的
ESP32 复位原因仍为欠压。随后在用户重新供电后继续安装诊断版，保留 WDT 和
欠压检测，以进一步定位异常。

RTC 代码已随该镜像安装，但硬件读写、离线恢复时间及断电保持均未通过实机验收。
保留独立的旧版、修复版和所有失败记录，避免将镜像校验成功当作业务成功。

## 重新供电后的诊断

用户确认 USB 供电、未接电池、没有可用的串口调试连接后，维护会话保持稳定。
为区分故障原因，增加内部 SRAM 的阶段/异常快照和只读 UART 诊断通知；ESP32
增加可选解析及 API 字段。独立维护协议和 OTA 操作码不变，未映射 SDRAM 的
5 种实际 ARM 启动/引导导出检查、真实引导 scatter 及无栈异常记录检查全部通过。
主机 MCU 测试 12/12、ESP 联测 7/7 通过。诊断完整镜像使用 FLASH 418336 B，
仍在 421888 B 应用范围内。

| 项目 | 值 |
| --- | --- |
| 诊断 MCU SHA-256 | `e5839d17e90bbbb594d0561ddca49eed47f333c01f52dbb1b4067315e71fe20f` |
| 诊断 MCU 内部 MD5 | `e12335c21e0431ef4aeea184df57c9c5` |
| 诊断 ESP32 SHA-256 | `87632a75b62a9638db70de8fe49d632280e20052943432952fd04c9fbd56bab6` |
| 双遍引导备份 | 两次 81920 B，均匹配已核对的 `ccb9d414…bebdf09` |
| 初始启动会话 | `669508375`，正常 30 秒维护窗口，内部 MD5 匹配 |
| 随后维护会话 | `3126297181`，reason 2，previous_stage 12，IPSR 3 |
| 故障寄存器 | CFSR `0x00000001`，HFSR `0x40000000`，LR `0xffffffb8`，MSP `0x2000fd98` |

该阶段已越过 SDRAM 自检、RTC 初始化、传感器初始化和面板初始化，进入传感器
轮询阶段后发生 HardFault。阶段只定位到调用范围，不包含异常 PC；RTC 初始化
返回不等于收到有效 RTC 时间。CFSR 的 MMARVALID/BFARVALID 未置位，不能使用
MMFAR/BFAR 的数值定位故障。ESP32 同时重新启动并报告新的欠压复位，因果关系
仍需供电对照和更精确的执行位置确认。不是 SDRAM 自检返回失败，也没有 WDT
复位标志；没有将故障归因于某个具体传感器或 RTC。

诊断镜像冻结在 `artifacts/diagnostic-20261106/`。随后用户接回原配电池，保留
USB 供电，使用完全相同的诊断镜像进行对照。初始会话 `975781965` 的内部 MD5
匹配，30 秒后 ESP32 uptime 再次归零并报告欠压，MCU 新会话 `1855003266`
记录 reason 2、previous_stage 11、IPSR 3、CFSR 1、HFSR `0x40000000`，
异常 LR/MSP 也相同。阶段 11 包含 RTC 轮询及随后原厂业务轮询，不能将其等同于
RTC 故障；这次对照证实接回电池仍复现。故障后串口维护继续稳定，原失败日志保留。

为取得异常 PC，增加 v2 可选诊断：只读取经过内部 SRAM 范围、对齐、完整帧、
堆栈异常位和 xPSR 检查的 MSP 异常帧。无效帧仍能完成恢复锁存和复位；七种
编译后 ARM 帧检查、普通复位策略和 ESP v1/v2 兼容联测通过。镜像冻结在
`artifacts/pc-diagnostic-20261106/`，完整应用 SHA-256 为
`e6ab17c4a417f3ee6e0fe4e64e2a0ce5391c2d83de18e8000eac086d4f481850`。
ESP 接收端已安装。首次安装请求因更换电源时设备离线，停在只读状态请求。随后
用户更换电源，设备恢复稳定维护；重新双遍核验引导区后，v2 MCU 安装成功，内部
MD5 为 `34fd88bee0fc808c25b7dfe93129b0a8`。30 秒窗口结束后再次出现 ESP 欠压
复位，MCU 新会话 `1086584146` 捕获 previous_stage 11、CFSR `0x10000`、
故障 PC `0xff78`、栈内 LR `0x2f0d7`。后者紧跟 RTC `registers` 的 `blx r5`；
预期回调为 `board_rtc_read` (`0x14a81`)，故障位置是引导区数据，符合回调指针
损坏的表现。不能据此将问题归因于 RTC 芯片本身。

编译后 ARM 业务执行模型已映射完整 SDRAM，运行真实运行时、RTC 策略、LVGL
渲染与刷新，外设操作用模型处理；所覆盖路径没有改写 RTC 回调。分阶段诊断版在外设
初始化后和业务调用之间直接检查十个控制指针，发现损坏后停止 LCD／背光并
保留串口维护。诊断构建每个外设阶段观察 5 秒，以确认最早损坏阶段。其真实 ARM
注入检查覆盖十种指针损坏、正常返回与显示开启时的停止路径。

## 分阶段诊断版的实机结果

用户更换电源恢复设备后，当前会话再次完成双遍 80 KiB 引导备份，并经 NOR
暂存回读、MD5 和安装门控安装分阶段诊断版。完整镜像 SHA-256 为
`f262e326db6a74bedd7a206a2db78d95fea2048d30ae560c749433be58511cab`，
内部 MD5 为 `ce31b7fd8e58a09552e85119a7c59c88`，新会话为 `2051289143`。
镜像冻结在 `artifacts/memory-guard-20261106/application/`。

该版本保留完整 RTC、传感器、显示和业务，并在各初始化阶段额外观察 5 秒。
实际 ARM 检查通过 12 种控制指针场景、保留区/异常帧、普通复位策略以及不映射
SDRAM 的 5 种维护启动/引导导出场景。

| 实机记录 | 结果 |
| --- | --- |
| Unix `1793939093.372` | 新会话进入正常 30 秒维护窗口 |
| Unix `1793939123.459` | MCU uptime 30133 ms，阶段 6；RTC 初始化已返回，十个控制指针检查通过 |
| Unix `1793939125.594` | ESP32 仍在线，最后诊断仍为阶段 6，未报 SDRAM/指针错误 |
| Unix `1793939127.747` 起 | HTTP 请求超时，此后未取得阶段 7–10 的上报 |
| Unix `1793939187.856` | ESP32 短暂响应，uptime 562 ms，复位原因为欠压；MCU 服务尚不可见 |

首次超时请求跨过预计的传感器初始化时刻；因此只能将失联范围缩小到 RTC 后的
观察窗口及传感器初始化附近，尚不能指定某条 GPIO 写入或某个传感器。此时显示
和背光初始化尚未得到确认，不能把本次掉线直接归因于点亮背光。没有捕获到指针
错误也不能证明后续 SDRAM 始终正常。需要串口日志或供电测量继续区分电源下跌、
负载变化和软件异常；目前仍未通过完整业务验收。

补充的 ARM 执行模型使用推进中的 RTC 日历和网络时间应答，覆盖实际 UART
授时解析、RTC 策略、日历写入、读回确认和后续授时检查。正常业务模拟运行
125 秒完成，RTC 读回状态正常，回调始终指向预期函数；实际执行首帧刷新后，
其余像素刷新在函数边界跳过，以便集中检查授时路径。该检查使用先前捕获故障
位置的 v2 镜像，未复现回调损坏，不覆盖电气故障或完整显示压力。

原始私有记录位于 ESP32 工作树的
`private/bry88-mcu-reboot-20261106/`，包含状态采样、双遍引导备份、CPU 检查和
UART 安装日志。引导备份不放入公开应用产物。

## 再次供电后的细分诊断与恢复

再次连接电源后，旧分阶段镜像取得了两组实际控制指针错误：会话 `1345589944`
在传感器初始化阶段报告错误 10（`runtime.context`），随后会话 `214930047`
在正常循环报告错误 8（`runtime.send`）。两次均保持串口故障维护。ESP32 也曾
多次报告欠压并重新启动，期间 MCU 会话可以保持不变；需要分别判断两个芯片的
复位，不能将 HTTP 暂时断开当作 MCU 一定复位。错误位置和阶段并不固定，尚未
定位 SDRAM 内容损坏的根因。

新增 v3 诊断在内部 SRAM 保存失败比较的实际值、预期值、字段地址，以及
20 个传感器初始化检查点。失败值取自比较时的同一次 volatile 读取，不修复或
覆盖损坏字段。完整镜像保留 RTC、原有采集和显示、30 秒启动维护窗口及 UART
OTA；未改变电源引脚、SDRAM 时序、保护位或引导程序。

| 验证项目 | 结果 |
| --- | --- |
| MCU 主机测试 | 12/12 通过 |
| ESP32 主机与 UART 联测 | 7/7 通过，含 v1/v2/v3 兼容及通知不改变待处理请求 |
| 实际 ARM 控制指针检查 | 12 种正常／损坏／关屏场景通过 |
| 实际 ARM 传感器初始化 | 正常路径及在 19 个检查点分别注入损坏，20 种场景通过 |
| 不映射 SDRAM 的最小服务 | 5 种启动／恢复场景和实际引导读出通过 |
| 保留 SRAM／异常帧 | 原厂 scatter 执行及 7 种异常帧检查通过 |

ESP32 接收端先安装并回报 `diagnostic_max_version=3`。随后在同一 MCU 会话
完成双遍 81920 字节引导备份，SHA-256 均为 `ccb9d414…bebdf09`。经 NOR
暂存回读及原有安装门控，细分诊断完整镜像安装成功：

| 项目 | 值 |
| --- | --- |
| MCU SHA-256 | `4db1b0d62085ab2945c15f25f8f38cf4c9b88fe819e43419bdaed31b0c536eec` |
| MCU 内部 MD5 | `b1cdb30858fbcdc7f56390cb9ee988ad` |
| MCU 新会话 | `2260169015` |
| ESP32 SHA-256 | `75cbd69cbaedab10166b07e3b3a5b539347e7f283393c9a651890610b2341abf` |
| MCU 冻结目录 | `artifacts/sensor-detail-20261106/application/` |

新会话依次通过 RTC、20 个传感器检查点、LVGL、面板和背光阶段，进入正常循环。
截至 MCU uptime 558643 ms，118 次新镜像状态采样跨越 553 秒，MCU 会话不变，
未出现指针、自检或异常寄存器错误，ESP32 uptime 单调增加。期间有短暂 HTTP
503，重试后继续取得相同 MCU 会话和递增 uptime。RTC 从无效时间变为 `READY`，
完成网络授时后的物理写入和读回验证；
离线断电保时尚未测试。温度、湿度和 CO₂ 持续更新，网页布局 revision 7 已应用。
PM2.5 和 TVOC 共用的 UART2 尚未产生有效样本，因此整机传感器验收仍未完成。
该通道的引脚、9600 波特率、请求、分帧、校验和转换与已验收的旧镜像相同；
没有用零值替代缺失数据，也没有因暂时稳定而宣称先前的损坏根因已修复。

本轮原始记录、失败重试、双遍备份、安装和持续采样位于
`private/bry88-mcu-reboot-20261106/sensor-stage-reconnected/`。当前保留完整
诊断镜像运行，物理显示／触摸和 UART2 通道状态分别验收。

## English

The MCU was responsive in forced maintenance, which intentionally leaves sensors
uninitialized. The observed session stayed stable and had no WDT reset flag.
The previous normal-entry path incorrectly set the retained fault latch; a later
reset preserving SRAM could therefore enter permanent maintenance. Normal entry
now clears it, while exception/request latches, WDT detection and installation
gates remain enabled. Compiled-ARM execution reproduces the old behavior and
checks the fix. This does not identify the original reset trigger or prove that
no genuine fault occurred earlier. The fixed image was installed and announced
its expected internal digest in the startup window. A subsequent ESP32 restart
reported `ESP_RST_BROWNOUT`, and the MCU returned to forced maintenance. Sensor
and RTC hardware acceptance failed or remains incomplete. Diagnostic installation
continued after the user restored power; watchdog and brownout detection remain
enabled throughout the investigation.

After power reconnection without the battery, the diagnostic image booted with
the expected internal digest, then captured a HardFault at the sensor-polling
stage (IPSR 3, CFSR 1, HFSR 0x40000000). SDRAM self-test and RTC initialization
had returned; this does not prove a successful physical RTC read. The ESP32 also
recorded a fresh brownout reset. These observations do not identify the precise
instruction or establish which event caused the other. A comparison with the
original battery reconnected used the exact same image and reproduced both a
fresh ESP32 brownout and an MCU HardFault, this time at stage 11 (RTC plus factory
business polling). Reconnecting the battery did not fix the fault. Telemetry v2
adds bounded internal exception-frame capture; its ESP receiver is installed.
An initial v2 install attempt stopped at its read-only status query while the
device was offline during supply changes. After the user restored stable power,
two matching boot backups were obtained and the v2 MCU image was installed with
the expected internal MD5. A fresh ESP brownout was recorded as business startup
began.
MCU session 1086584146 captured stage 11, CFSR 0x10000, PC 0xff78 and stacked LR
0x2f0d7. The LR follows the RTC read callback's indirect branch; the expected
callback is 0x14a81, while the fault PC lies in boot-region data. This is
consistent with a damaged callback, not evidence of a defective RTC device.
An ARM business model with mapped SDRAM did not reproduce callback corruption
in its covered paths. The staged diagnostic image checks ten control pointers at
each peripheral stage and between operations, and observes each startup stage
for five seconds. Corruption stops LCD/backlight and keeps UART recovery active.

That staged image was installed after another matching pair of current-session
80 KiB boot backups. Its SHA-256 is
`f262e326db6a74bedd7a206a2db78d95fea2048d30ae560c749433be58511cab`, and
session 2051289143 announced the expected internal MD5. RTC initialization and
the first control-pointer check returned at MCU uptime 30133 ms. HTTP was still
responsive two seconds later, then timed out around the expected transition to
sensor initialization. No later stage or pointer-error report was captured.
The ESP briefly returned with uptime 562 ms and a fresh brownout reset reason.
This narrows the outage interval to the post-RTC observation/sensor-startup
period; it does not identify a failing GPIO or sensor. Display/backlight startup
was not confirmed in this run. Serial or electrical evidence is needed before
another targeted change; complete business acceptance remains pending.

An additional run of the v2 ARM image completed 125 simulated seconds of normal
business with advancing RTC/network-time models. It exercised actual UART time
parsing, calendar write/readback and later synchronization checks without
changing the RTC callbacks. The first pixel refresh executed; subsequent pixel
refreshes were skipped at the function boundary. This checks the synchronization
path without modeling electrical faults or full display load.

After another power reconnection, the previous staged image reported two
different control-pointer failures: session 1345589944 stopped at sensor startup
with error 10 (`runtime.context`), while session 214930047 stopped in normal
business with error 8 (`runtime.send`). UART recovery stayed available. The ESP
also recorded repeated brownouts, sometimes without a change in MCU session.
The SDRAM corruption root cause remains unresolved.

Telemetry v3 records the exact failed volatile comparison, expected value,
address and sensor checkpoint in internal SRAM. The linked ARM checks passed
12 control-pointer cases and 20 peripheral-startup cases, including corruption
at each of 19 checkpoints. MCU host tests passed 12/12; ESP tests passed 7/7.
The five SRAM-only maintenance cases and boot-retention/exception-frame checks
also passed. The full image keeps RTC, sensors, display, the 30-second startup
window and UART OTA; no power-pin or SDRAM-timing experiment was made.

After installing the v3 ESP receiver and obtaining two matching current-session
80 KiB loader backups, the full diagnostic MCU image was installed through the
existing NOR verification and boot gates. Its SHA-256 is
`4db1b0d62085ab2945c15f25f8f38cf4c9b88fe819e43419bdaed31b0c536eec`;
session 2260169015 announced internal MD5 `b1cdb30858fbcdc7f56390cb9ee988ad`.
It passed RTC, all sensor-startup checkpoints, LVGL and panel startup. Through
MCU uptime 558643 ms, 118 new-image status samples over 553 seconds kept the
same MCU session and monotonically increasing ESP uptime, without reported
memory or exception errors. Brief HTTP 503 responses were followed by successful
retries without evidence of a reset. RTC reached READY after network-time write/readback, and
temperature, humidity and CO2 updated repeatedly. Layout revision 7 was applied.
PM2.5 and TVOC on UART2 still lacked valid samples; this run does not complete
sensor acceptance or establish a fix for the earlier memory corruption. UART2
configuration and acquisition code match the previously accepted image.
Power-off clock retention and current physical display/touch remain separate
checks. Raw records are in the `sensor-stage-reconnected` private evidence folder.
