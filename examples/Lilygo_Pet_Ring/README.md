# LILYGO 芽伴 · Pet Ring

基于 T-QT-C6 的像素宠物固件。可以喂食、洗澡、玩接露珠游戏、睡觉和成长，也可以通过 ESP-NOW 与 T-Bao-S31 小车配对、互动和驾驶。

[English README](README_EN.md)

## 编译和烧录

在仓库根目录执行：

```bash
pio run -e Lilygo_Pet_Ring
pio run -e Lilygo_Pet_Ring -t upload
pio device monitor -b 115200
```

目标配置：ESP32-C6、4 MB Flash、USB CDC。当前默认屏幕为 GC9107，电池背板为 Battery V1.2。

Battery V1.2 识别到 SGM41562（ID `0x04`）后，固件会按 `Lvgl_CIT_SGM41562` demo 的配置开启充电。更换电池背板前，请检查 `libraries/common/pin_config.h`，不要把该配置用于其他 PMU。

## 宠物玩法

| 入口 | 用法 |
| --- | --- |
| 房间 | 点击或短划宠物进行抚摸；点击书架旁的宝仔进入小车联动 |
| `CARE` | `FEED` 喂食、`CLEAN` 洗澡、`PLAY` 接露珠、`SLEEP` 睡觉 |
| `INFO` | 查看状态、成长条件、照顾记录和纪念日 |
| `GROW` | 条件满足后点击 `EVOLVE`，成长需要手动确认 |
| 公园 | 点击 `RUN` 玩接露珠，点击 `LINK` 进入小车联动 |
| 设置 | 长按空白处进入，可调整亮度和 English / 中文 |

- 洗澡按 `SHOWER → SOAP → RUB → SHOWER → TOWEL` 顺序完成。
- 喂食完成三口才计为一次完整正餐。
- 轻轻倾斜指环，宠物会在房间地面移动。
- 连续点击宠物三次触发 `BOOP`，长按宠物触发秘密拥抱。
- 熄屏后的第一次触摸只负责唤醒，不会执行照顾操作。

## 与小车联动

### 进入联动页面

有两个入口：

1. 在房间点击书架旁的宝仔。
2. 进入公园后点击 `LINK`。

联动在 `COMPANION` 页面完成，不需要路由器或手机。

### 首次配对

1. 打开指环和 T-Bao-S31 小车，进入 `COMPANION` 页面。
2. 指环自动寻找小车，等待页面显示六位配对码。
3. 核对两端显示的配对码。
4. 两端各点击一次确认成为伙伴。
5. 配对信息保存在独立的 `petlink` NVS namespace，不会删除宠物存档。

两端必须使用兼容的 `PetLink v3`。如果显示 `UPDATE BOTH DEVICES`，请同步更新指环和小车固件。

### 开始和停止驾驶

1. 确认页面显示小车在线且没有故障。
2. 点击 `START`。
3. 点击开始时，系统会使用当前佩戴姿态作为本次驾驶的零点，等待页面完成 `SETTING CURRENT POSE` / `STARTING`。
4. 通过倾斜指环控制小车前进、后退和转向。
5. 点击 `STOP` 停车并撤销本次驾驶授权。

每次点击 `START` 都会重新取零。停止、退出页面、失联、IMU 数据持续异常或小车故障后，不会自动恢复行驶，必须再次点击 `START`。

如果显示 `INPUT PAUSED. KEEP STEADY.`，请保持指环稳定；可靠数据恢复后会继续当前驾驶，持续异常则自动停车。

### 双宠互动

点击右上角 `...` 打开互动菜单：

- `WAVE`：挥手。
- `RUN`：双方在屏幕上追逐。
- `HUG`：拥抱。
- `SECRET`：播放双角色对白。
- `FIVE`：击掌。
- `PET`：摸摸对方。

互动开始前如果正在驾驶，系统会先停车。互动只产生屏幕动画和对白；联动菜单中的 `RUN` 不会自动让小车行驶。完成同行陪玩后，可以计入一次 `PLAY`。

### 重新校准和解绑

- `... → RECALIBRATE`：停车后重新校准，不删除配对。
- `... → UNPAIR → UNPAIR`：确认解绑。在线时双方都会清除绑定；离线时可能需要在另一端再次解绑。
- 解绑不会删除宠物养成、语言设置或 Wi-Fi 配置。

### 测试安全

第一次测试请把小车轮子悬空，先确认停止、配对、取零和低速控制，再进行地面测试。小车没有防跌落、避障、精确里程或自动返航功能。

## 串口命令

串口速率为 `115200`，每条命令以换行结束：

```text
STATUS
SAVE
TIME <UTC Unix seconds>
HELP
```

`STATUS` 可查看 IMU、存档和联动状态。`TIME` 使用 UTC Unix 秒，不是日期字符串。

## 相关说明

- `PetLink` 协议和小车端同步说明：[`../../libraries/PetLink/README.md`](../../libraries/PetLink/README.md)
- 公共引脚和硬件版本：[`../../libraries/common/pin_config.h`](../../libraries/common/pin_config.h)
