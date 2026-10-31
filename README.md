# NRF_52811_EDP

nRF52811 + Web Bluetooth 的墨水屏显示方案。设备端跑 Zephyr RTOS，通过 BLE GATT 接收图片数据刷到 1.54 寸四色墨水屏；网页端为纯前端 Web BLE 上位机，无需安装 App，Chrome/Edge 打开即用。

```
浏览器(Web BLE) ──BLE GATT──> nRF52811 ──SPI──> GDEM0154F51H 1.54" 四色屏
                                  └──> 状态 LED(P0.11)
```

## 目录结构

| 路径 | 说明 |
|---|---|
| `edp_wx_bt_52811/` | Zephyr 固件源码（含自定义 board） |
| `edp_html/` | Web BLE 上位机（纯静态页面） |
| `edp_html/v1.5/` | 上位机旧版，保留备查 |

固件内部布局：

```
src/
  main.c                上电初始化与线程编排
  app_config.h          所有时间/阈值常量
  ble/app_ble_epd.c     BLE 广播、GATT 服务、连接管理
  protocol/epd_protocol.c  指令解析与块传输状态机
  epd/epd_154d.c        屏幕驱动（复用 bsides 驱动核心）
  led/                  LED 显示层 + 状态策略层
  power/                空闲检测与系统休眠
  battery/              ADC 采样与低电告警
boards/my_nrf52811/     自定义板级设备树与 Kconfig
```

## 硬件

MCU：nRF52811-QFAA（ARM Cortex-M4F，Flash 192 KB / **SRAM 仅 24 KB**）
屏幕：GoodDisplay GDEM0154F51H，1.54 寸，200×200，**四色**（黑/白/红/黄），SPI

### 引脚连接

| 功能 | nRF52811 | 备注 |
|---|---|---|
| SPI MOSI | P0.08 | 屏幕数据输入 |
| SPI CLK | P0.07 | |
| CS | P0.06 | active low |
| DC | P0.05 | active high |
| RST | P0.04 | **active low**（低电平复位） |
| BUSY | P0.03 | active low |
| BAT ADC | P0.02 | 电池电压采样 |
| Status LED | P0.11 | **高电平点亮** |

> **LED 极性是易错点。** 代码统一用 `gpio_pin_set_dt()`（逻辑电平接口），因此设备树必须声明 `GPIO_ACTIVE_HIGH`。若误写 `GPIO_ACTIVE_LOW`，Zephyr 会翻转物理电平，表现为「该亮的时候灭、该灭的时候亮」——整张行为表整体取反，极难一眼看出。详见 `boards/my_nrf52811/my_nrf52811.dts` 中的注释。

### SRAM 很紧张

nRF52811 只有 24 KB SRAM，当前占用约 **97.5%**。任何新增静态缓冲（尤其是 BLE TX buffer）前请先算内存预算。`prj.conf` 里的 `CONFIG_BT_BUF_ACL_TX_COUNT=3` 就是为此压低的值——副作用见「已知问题」。

## 编译与烧录

环境：**nRF Connect SDK v3.3.0**（内含 Zephyr v4.3.99），工具链随 NCS 安装。

```bash
cd edp_wx_bt_52811

# 首次/全量构建（-p always 表示清除缓存重来）
west build -b my_nrf52811 -p always .

# 增量构建
west build -b my_nrf52811 .

# 烧录（需接 J-Link）
west flash
```

产物在 `build/edp_wx_bt_52811/zephyr/`：

- `zephyr.hex` —— J-Link / nRF Programmer 直接烧
- `zephyr.bin` —— Bootloader / 量产烧录用

参考资源占用（会随代码变动）：`FLASH 140212 B (71.32%)`、`RAM 23960 B (97.49%)`。

自定义 board 位于项目的 `boards/` 目录，Zephyr 会自动把 `<app>/boards` 加入 board root，所以 `-b my_nrf52811` 能直接找到，**无需手动设置 `BOARD_ROOT`**。

### 调试日志

串口已禁用，日志走 **SEGGER RTT**（`prj.conf` 开了 `CONFIG_USE_SEGGER_RTT`）。用 J-Link RTT Viewer 或：

```bash
# RTT Telnet 方式
JLinkRTTClient
```

RTT 后端配置成了 `DROP` 模式：缓冲满时丢弃新日志而不是阻塞调用者。**这是刻意的** —— 日志若阻塞 BLE 回调线程，会导致 GATT 应答超时、链路被踢掉。

## Web 上位机使用

Web Bluetooth API 只在**安全上下文**可用，所以必须走 `https://` 或本机地址（`http://localhost`、`http://127.0.0.1`）。直接双击 `index.html` 用 `file://` 打开**不行**。

```bash
cd edp_html
python -m http.server 8000
# 浏览器打开 http://localhost:8000
```

