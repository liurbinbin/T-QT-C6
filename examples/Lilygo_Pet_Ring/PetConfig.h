#pragma once

#include <stdint.h>

// 引脚与 LCD 控制器沿用公共 pin_config.h，以下只控制本 sketch 的交互策略。
#ifndef PET_TOUCH_SWAP_XY
#define PET_TOUCH_SWAP_XY 0
#endif
#ifndef PET_TOUCH_FLIP_X
#define PET_TOUCH_FLIP_X 0
#endif
#ifndef PET_TOUCH_FLIP_Y
#define PET_TOUCH_FLIP_Y 0
#endif

// IMU 的 X 轴用于左右倾斜；若佩戴方向相反，将符号改为 -1。
#ifndef PET_IMU_HORIZONTAL_SIGN
#define PET_IMU_HORIZONTAL_SIGN 1
#endif
#ifndef PET_IMU_VERTICAL_SIGN
#define PET_IMU_VERTICAL_SIGN 1
#endif

// 默认采用安全的熄屏待机；触摸唤醒经过实际板验证后才启用 light sleep。
#ifndef PET_ENABLE_LIGHT_SLEEP
#define PET_ENABLE_LIGHT_SLEEP 0
#endif

// 默认按 T-QT-C6 Battery V1.2 的 SGM41562 参考 demo 初始化充电器并开启充电。
// 仅在识别到芯片 ID 0x04 且使用 Battery V1.2 时执行；其他 PMU/背板不会套用该寄存器序列。
#ifndef PET_CONFIGURE_KNOWN_SGM
#define PET_CONFIGURE_KNOWN_SGM 1
#endif

// 分母越小移动越快；输入单位 mg，横向为世界像素，纵向为地面深度。
constexpr int32_t kPetHorizontalMotionDivisor = 8000;
constexpr int32_t kPetDepthMotionDivisor = 18000;

constexpr unsigned long kPetScreenOffMs = 60000;
constexpr unsigned long kPetDimMs = 20000;
constexpr unsigned long kPetCheckpointMs = 300000;
constexpr unsigned long kPetFrameMs = 100;
