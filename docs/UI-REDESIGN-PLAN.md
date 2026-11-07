# BRY88BBA15G 屏幕 UI 与休眠策略需求

更新：2026-11-07。本文记录当前设计要求。2026-11-06 交付独立 Web 原型；本次按用户确认，同步更新 LVGL 源码与 `/dakin_lvgl/` Web 预览的布局，再将确认的布局写入本文。未烧录设备、未下发布局或设置。

本次以现有 LVGL 的 480×854 画布和 4×6 逻辑网格为基础，缩小边距、扩大组件区域，并将底部页点改为顶边铺满的半透明分段横条。LVGL 的几何参数集中在 `include/ui_layout.h`，预览集中在 `simulator/ui-preview/geometry.js`，规则测试检查二者一致。已有组件位置和尺寸无需迁移。

固件和公共管理 WebUI 已有功能见 [UI、动态亮度与可选资源](UI-AND-RESOURCES.md)，协议见 [screen-link](screen-link.md)。本文同时保留独立原型的演示范围；生产协议最多八项每周计划，原型最多 32 项。本次同步的 Web 页面为 `/dakin_lvgl/` 独立预览，ESP32 公共管理页不在本次修改范围。

## 设计要求

- 实际屏幕约 3.5 × 6.5 cm，原生画布 480 × 854 竖屏。简约、可读、直观；优先保证在实际尺寸下能读清，而非塞入更多信息。
- 用户选定深底高对比、大时钟首页。去掉品牌口号、技术型号、操作说明、数据来源、更新秒数等常驻文字；指标名、单位与必要异常提示保留。
- 时钟 144 px；Web 数值组件以 96 px 读数、34 px 标题、24 px 单位为基础，随组件尺寸同比例调整。LVGL 延续现有按字宽和组件宽高适配字体的方式。保留完整的正负号、小数和大数值。其他必要标签、单位、图例通常为 28–32 px；页面左右与底部边距缩为 16 px，组件间距 8 px。空间不足时优先调整布局或拆页。
- 首页显示时间、短日期和 CO₂、PM2.5、温度、湿度。顶部标题／状态区高 40 px，距顶边 16 px，与组件区域相隔 10 px；第一行含时钟时省略重复页面标题。长标题单行截断，不挤占状态图标。
- 换页标识采用**屏幕顶边铺满的半透明分段横条，可选 6、12、20 px，默认 12 px**，不占用底部组件空间；较粗时标题下移，组件网格不变。只为已启用页面等分，按翻页顺序排列；当前页使用主题强调色、90% 不透明度，其他段使用主题次要文字色、20% 不透明度。相邻段无外边距或缝隙，切换主题、显示壁纸或进入指定休眠页时同步变化。仅一页时整条为当前页；新增、删除、禁用页面后重新等分。横条是位置提示，设备翻页仍沿用原操作。
- 状态颜色与文字、符号同时使用，不仅靠颜色判断；缺测、异常与过期提示继续保留。
- 保留时钟、空气详情、文字、折线、柱状、环形、天气、提醒、外部组件。甲醛有效值与单位未确认，保持 `--`；TVOC 沿用优／良／差等级。
- 天气突出温度和图标，提醒突出标题与正文，长内容按固定行数截断。缺测显示 `--`；过期数据置灰并标注“已过期”。

## 小组件与组合页面