然后在 Chrome / Edge（不支持 Firefox、Safari）点「连接」，选择 `NRF_EDP_xxxx`。

### 端侧参数

连接成功后设备会主动上报三次 RX 通知：

1. **14 字节配置帧**（见下方协议说明）
2. `FID=DONGSHAN` —— 驱动预设组
3. `mtu=244` —— 单块负载大小

页面据此自动选择「东山驱动」预设。1.54 寸四色屏对应：**驱动 `0x23`（1.54寸 四色 26P）→ 画布 200×200 → 颜色模式四色（黑白红黄）**。四色为 2bpp，一帧 = 200×200/4 = **10000 字节**。

> 烧录新固件后若搜不到设备，先到 Windows「蓝牙和设备」里**删掉旧的配对记录**再扫描。Windows 会按 MAC 缓存 GATT 数据库，固件改了属性布局后旧缓存会让服务发现失败。

## BLE 协议

### GATT 服务

| 角色 | UUID |
|---|---|
| Service | `62750001-d828-918d-fb46-b6c11c675aec` |
| RX（主机写入） | `62750002-d828-918d-fb46-b6c11c675aec` |
| TX（设备通知） | `62750003-d828-918d-fb46-b6c11c675aec` |

广播名运行时按身份地址动态生成：`NRF_EDP_XXXX`，`XXXX` 是 MAC 显示顺序的**后四位**（如 MAC `D5:B2:3A:66:16:42` → `NRF_EDP_1642`）。

> **128-bit UUID 在 Zephyr 里是小端存储**，需把 16 字节整体倒序写入。标准串 `62750001-d828-918d-fb46-b6c11c675aec` 的 BLE 字节数组应为 `ec 5a 67 1c c1 b6 46 fb 8d 91 28 d8 01 00 75 62`。中间 `b6 c1 1c 67` 段倒序是 `1c c1`（**不是 `c1 1c`**），写错会以 `...b61cc1675aec` 广播，网页端 `getPrimaryService` 直接抛 NotFound。详见 `src/ble/app_ble_epd.c` 顶部注释。

### 配置帧（连接后设备主动通知，14 字节）

```
索引  0     1     2     3     4     5     6     7     8     9    10    11    12    13
含义  ────────── 引脚配置 ──────────   驱动ID  保留  保留  ...   ...   ...   ...
示例  0x14  0x13  0x06  0x05  0x04  0x03  0x02  0x23  0xff  0xff  0x07  0x01  0x00  0xff
```

- `[0..6]` 引脚编号（依次 MOSI / CLK / CS / DC / RST / BUSY / BAT ADC）
- `[7]` 屏幕驱动 ID，`0x23` = GDEM0154F51H 1.54" 四色

### 指令

| 命令 | 码 | 说明 |
|---|---|---|
| SET_PINS | `0x00` | 引脚配置（当前实现忽略） |
| INIT | `0x01` | 初始化屏幕，并回送上述配置帧 |
| CLEAR | `0x02` | 全白清屏并刷新 |
| SEND_CMD | `0x03` | 透传屏幕命令 |
| SEND_DATA | `0x04` | 透传屏幕数据 |
| REFRESH | `0x05` | 触发刷新 |
| SLEEP | `0x06` | 屏幕进入休眠 |
| SET_TIME | `0x20` | 设置时间 |
| SET_WEEK_START | `0x21` | 设置每周起始日 |
| WRITE_IMG | `0x30` | 整帧写入（不分块） |
| WRITE_BLOCK | `0x31` | **分块写入（推荐）**，带 CRC16 校验 |
| QUERY_STATUS | `0x32` | 查询传输状态，返回缺失块 bitmap |
| RESET_TRANSFER | `0x33` | 中止当前传输会话 |

### 块传输流程（`0x31`）

```
[cmd 0x31][block_id:2][total:2][cfg:1][payload:N][crc16:2]
```

- `cfg` 高 4 位：`0x00` 首块 / `0xF0` 续块；低 4 位：`0x00` 彩色层 / `0x0F` 黑白层
- 单块 payload 上限 240 字节（`EPD_PROTOCOL_MAX_PAYLOAD`），最多 512 块
- 每块带 CRC16，**校验失败的块会被丢弃但会话继续**
- 主机传完后用 `0x32 QUERY_STATUS` 查询缺失块 bitmap（64 字节），补发缺失块
- 全部收齐后固件自动调用 `REFRESH` 刷屏

## LED 状态含义

| 场景 | LED |
|---|---|
| 上电 | 亮 **1 秒** → 熄灭 |
| 屏幕自检刷新完成 | 快闪 3 下（120ms/120ms）→ 熄灭 |
| 蓝牙连接成功 | **常亮** |
| 正在接收图片数据 | **慢闪**（500ms 翻转，约 1Hz） |
| 数据接收完成 | 恢复 **常亮**（蓝牙仍连接时） |
| 屏幕刷新完成 | 快闪 3 下 → **回到常亮**（蓝牙仍连接时） |
| 蓝牙断开 | **熄灭** |
| 电量低（未连接时） | 告警图案：3 短闪 + 1 长闪循环 |

