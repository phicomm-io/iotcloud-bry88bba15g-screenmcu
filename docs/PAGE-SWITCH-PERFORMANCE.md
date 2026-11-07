# 换页性能分析 / Page-switch performance

2026-11-07，在现有应用上完成 108 次真机采集。主要开销是重复的软件透明混合和
活动帧缓冲拷贝。优先预合成壁纸暗化、复用已有的字宽拷贝实现；随后验证完整双缓冲
交换。单纯增大 40 行绘制缓冲或缓存更多 LVGL 对象，不足以解决当前主要成本。

本轮只分析和临时调整外观做对照，没有更换应用。推荐方案的最终速度尚未实测。

## 设备与方法 / Device and method

- BRY88BBA15G / SWM34S，480×854 RGB565，MCU 应用 MD5
  `1012893aecc7368909de69aeb274126c`，保存布局 SCN3 rev 23。
- 时钟、空气数据、组合概览三页，各测真正换页与原页强制重绘，每项重复三次。
  四种外观加恢复后的原外观，共 90 次。原页重绘保留已有对象树。
- 恢复外观后，六个启用页各重复换页三次，再采集 18 次。
- 全部 108 次均为一轮 monitor、22 次 flush、409920 像素；每块 40 行，最后 14 行，
  每次 23 条完整记录，无丢失。同步 flush 计时包含实际拷贝。
- LCD 帧结束 IRQ 平均周期约 25.06 ms，寄存器推导值 25.032 ms，约 40 Hz。
  这是控制器中断测量，尚无外部 VSYNC 探头测量。

Below, each controlled row is the median of nine captures: three saved pages,
three repetitions per page. LVGL monitor excludes object construction before
the refresh handler. Capture elapsed includes that pre-refresh interval and
timer scheduling, but excludes the trailing 120 ms diagnostic quiet interval.
These measurements are not touch-to-photon latency.

## 绘制对照 / Rendering contrasts

| 外观设置 | 换页 LVGL / ms | 原页重绘 LVGL / ms | 换页拷贝 / ms | 换页诊断区间 / ms |
| --- | ---: | ---: | ---: | ---: |
| 原外观：壁纸 1、卡片 80%、暗化 24% | 681 | 665 | 140.931 | 726.325 |
| 仅取消全屏暗化 | 482 | 463 | 141.015 | 525.686 |
| 同时将卡片设为 100% 不透明 | 326 | 308 | 140.872 | 368.972 |
| 再改用纯色背景 | 283 | 264 | 141.138 | 327.068 |
| 恢复原外观 | 683 | 661 | 141.273 | 727.370 |

“纯色背景”仍绘制完整页面及文字、数值和图表，并非空白填充测试。取消壁纸还会
移除壁纸模式的卡片边缘，故最后一项差值不能全归为图片拷贝。

仅取消暗化就减少约 199 ms，约占原刷新时间的 29%。这是每次整屏重新绘制黑色
半透明遮罩的成本，位置在 `src/ui.c` 的壁纸 dim 对象。缓存已经暗化的 RGB565
壁纸可以保留显示效果，并将这项工作移到壁纸加载或暗化设置变化时。

卡片由 80% 改为 100% 又减少约 156 ms。该收益包含透明混合和 LVGL 对不透明对象
背后内容的剔除，不能只算作 alpha 运算。直接取消透明度会改变用户设置；保留效果
的方案需评估静态背景缓存或经过验证的硬件混合。背景缓存应按需保存少量页面，
并在主题、壁纸、几何、透明度变化时失效。八个页面全部缓存会超过现有 SDRAM 预算。

真正换页和原页重绘相差约 16–22 ms 的 monitor 时间，诊断区间相差约 40 ms。
monitor 不包括前面的对象创建，差值也可能含字形缓存变化，因此没有把这些差值
全部称为对象重建耗时。两者都需数百毫秒，支持优先减少像素工作。

## 拷贝路径 / Copy path

`platform/swm34s/application.c` 的 flush 逐行调用 libc `memcpy`。冻结的最终
ELF 中 `memcpy` 位于 `0x383bc`，内层是 `ldrb` / `strb` 字节循环。一帧
819840 字节约耗 141 ms，有效载荷约 5.8 MB/s；当前没有逐块等待 VSYNC。

同一应用已包含 LVGL 的 `lv_memcpy.isra.0`（`0x330ba`）。对齐相同时，它先处理
头部对齐，再按 32 位展开拷贝；对齐不同则安全退回字节路径。完整 480 像素行的
源、目标均为 4 字节对齐，是可使用字宽路径的常见情况。

| 实际镜像中的实现 | 对齐的 960 字节行：执行指令数 | 数据读取 |
| --- | ---: | --- |
| libc `memcpy` | 3846 | 960 次单字节 |
| LVGL `lv_memcpy` | 777 | 239 次 4 字节、4 次单字节 |

