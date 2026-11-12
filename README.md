# iotcloud · 大金第三代屏幕 MCU

BRY88BBA15G 的独立 SWM34SVET6-50 应用，480 × 854 RGB565，LVGL 8.3.11。
配套 ESP32 固件与免拆迁移教程见 [iotcloud](https://phicomm-io.github.io/guides/daikin-g3-guide.html)。

- 保留原厂 UART 业务和串口 OTA；社区扩展采用能力协商。本版使用 SCN3、APP1 和第三版维护诊断，配套 RC6 ESP32；无扩展声明时使用原厂协议。
- 九种组件：时钟、数值、文字、折线、柱状、饼图、天气、提醒、摄像头图片。支持最多八页、32 组件、原始值／等级、256 点本地曲线、主题、独立配色、壁纸与语言设置。
- 外部文字／JPEG 通知采用圆角卡片堆叠，最多 16 条消息及 4 张图片。支持黑屏临时唤醒、显示后定时关闭或单击确认；双击前触摸键按 0（原页面）→1→2→3→4→0 切换。
- MCU 主动请求 HTTP(S) MJPEG、解析 Basic／Digest、multipart／chunked，并通过 JPEG 硬件解码；ESP32 只透传网络字节。默认 UART 115200，恢复阶段可协商 230400、460800、921600 或自动选择。接口与图片格式见[媒体协议](https://phicomm-io.github.io/iotcloud-community-protocols/zh/daikin-g3-media/)。
- 触摸翻页、ESP32 控制换页、真实 PWM 亮度、休眠与每周计划、RTC、传感器、补偿、校准及电池状态。
- 上电先运行 SRAM 维护服务；ESP32 默认短时间后放行业务，可关闭自动放行。继承 WDT 的处理与传感器启停保留；OTA 时降亮度并关闭 AM1008L，结束后恢复。
- 完整中文字库、壁纸位于可选外部 NOR；限时校验后缓存到 SDRAM，资源错误降级为内置字体与纯色背景，继续提供通信及升级。
- 应用位于 `0x14000`，长度 421888 字节。安装检查应用、NOR 暂存、回读及复制尾区；不要求引导备份或引导 MD5 白名单，不修改原厂引导区。

[屏幕交互演示](https://phicomm-io.github.io/dakin_lvgl/) · [WebUI 演示](https://phicomm-io.github.io/demos/daikin-g3/) · [设计要求](docs/UI-REDESIGN-PLAN.md) · [串口恢复与 WDT](docs/RECOVERY-WDT.md) · [电源管理](docs/POWER-MANAGEMENT.md)

## 构建

依赖来源、版本和摘要见 `dependencies.lock.json`、`cjson.lock.json`、`platform/swm34s/sdk.lock.json`。SDK 使用芯片厂商原始 SWM341_Lib；代码与资源分别遵循 `LICENSE` 和 `resources`／字体目录中的第三方许可。

```sh
cmake -S . -B ~/.cache/phicomm/screen-host -DCMAKE_BUILD_TYPE=Release
cmake --build ~/.cache/phicomm/screen-host -j8
ctest --test-dir ~/.cache/phicomm/screen-host --output-on-failure

cmake -S . -B ~/.cache/phicomm/screen-arm \
  -DCMAKE_TOOLCHAIN_FILE=platform/swm34s/arm-none-eabi.cmake \
  -DSCREEN_BUILD_FIRMWARE=ON -DSCREEN_BUILD_PREVIEW=OFF \
  -DSWM_SDK_DIR=/absolute/path/to/SWM341_Lib
cmake --build ~/.cache/phicomm/screen-arm -j8
```

最小维护镜像可另建目录并加 `-DSCREEN_RECOVERY_ONLY=ON`。产物包含应用 `.bin`、ELF、map、Git 版本、清单和 SHA256SUMS；应用镜像不能作为整片 Flash 使用。
预览可通过 `python3 -m http.server --directory simulator/ui-preview 8080` 打开。

## English

Independent SWM34SVET6-50 application for the Daikin BRY88BBA15G, using LVGL 8.3.11 at 480 × 854. It retains stock UART business traffic and UART OTA, adds negotiated SCN3 layouts, APP1 appearance and v3 maintenance diagnostics, and pairs with the RC6 ESP32 firmware. Stock ESP32 operation uses the original UART protocol.

Nine widget types cover clocks, measurements, text, line/bar/pie charts, weather, reminders and camera images. Themes, wallpaper, language, touch/remote paging, brightness and weekly sleep plans are configurable through the shared WebUI. Fonts and wallpaper are optional NOR resources, checked and cached in SDRAM; failed resources fall back without blocking sensors, communication or OTA.

External text/JPEG notifications use rounded stacked cards, with 16 messages and four image slots. They can temporarily wake a dark screen and disappear after their visible duration or a confirming tap. Double tap the front touch key to cycle 0 (normal page), 1, 2, 3, 4, then 0. The MCU requests HTTP(S) MJPEG, handles Basic/Digest and multipart/chunked responses, and decodes JPEG in hardware; ESP32 relays socket bytes. UART defaults to 115200 and negotiates 230400, 460800, 921600 or automatic selection during recovery. See the [media protocol](https://phicomm-io.github.io/iotcloud-community-protocols/en/daikin-g3-media/) for API and image requirements.

The application starts with an SRAM maintenance service. The ESP32 normally resumes business after a short delay; automatic resume can be disabled. Updates preserve the factory bootloader and verify the staged application and reserved copy tail. Boot backups are optional; no boot MD5 allowlist gates activation. The padded application is 421888 bytes at `0x14000`, not a full-flash image. Build commands above produce Git identity, ELF, map, manifest and checksums. The SDK and dependencies are hash pinned. See the [English device guide](https://phicomm-io.github.io/guides/en/daikin-g3-guide.html).

## OTA 安装提示 / Installation note

新脚本与配套 ESP32 可将完整 MCU 应用直接发送给原厂 MCU，无需先安装恢复镜像；原厂版本和引导摘要不作为白名单。建议首次操作前保存已有备份并保持稳定供电。串口 OTA 仍依赖原厂引导的复制协议；未知引导的兼容性需自行核对，应用不会改写它。

The paired ESP32 and tool can send the complete MCU application directly to stock firmware, without a recovery-first step or stock-version/boot-digest allowlist. Keep an existing backup and stable power when possible. UART OTA still relies on the existing factory loader copy contract; check compatibility for other loaders. The application does not rewrite the boot region.

### URL notifications

`screen.push.send` can carry a transient HTTP(S) JPEG/MJPEG `url` with the same
resolution, frame limit and authentication options as a page camera. A repeated
queued URL renews the existing notice; `last_push_id` reports the canonical ID.
The first frame starts the visible countdown, with a 15-second visible loading
limit. A single source owner retains the saved page configuration and restores it
after the notification releases the network/JPEG/RGB arena. Notification frames
invalidate only their image rectangle; they do not rebuild the page or stack.
Capability bit 25 gates URL push, and status version 3 adds source ownership and
remaining duration. Queue depth stays 16 and uploaded image cache stays 4.
