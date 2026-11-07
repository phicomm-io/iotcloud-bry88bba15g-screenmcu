# BRY88BBA15G 实施与验证 — 2026-11-05

本文保留首次实现时的构建和验收记录。随后已经推进到实际 MCU 烧录、两遍引导
备份和 v3 安装门控；最新状态见 [烧录测试](FLASH-TEST-20261105.md) 与
[本板引导分析](BOOTLOADER-20261105.md)。下文“原厂 MCU 仍在运行／激活关闭”
描述的是初次实现快照，不代表后续设备状态。

## 代码与边界

屏幕为独立本地 Git 仓库 `iotcloud-bry88bba15g-screenmcu`。ESP32 位于相邻 `iotcloud-bry88bba15g-firmware`，基于 `rc4-policy/main` 的 `a41f4d5d2d8a9c2592b2ca64b02e0bfc212c3939`；公共前端 `phicomm-community-frontend` 基线 `a331560a75169a351dc4e7aa2640f2b6d6c23ce5`。没有提交或推送，已有修改保留。

社区网络复用 iotcloud/openiot HTTPD、鉴权、MessagePack、MQTT 和 LAN 模块。原厂 JSON、主题和文本仅留在内部 UART 兼容边界，不接入大金公共服务器。

## 已实现

| 模块 | 行为 |
| --- | --- |
| 启动 | 官方中断表、应用基址 0x14000，早期数据／UART 在内部 SRAM；PLL/SDRAM 有限等待、SDRAM 数据与地址别名测试；故障复位进入维护模式 |
| LCD | 原厂恢复的 207 个 9 位命令／数据字，RGB565 480×854、原厂 LCD/SDRAM 引脚与时序、PM10 PWM 背光 |
| 触摸／电源 | PD11 用户触摸键、PD13 网络按钮、熄屏首按只唤醒、长按和启动门控；ADC 电池采样／截尾均值／外电修正／原厂曲线、PM9 外电检测 |
| 采集 | SHT3x I²C、三组 UART；原始分母 65534，恢复的补偿与渐变链；CO₂/TVOC 校准，HCHO 缺失与失效区分 |
| 原厂业务 | 版本／时钟／快照、显示／勿扰／推送设置、配网和 OTA；hcho_install 按原厂仅应答，不伪造校准 |
| 布局 | 4×6 网格、最多 8 页／32 组件／每页 8 组件；SCN2 3120 B，UTF-8／边界／重叠检查、CRC／版本／原子分片提交 |
| 组件 | 时钟、数值、文字、折线、柱状、饼图、天气、提醒；外部数据最多 64 点并带 TTL |
| 数据 | 本次启动 1440 点分钟历史，缺测区间留空、失效显示 --；历史是分钟采样而非分钟平均 |
| 提醒 | 单次／每日／每周、API 推送、时区、32 条规则／通知、未读确认、勿扰和三种唤醒策略 |
| 天气 | 当前天气和三天预报，缺失字段与过期状态；ESP32 适配和风天气、心知天气、OpenWeather |
| 字体 | 16 像素 GB2312 常用中文完整集、24 像素界面子集、大数字字体 |

LVGL 无默认主题，显式设置背景不透明度、曲线和网格线宽。[实际 C UI 预览](../artifacts/preview/contact.png) 包含时钟、空气读数、趋势、天气、提醒和三种图表。公共管理页截图位于 `artifacts/browser/`。

## ESP32／WebUI／持久化

单 UART owner 保留原厂协议，能力协商后才发送扩展；每次启动／链路重置只发送一次 offer，使用芯片双时钟 PRNG 获取会话号，PRNG 不可用时保留原厂模式。重复 offer 不重置序号，拒绝旧会话和重放。当前能力 widgets、scene、services；未宣告 Flash 配置位读取。

公共设备设置页实现网格拖放、页面开关／标题、组件类型、数据源、坐标／大小／单位。轮询不覆盖草稿；状态区分别显示 ESP32 保存版本与 MCU 已应用版本。提醒支持编辑、禁用、星期选择、推送和已读。天气密钥不回传，仅报告 key_set，空输入保留原值。

HTTPS 校验证书，主机限制、禁止任意重定向，有界 gzip 解码／超时／内存门槛和独立查询任务。线程退出前释放 C++ 对象，过滤服务商无关字段，24 时段预报可在 12 KiB JSON 池解析。实机连续两次使用无效测试密钥进入请求失败路径，任务完成、原厂传感器数据持续、ESP32 无重启；随后禁用查询并清除测试密钥。服务商真实账号和密钥未提供，尚未验证成功天气响应。

原厂五分区和 NVS 保留。配置／MCU 镜像使用 ESP32 空闲 `0x700000..0x7fffff`，物理容量和完整分区表匹配后检查整区空白；未知内容不擦除。配置双 64 KiB 槽、载荷上限 48 KiB、CRC／提交后回读，掉电保持旧或新完整记录。

实机原厂引导头声明 4 MiB、物理 8 MiB。固定 IDF 4.4 库会据引导头裁剪 RAM 中的访问上界。初始化仅在物理容量确认为 8 MiB、原分区完全匹配时恢复这个 RAM 上界；不改引导头、ROM/cache 映射或分区表，拒绝暂存区时恢复原界限。依据见 ESP32 工程 `BRY88BBA15G/SERVICES.md`。