- 用户追加要求：各功能作为独立小组件，可以在同一页面组合。例如时钟＋天气＋空气读数＋提醒，或时钟＋堆叠图。
- 时钟、数值、文字、图表、天气、提醒六类组件共用 4 × 6 网格。现有六个示例页面也由组件组成，可编辑；支持新建空白页面、命名、删除、移除组件和撤销最近 20 次布局调整。
- 沿用工程的容量边界：最多 8 页、总计 32 个组件、每页不超过 8 个；可实际放置的数量同时受最小尺寸约束。
- 网格内容区原生像素为 `x=16, y=66, w=448, h=772`，间距 8 px；横／纵步距分别为 114／130 px。组件矩形为 `x=16+114×列, y=66+130×行, w=114×列跨度−8, h=130×行跨度−8`，满区域到 `y=838` 为止。相对原 416×708 区域，显示面积增加约 17.4%。2×2 卡片由 202×228 扩大到 220×252；4×2 图表为 448×252，4×1 数值为 448×122。
- 文字、天气、提醒最小 2 列 × 2 行；时钟默认 4 列 × 2 行、最小 2 列 × 2 行。数值组件默认 2 列 × 2 行，整行数值可缩到 4 列 × 1 行；图表默认、最小为 4 列 × 2 行。禁止重叠和越界。逻辑行列及原有布局数据保持兼容，拖动吸附、缩放预览与编辑网格统一采用新步距。
- 数值组件按形状排版：方形标题与单位同一行、标题稍大、单位稍小，下方突出大读数；2 × 2、3 × 3、4 × 4 方形之间按可用宽高等比放大或缩小字号、间距。满宽且较矮的组件（4 列 × 1–3 行）左上显示标题、标题下方显示单位，右侧显示大数字。拖动调整尺寸时即时预览排版变化，松开后检查冲突并保存。
- 数值与图表提供常用尺寸按钮，可一次调整宽高；数值包括方形、整行，图表包括 2、4、6 行。原有尺寸选择器仍可精确调整。
- 宽时钟保持 144 px，半宽时钟使用 72 px；其他组件随可用空间选择摘要或详情。短组件显示当前天气／最近提醒，整页展示更多天气／提醒内容；长文本按尺寸截断。
- 每个数值组件独立选择数据与标题；预览提供 CO₂、PM2.5、温度、湿度、TVOC、甲醛和外部功率。每个图表独立选择类型和时间范围，文字组件可编辑内容。后续固件应保持内置／外部数据绑定能力，预览只使用固定样例。
- Web 外围编辑器提供点选、拖动、右下角调整尺寸，也可通过起始行列、宽高选择器或方向键调整。新增失败、重叠与越界须提示并保留原布局。这里的拖动属于网页编排操作，不假设屏幕支持坐标触摸。
- 组合页可被无操作休眠或定时计划选为休眠页面。删除被引用页面时，引用退回“保持当前页面”；撤销删除恢复页面，不擅自恢复休眠引用。休眠设置仍须单独点击保存。
- 独立原型的页面和组件布局自动保存在此浏览器，刷新保留；支持本地记录损坏或存储被禁用时恢复默认并提示。设备端的保存、下发与恢复以现有生产实现和协议为准，本次不下发。

## 主题、组件配色与壁纸

- 用户追加要求：主题色可以整组替换；组件可定义背景及各组文字颜色，默认跟随主题；组件在壁纸上以半透明背景和边缘显示。
- 提供森绿、深墨、海蓝、暖杏、紫雾、纸白六组完整主题。每组同时定义屏幕背景、组件背景、标题、数值、单位／次要文字、正文／图例、强调、分隔线、三种图表序列与异常色；可展开编辑整组颜色或还原。
- 切换主题时，整组默认色及图表序列一起替换，并清除上一组的全局自定义颜色。组件已经单独指定的颜色仍保留；未指定的颜色立即继承新主题。
- 各组件按实际内容提供背景、标题、数值、单位／次要文字、正文／图例颜色选项。例如时钟分别控制时间和日期，文字组件控制标题和正文。支持逐项恢复跟随主题，或一次恢复该组件全部配色及透明度；组件配色也支持撤销与刷新保留。
- 异常数值继续使用主题异常色，过期数据继续置灰并保留“已过期”，避免自定义普通数值颜色掩盖状态。
- 默认纯色背景；可选择暮山、极光、沙丘三张随原型提供的壁纸，或上传 5 MB 以内的 PNG、JPEG、WebP。上传图片居中裁切为 480 × 854 并压缩为 WebP，仅保存在当前浏览器，不上传到服务器。
- 选择壁纸后，各组件默认使用主题背景色的 80% 不透明度，圆角与边缘以半透明细线叠在壁纸上。全局可调整组件背景不透明度（0–100%）和壁纸遮罩（0–70%）；每个组件也可单独覆盖不透明度或恢复跟随页面。透明度只影响组件背景和边缘，文字保持不透明。
- 没有壁纸时延续纯色排版：紧凑卡片有底色，时钟和高组件默认背景透明；显式定义组件背景后使用该背景。既有本地布局无需迁移，缺少配色项即继承当前主题。
- 独立原型的主题和壁纸自动本地保存，存储被禁用、空间不足或本地数据损坏须提示并允许继续预览。重置演示同时恢复默认主题和纯色背景。LVGL 沿用现有主题、组件颜色和壁纸资源处理，本次新增的边缘横条跟随全局强调色／次要文字色，不受组件独立配色覆盖。

## 图表要求

- 用户要求同时支持多曲线叠放和数值累加堆叠；预览包含折线、多曲线、双单位曲线、柱状、堆叠柱状、堆叠面积和环形。
- 多曲线默认两条、最多三条。同单位共轴；不同单位最多左右两轴，分别标明单位。双单位示例用实线／虚线与颜色区分。
- 累加堆叠仅用于同单位、可相加的数据；预览用照明、空调、电器功率 W，柱高／面积顶边等于总功率。不将 CO₂、温度等不同指标直接累加。
- 页面突出图表，简化刻度，图例保留名称、当前值与单位；支持 1h／6h／24h。折线缺失时段断开，不补零或连线。
- 图表类型、时间范围和数据状态通过预览外围设置切换，不假设设备支持直接触摸图表。
- 新增 2 行 × 4 列的紧凑图表：左侧 1 列显示标题、当前数值或总量、单位、时间范围，右侧 3 列显示图形与精简图例。大数值可用 `k` 简写，完整曲线数据保留；缺测显示 `--` 与“暂无数据”。
- 紧凑图表保留首尾时间刻度和最小／最大纵轴刻度；多序列保留名称和颜色，双单位图例分别标注单位。4 行及整页图表恢复较多刻度、当前数值与完整图例，避免把整页内容直接缩小塞入两行。