以上是执行同一 ELF 的 Unicorn 指令与访问计数，576 个组合覆盖零长度、边界长度、
0–3 字节源/目标对齐及完整 38400 字节绘制块，结果和哨兵均正确，没有越界访问。
模拟器没有 SDRAM 等待、LCD 总线争用或中断模型，不能把约 4.95 倍的指令数差异
当作真机提速。可先只替换 flush 内的拷贝调用，避免全局替换 libc，再实测该项。
即使完全消除当前拷贝，原外观也最多减少这约 141 ms，仍有约 540 ms 的 LVGL 工作。

## 趋势与复杂页面 / Charts and complex pages

原外观恢复后的三次换页中位数：

| 页面 | LVGL / ms | 拷贝 / ms | 诊断区间 / ms |
| --- | ---: | ---: | ---: |
| 1 时钟 | 684 | 141.493 | 726.656 |
| 2 空气数据 | 687 | 141.035 | 731.647 |
| 3 变化趋势 | 617 | 141.075 | 640.695 |
| 4 户外天气 | 663 | 140.907 | 703.760 |
| 6 空气质量，多曲线 | 722 | 141.302 | 757.841 |
| 7 组合概览 | 686 | 140.460 | 730.098 |

多曲线页面值得单独优化，但上述页面内容和几何不同，不能把两页差值全归于曲线。
代码中另外找到两个随使用时长和点数增长的成本，尚未做独立真机分段计时：

1. `prepare_plot` 为每个目标点遍历全部历史。三条曲线、256 点、1440 分钟时，
   最多约 110.6 万次历史访问。可改为按时间桶单次汇总，复杂度从
   `O(series × points × history)` 改为 `O(series × (history + points))`。
   必须保持缺测断线、分桶边界、最新点补齐与平均值语义。
2. `draw_plot` 对每个绘制分块重新遍历所有点，重复做浮点坐标换算。
   可在数据、尺寸或坐标范围变化时缓存像素坐标，并按实际 clip 区域剔除线段，
   保留线宽和抗锯齿边界。传感器补偿运算顺序不需要改变。

字体已全量校验并缓存到 SDRAM，绘制阶段读取 RAM；缩放字体已有 16 槽、144 KiB
LRU。没有发现每次换页重新从 SPI NOR 加载完整字库的路径。字体命中率和缩放耗时
尚未单独上报，不能据此排除复杂文本的成本，但不应先重复优化已经存在的整库缓存。

## 与原厂比较及实施顺序 / Stock comparison and implementation order

原厂 `SENSOR3_MCU SV.04.00.04.00` 的 `0x3de84` 配置两份完整帧缓冲，
`0x2dc10` 写 LCD 地址并请求帧边界装载，随后通知 LVGL。当前是 40 行分块拷入
活动扫描帧，所以会逐段显现。增大分块不会自动消除同样的整屏像素工作。

建议实施顺序：

1. 在壁纸生成/加载或 dim 变化时预合成暗化；复用现有 RGB565 缓存。按批处理，
   保持 UART/维护服务运行。逐像素对照 LVGL 的 RGB565 混合与舍入规则。
2. flush 使用经对齐验证的字宽拷贝。记录实际拷贝完成时间，回测局部区域、奇数宽度、
   首尾行与颜色，确认 UART、传感器、恢复服务无回退。
3. 完整双缓冲加一次帧边界交换，减少活动帧拷贝和逐段显示。现有链接 SDRAM
   4254880 字节 / 8 MiB；替换 40 行绘制缓冲后净增加约 763 KiB，容量可行。
   必须先验证 A/B 地址独立、阴影地址实际生效、局部刷新同步，以及有界锁存超时。
   LVGL 8.3 的 direct mode 含区域同步；full refresh 则每轮重画全屏，二者不能混用
   假设。原厂无限等待循环不适合直接移植。
4. 按分段数据优化曲线聚合、坐标缓存，再评估透明卡片缓存和 DMA2D 混合。

不能直接打开上游 SWM341 GPU 开关：当前 pinned 实现的图片分支先调用软件 blend、
再调用 DMA blend，会重复处理；半透明图片还可能得到错误颜色。等待 DMA 完成的
循环也无超时，普通半透明纯色填充尚未走硬件路径。需修正后分别测实际完成时间、
像素一致性、SDRAM 争用和错误回退。当前工程仍关闭 GPU。

The immediate candidates are precomposed wallpaper dimming and a scoped word
copy in flush. Full independent framebuffers can improve presentation, but
require bounded frame-boundary latching and correct dirty-area synchronization.
Chart aggregation/coordinate caching and DMA2D follow separate measurements.
No optimized firmware has been installed by this analysis run.

## 证据与恢复 / Evidence and restoration

固件仓库 `private/bry88-page-render-analysis-20261107/` 保存完整 108 次区域记录、
前后配置、每组健康快照、源文件校验、两种拷贝实现反汇编及 576 项执行结果；
汇总为 `summary.json`，说明为 `RESULTS.md`。

已恢复原壁纸、80% 卡片透明度、24% 暗化、原第 7 页。外观字段一致，修订号因
四次临时调整从 25 增至 29；布局字节、休眠策略均原样，亮度保持 54%。
MCU 会话 `1353381587` 未变，五项传感器仍有效，没有新增 SDRAM 或异常错误。
未重启、未安装应用、未读写引导区、未重写字库。