实机修复了两项配置问题：复用单 owner 的 SCN2 缓冲区，消除提交命令嵌套解析导致的 8 KiB 栈溢出；MessagePack 持久化使用显式长度的字节缓冲区，避免 ArduinoJson 的文本 String writer 在零字节处截断。加载按实际记录长度分配，状态可报告配置解码错误。布局、提醒和天气配置已分别验证重启恢复。

社区屏幕的设置在 RAM 中；社区 ESP32 会持久化并在新会话恢复。原厂 ESP32＋社区屏幕组合尚未实测，独立屏幕的设置持久化和全部原厂云业务不完整。

## UART OTA 与安装门槛

维护接收器独立于显示，原始二进制不进入文本业务解析。生产 ESP32 owner 与生产 MCU 接收器已模拟两次完整 421888 B 传输，流程为 bin_summary → start_ota 1 20 → download_bin 1024 → >／+bin → end_ota，校验偏移、超时、向量、串流 MD5 和 NOR 全量回读 MD5。

| 区域 | 地址 |
| --- | --- |
| 原厂内部引导，不覆盖 | 0x00000..0x13fff |
| 内部应用 | 0x14000..0x7afff，最大 0x67000 B |
| 原厂内部配置，不覆盖 | 0x7e000 |
| NOR MCU 暂存 | 0x780000..0x7e7fff，13×32 KiB |
| NOR 原厂安装标记，不写 A1 | 0x7f0000 |

原厂前 80 KiB 引导程序未取得；分析的 OTA 应用 SV.04.00.04.00 与实机 SV.04.00.01.00 也有版本差异。最终搬运、断电恢复和重入维护尚不能核对。因此 installable=false；screen.update.install 返回 bootloader_unverified，原厂 MCU 的 stage 请求也被拒绝，避免其自动提交。社区屏幕或维护模式仅允许暂存并验证。图库暂存与 MCU 窗口重叠，图库升级未实现。

首次真实 MCU 安装前，需取得该板引导程序或建立可靠的 SWD 恢复路径，再核对安装标记、应用复制和掉电恢复。当前没有社区 MCU 上板运行结果。

## 构建与离线验证

| 检查 | 结果 |
| --- | --- |
| GNU Arm 13.3.Rel1 交叉编译 | 通过：FLASH 412492／421888 B（97.77%），内部 SRAM 10472 B，SDRAM 1788112 B |
| ESP32 PlatformIO bry88bba15g | 通过，最终大小与 SHA 见交付 manifest |
| 屏幕 host | 7／7：core、widgets、update、sensors、services、maintenance、factory |
| ESP32 host | 6／6：bridge、screen_link、stock_frame、store、weather、ota_owner |
| ASan＋UBSan | 上述 7＋6 全部通过 |
| UART 升级 | 两次全尺寸传输；实际生成镜像亦经生产 owner／接收器；覆盖 NOR 损坏、错偏移、超时、取消 |
| 配置掉电 | 36 个中断点，恢复旧或新完整记录 |
| JS／C ABI | C 默认布局到 JS，JS 编辑到 C 校验；3120 B 与 CRC 一致 |
| WebUI 功能 | 新增 4 组、既有 DAIKIN 6 组通过 |
| Chromium | 实际压缩页面、MessagePack HTTP，中文／英文×390／1280 宽度；布局提交、草稿、每周提醒、天气配置、无 JS 异常和横向溢出 |
| 实机 Chromium | 真实 ESP32 页面及接口：完整 SCN2 保存、提醒／天气读取、密钥隐藏，390／1280 宽度无横向溢出或 JS 异常 |

MCU 应用补齐至 421888 B；SHA-256 `cbd2529ad2010d21a95e084ba7c5014e8aa8ec10e8911511ec309a2a27c06939`，MD5 `2f9b2031e5c1e2bbe92c312db95890bb`。bin／ELF／map／源文件摘要／校验清单在 ESP32 工程 `private/bry88-screen-implementation-20261105/screen/`。

## 实机与待完成

ESP32 已通过应用 OTA 启动新版，原厂 MCU SV.04.00.01.00 继续上报温湿度、CO₂、PM2.5；Flash 空闲区域识别正常。MessagePack 接口验证通过：分片布局保存／重启恢复、禁用的每周提醒规则恢复／删除、API 通知／已读、MCU 镜像全量上传及哈希回读、拒绝短镜像和原厂 MCU 激活。最终配置／上传／重启断言见 ESP32 工程 `private/bry88-screen-implementation-20261105/live-services.json`，浏览器与天气失败路径另有独立记录。

待完成：原厂完整错误／预热链、PM9/PM15 电量平滑、独立屏幕设置持久化、四种原厂／社区混刷组合硬件验收、BLUFI、图库升级、可选 Flash 配置位诊断。HCHO 单位未交叉复核。ETA6964 保持硬件充电行为，未找到本 MCU 对其 I²C 配置的证据。后续启动维护、WDT 与 80 KiB 只读导出见 [恢复说明](RECOVERY-WDT.md)；增加字体与维护代码后完整镜像占用约 414 KiB/422 KiB，继续扩展须检查 map。

TVOC 后续核对：UART2 请求与浓度字段匹配 Cubic AM1002 协议的 ppb 输出，
`ppb=BE16(frame[3:5])`，原厂 MCU 限幅 2000。ESP32 按用户要求显示 ppb；
屏幕 MCU 使用已恢复的原厂滞回等级，商业模块料号仍待实物确认。
