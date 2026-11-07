# 本板原厂引导程序核对

两次通过社区最小维护程序、CRC 分块读出 `[0,0x14000)`，每份 81920 B。
SHA-256 均为 `ccb9d414186c5ea47823badf5083f7ab5b1b61b02c6a8d0510af6ee31bebdf09`，
MD5 为 `399e3c7bd32e8468dc6a109c7c8e0842`。入口 SP=`0x20006230`，Reset=`0x1bd`。
原件、分块日志、线性反汇编和实际 ARM 控制流跟踪位于 ESP 工程
`private/bry88-screen-flash-20261105/boot-backup/`。

## OTA 搬运

| 位置 | 本板引导程序的实际行为 |
| --- | --- |
| `0x548a..0x5490` | 从 NOR `0x7f0000` 读取安装标记 |
| `0x5562..0x556e` | 区分 A1 应用、A2 图库 |
| `0x5720..0x5750` | A1 擦除内部 `0x14000..0x7dfff`，每次 4096 B |
| `0x577c..0x57d0` | 从 NOR `0x780000+offset` 读取 256 B，写到内部 `0x14000+offset`；offset 终值为 `0x7e000` |
| `0x7ac`、`0x7f4` | 调用 ROM IAP 擦除／写入；没有检查 ROM 返回值 |
| `0x59a8..0x5a32` | 屏蔽中断，复位部分外设，停 SysTick，设置应用 VTOR／MSP 后跳转 |

**搬运循环长度是 `0x7e000`，不是应用长度 `0x67000`。**
其源区间为 `[0x780000,0x7fe000)`，发起的目标区间为
`[0x14000,0x92000)`。手册所述内部 Flash 为 512 KiB；其中 288 个写请求超出物理上界。
ARM 跟踪只记录 IAP 调用，不模拟 ROM 对越界地址的行为，不能把它当作断电安全证明。
不修改或重刷此引导程序。

内部配置 `[0x7e000,0x80000)` 不在擦除循环内，却对应于 NOR
`[0x7ea000,0x7ec000)` 的写入源。历史完整 NOR 备份中，应用以后的
`[0x7e7000,0x7fe000)` 全为 FF；本轮已知写入限定在应用暂存和安装标记块。

社区安装额外检查：重新计算应用 NOR MD5，逐块确认整个复制尾区为 FF，
通过后才允许写 A1 并回读。未知尾区不会被自动擦除。配置页没有被擦除，
FF 不会清除其中已有的位。引导 MD5／SHA-256 不再作为白名单，双读备份是可选操作。
配套新 ESP32 与脚本允许直接发送完整 MCU 应用，不要求原厂版本或恢复镜像。
已安装的旧 MCU 仍保留其原有检查，更新时需满足该旧版本的要求。

原厂串口安装依赖现有引导的复制协议，其他引导的搬运范围需要核对。
本板原件的上述越界写请求事实不变；此修改不改写引导区。应用重启后回报实际
内部应用 MD5；需要时仍可只读导出前 80 KiB。

## 复位时的 SRAM 初始化

引导的 `0x6f4` scatter loader 读取 `0x126b8..0x126d7` 的两条记录：
从 `0x126d8` 复制 128 B 到 `0x20000000`，随后清零
`[0x20000080,0x20006230)`。初始 MSP 为 `0x20006230`。普通应用的低地址
NOLOAD 段仍会被它覆盖；实机曾因此无法保持“复位后停留维护”的标记。

链接脚本将 16 B 保留区固定在 `0x2000bff0`，与引导初始化区及其栈分开；
应用 16 KiB 栈从 `0x2000c000` 开始。保留魔数及反码，正常升级／用户复位
显式清除它，运行故障保留它。`tools/check_boot_retention.py` 执行真实 scatter
代码，复现旧地址被清除并验证新地址未被写入；另外验证故障汇编不依赖栈。
该 CPU 检查仍须配合实际软件复位和再次 OTA 测试。

## WDT、保护与调试

首次最小维护启动回报的继承值为 CR=`0`、RSTVAL=`0`、CLKSEL=`0x00cc2804`、
RSTSR=`1`。它证明这次交接时 WDT 未运行，不能据此推断引导所有分支都没有 WDT。
原厂应用自身确实初始化、启动并喂 WDT，见 [WDT 说明](RECOVERY-WDT.md)。
社区始终显式接管 WDT，避免依赖引导时钟状态。

本板引导偏移 `0x1c` 为 `0`，未声明手册中 `0x43211234`／`0xabcd1234`
两种 SWD 加密关键字。读出的代码没有因此证明所有保护寄存器都开放，SWD 接线故障也未解决。
社区应用不设置这些关键字、不改保护寄存器、不复用 PB12/PB14 SWD 引脚。

## English

Two identical 80 KiB exports identify this board's bootloader. Its A1 erase loop
preserves the configuration pages, but its copy loop issues 516096 bytes of IAP
writes, exceeding both the application and the documented 512 KiB Flash range.
The ARM trace records calls; it does not emulate out-of-range ROM behavior.
Community activation checks the staged application and erased copy tail. Boot
backups are optional; the live boot MD5 and stock-version allowlists have been
removed. The paired new ESP32/tool can send a complete application directly to
stock firmware. Already installed older MCUs retain their previous gates until
updated. UART installation still depends on the existing factory copy contract.
Unknown tail contents are rejected without erasing them. Startup reports the
actual application digest. Read-only boot export remains available for explicit
checks and recovery. No bootloader, protection or SWD pin configuration is modified.