常量集中在 `src/app_config.h`：`LED_BOOT_ONCE_ON_MS` / `LED_RX_BLINK_INTERVAL_MS` / `LED_FLASH_ON_MS` / `LED_FLASH_OFF_MS`。

状态机由 `src/led/led_policy.c`（纯函数，给定状态选模式）+ `src/led/app_led.c`（GPIO 输出与定时）两层构成，**策略层无硬件依赖，可单独跑单元测试**。

### 实现注意事项（踩过的坑）

LED 显示层原来只在「模式发生变化」时才排工作项写引脚，而常亮/熄灭是静态模式、不会自己续排，于是**电平会残留在旧状态**。修正点：

1. 事件到达时对静态模式**同步直接写 GPIO**，不等工作项排队；
2. 快闪/慢闪的延期重排（Q 个毫秒）会**覆盖**外界刚提交的即时更新，需在 handler 末尾重新取一次当前模式，若与进入时不同就改排 0ms；
3. 上电脉冲、快闪的收尾要**直接切到新模式电平**，不能「先写灭、下次再纠回来」。

## 电源管理

**当前为「常驻」策略，不会进入 System OFF。** 这块 PCB 没有唤醒按键，一旦进 System OFF 就再也叫不醒了，所以 `app_power_init()` 里明确禁用了深度休眠，只启动慢速广播（间隔 2 秒）。若后续硬件加了唤醒按键，可在此处放开。

其它行为：

- BLE 连接空闲超过 **30 秒**（`BLE_IDLE_DISCONNECT_MS`）自动断开，随即恢复广播
- 每 60 秒采样一次电池电压，低于阈值时 LED 走低电告警图案：**3 短闪（100ms/100ms）+ 1 长闪（500ms）** 循环
- 广播间隔有两档：慢速 2 秒（`BLE_SLOW_ADV_INTERVAL_MS`）、活跃 500 毫秒（`BLE_ACTIVE_ADV_INTERVAL_MS`）

**断开后必须主动恢复广播。** BLE 从机一旦建立连接，控制器会自动停止广播，`epd_disconnected()` 里如果不重新调 `bt_le_adv_start`，设备就永久不可见。`src/ble/app_ble_epd.c` 里用 `adv_restart_work`（延时 200ms、失败每 300ms 重试、最多 10 次）处理，覆盖正常断开、空闲超时断开、对端断链、连接失败四种情形。

## 已知问题

**连接早期出现 3 次 `notify failed len=7 err=-12`（-ENOMEM）。**

发生在 Web 端读完 246 字节 TX 特征、写入 `0x01` 之后，原因是 `CONFIG_BT_BUF_ACL_TX_COUNT=3` 的 TX buffer 池被瞬时占满。传图和刷新都能正常走完，暂不影响功能。**要修需要加大 buffer，但 SRAM 只剩 2.5%**，必须连内存预算一起评估，不要单独调这一个数。

## License

根目录 `LICENSE` = **Apache-2.0**，适用于 `edp_wx_bt_52811/` 下本项目自有编写的代码（这些源文件本身也带 `SPDX-License-Identifier: Apache-2.0` 头）。

**例外是第三方代码——它们的原始条款继续有效，不因本项目采用 Apache-2.0 而改变。** 完整归属与条款说明见 `NOTICE`，要点：

| 组件 | 许可 | 位置 |
|---|---|---|
| 本项目固件代码 | Apache-2.0 | `edp_wx_bt_52811/src/`（除下两项） |
| Bosch BMP581 驱动 | **BSD-3-Clause** | `src/APP/app_bmp581/bosch/` |
| Zephyr beacon 示例 | Apache-2.0 | `src/APP/app_beacon/` |
| Web 上位机 | **未知（待确认）** | `edp_html/` |

### ⚠️ 关于 `edp_html/` 的提醒

网页端源自社区「墨水屏日历」项目，仓库副本内**没有任何来自上游的版权或许可声明**（已全目录检索：无 LICENSE、无 SPDX 头、无版权注释）。因此它**不适用**根 LICENSE 的 Apache-2.0 条款，再分发或商用前请自行确认上游授权。若你是权利人，请开 Issue 联系，我会补齐归属或下架。

> 之所以没整体采用 MIT：固件源码已有 58 个文件带 Apache-2.0 的 SPDX 头，再标 MIT 会造成声明冲突；而 Bosch 驱动是 BSD-3-Clause，要求保留原条款。现状是自洽的最小改动方案。