## 休眠策略要求

### 无操作休眠

- 外接电源与电池分别设置等待时间；支持永不、常用时长和 1–1440 分钟自定义。
- 休眠行为可选完全熄屏，或指定 1–100% 亮度并选择页面。可选任一已启用页面，或保持进入休眠前的当前页。
- “指定温度”已由用户澄清为“指定亮度”。用户另要求指定休眠显示页面。

### 多条计划

- 每条独立设置启用状态、名称、星期、起止时间、休眠行为、亮度与页面。
- 支持跨午夜；星期指开始当天。例如仅选周五 23:00–07:00，周六 06:00 仍在计划内。
- 区间包含开始、不包含结束。起止相同视为无效；启用计划须至少选择一天。
- 多计划重叠时列表靠前者优先，可上移／下移；计划的显示配置覆盖无操作休眠的显示配置。
- 默认演示：每天 23:00–07:00，8% 亮度、时钟页；另提供默认关闭的工作日午休示例。原型最多 32 条计划，属于演示上限，不是现有协议能力。

### 临时唤醒与恢复

- 用户选定临时唤醒：首触只恢复正常亮度与休眠前页面，不同时翻页；之后再次操作才翻页。
- 计划内唤醒后，无操作达到当前供电模式的等待时间，再回到计划休眠；等待设为永不时，临时唤醒保持 1 分钟。
- 临时唤醒仅适用于当前计划的当前一次发生；新计划或下一次发生重新生效。
- 计划结束恢复正常显示；如果其他计划或无操作超时仍成立，则按对应规则显示。单纯时间结束不清除无操作计时。
- 休眠只改变显示，采集与通信持续。现有提醒唤醒／勿扰策略和 OTA 优先级须在未来固件实施时保留，原型不增加真实提醒或升级接口。
- 设备设置应持久化并在重启后恢复；正常亮度、休眠亮度、实际生效亮度须区别展示，尊重硬件上限。此部分沿用已记录需求与现有生产实现，当前原型不修改设备。

## Web 原型与边界

- 原型入口：`simulator/ui-preview/index.html`；Web 目录 `/var/www/html/dakin_lvgl` 链接至此目录，通过 `/dakin_lvgl/` 访问。资源使用相对路径，无 CDN、无新增构建依赖、无设备写入请求。
- 页面切换支持左侧导航、屏幕／触摸键单击、方向键；双击或 Home 回首页（默认时钟）。休眠时设备模拟操作首次仅唤醒。编辑模式中，屏幕用于选择和排列组件，下方圆键仍模拟设备触摸；左侧导航是设计审阅控件，可直接唤醒并选页。
- 预览提供正常、缺测、过期和大数值／长文本状态；外部组件支持数值、文字、图表。
- 时间为可手动调整的设备本地墙钟，标为 UTC+8，与浏览器所在时区无关。默认固定 2026-11-06 18:42，不自动走时；快进按钮用于测试休眠，不必等待。
- 休眠设置即时预览，点击保存后仅写入当前浏览器 localStorage。刷新保留已保存策略和自动保存的页面布局、主题、组件配色与壁纸；当前选页、时间、数据场景等演示环境在刷新后重置。无法使用本地存储时提示，预览可继续操作。“重置演示”恢复默认布局、主题和纯色背景，并将休眠策略恢复为待保存的默认草稿。
- 放大视图整幅等比缩放。实尺寸以 35 mm 宽度校准，原生比例对应约 62.3 mm 高；用户给出的 65 mm 为近似尺寸参照，避免拉伸像素。用实体尺校准 20 mm 标尺，可补偿浏览器缩放和显示器密度。
- 亮度变化是浏览器视觉模拟，非 PWM／亮度测量；可关闭模拟来检查低亮度页面布局。选择完全熄屏时始终隐藏显示内容。
- 主题和字号集中为 CSS 设计变量，页面以 HTML/SVG 绘制。此设计原型不声称由当前固件同一份 LVGL 代码渲染。原来的 `simulator/index.html` 与 `tools/preview.py` 保留。
- 组件摘要／详情、多序列数据、休眠页面与计划持久化的生产实现以对应文档为准；本次只同步布局与换页条，不变更现有 HTTP、串口、SCN2／SCN3 或外观协议。

## 验收

- 桌面、手机正常打开，无横向溢出、脚本错误、丢失资源。正常、缺测、过期、大值与长文本均能展示，必要字号不缩小。
- LVGL 实际渲染和 Web 预览都验证内容区 (16,66,448,772)、8 px 间距、底部最后一行不越界；1–8 页顶边横条等分铺满、当前页正确高亮，LVGL 跳过禁用页面。检查页面增删、首尾循环、主题强调色、壁纸半透明及指定休眠页，底部不再保留页点空间。
- 六页、七种图表、三个时间范围及三个外部组件可切换；保存、刷新与本地设置异常恢复可验证。
- 验证五种功能同页、时钟＋最小尺寸图表、独立数据绑定、尺寸和位置设置、拖动／调整尺寸／回到原点、方向键、撤销、页面上限、冲突保护、刷新保留、组合休眠页与删除引用回退。
- 验证数值标题／单位同排与大小层次、不同方形尺寸等比缩放、整行左右布局、1 行数值、拖动过程即时预览；不同绑定与四种数据状态均不得裁切读数。两行图表须验证七种图形、三个时间范围、双单位与堆叠总量、缺测及刷新恢复。
- 验证六组主题的背景、文字和图表一起替换；全局自定义与还原、组件各组颜色独立生效、邻近组件继承、切换主题时自定义颜色保留及逐项／全部恢复。验证三张壁纸、透明背景与边缘、文字不透明、全局与独立透明度、上传／删除／损坏图片、刷新保留及本地存储失败提示。
- 休眠覆盖外接／电池计时、永不、自定义分钟、指定亮度与页面、完全熄屏、首触唤醒／后续翻页、跨午夜、重叠优先级、计划结束与下次重新触发。
- 校准后的屏幕显示需用户结合实体尺和实际观看距离评估；Web 截图不替代实机亮度或物理可读性验收。
- Web 运行记录与截图见 `artifacts/ui-preview/`；本次 LVGL 对比图、主机测试及 ARM 编译记录见 `artifacts/ui-layout-20261107/`。规则测试 `tests/ui_preview_model.mjs`，浏览器场景 `tools/ui_preview_browser.cjs`，实际 LVGL 渲染测试 `tests/ui_render.c`。

## English

This standalone browser prototype redesigns the 480 × 854 display around a large clock, readable measurements, a dark high-contrast theme and minimal text. It retains existing content types and previews multiple lines, dual units, stacked bars and stacked areas. Additive charts use comparable external data, not mixed sensor units.

The 2026-11-07 layout update changes LVGL, the standalone `/dakin_lvgl/` preview and the ESP32 editor. It uses 16 px margins, 8 px gaps and a 448 × 772 widget region at (16,66), approximately 17.4% more area. A full-width translucent rail overlays the top edge, with selectable 6, 12 and 20 px heights (default 12 px): enabled pages receive equal segments, with the current segment in the theme accent at 90% opacity and the others in secondary text color at 20%. Titles move below thicker rails; widgets retain their coordinates. It reserves no footer space. Logical 4 × 6 positions and saved layouts remain compatible. Height is stored separately in APP1 after capability negotiation.

Clock, measurement, text, chart, weather and reminder widgets can be combined on a shared 4 × 6 grid. Existing examples are editable; new pages support drag, resize, per-widget data/chart settings, undo and automatic local persistence. Readable minimum sizes, collision checks and capacity limits protect the small display. Composed pages can also be selected for sleep.

Measurement widgets scale proportionally in square layouts. Full-width rows place the title and unit on the left and a large reading on the right; a single-row option is available. Two-row charts reserve one column for the title, reading, unit and range, with three columns for the plot and compact legend. Larger charts retain their detailed presentation.

Six complete themes replace background, text and chart palettes together. Components inherit their theme by default and may override their background, title, value, unit, body and background opacity independently. Individual or complete resets restore inheritance. Built-in wallpapers and local PNG/JPEG/WebP uploads sit behind translucent cards and edges; text remains opaque. Theme, component colors and compressed wallpaper images persist only in the browser. Switching themes preserves component overrides.

Sleep requirements cover separate idle timeouts for external/battery power, multiple weekly schedules including overnight intervals, configurable dimming or display-off behavior and a selected sleep page. The first touch temporarily restores the previous page and normal brightness. Schedules use list priority; temporary wake belongs to one occurrence. The standalone preview saves only in browser localStorage and makes no device writes. Production firmware and shared WebUI behavior are documented in [UI and resources](UI-AND-RESOURCES.md); production supports eight weekly plans, compared with the prototype's 32. Acquisition, networking, reminder/DND behavior, OTA priority and hardware brightness limits must be preserved.
