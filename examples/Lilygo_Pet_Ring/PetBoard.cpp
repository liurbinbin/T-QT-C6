#include "PetBoard.h"

#include <esp_sleep.h>

namespace pet {
namespace {
constexpr uint8_t kTouchAddress = 0x15;
constexpr uint8_t kPmuAddress = 0x03;
volatile bool touchInterrupt = false;
struct BusGuard {
  SemaphoreHandle_t mutex;
  bool locked;
  explicit BusGuard(SemaphoreHandle_t value,uint32_t timeout=12) : mutex(value),
    locked(value && xSemaphoreTakeRecursive(value,pdMS_TO_TICKS(timeout))==pdTRUE) {}
  ~BusGuard() { if(locked) xSemaphoreGiveRecursive(mutex); }
};
void ARDUINO_ISR_ATTR touchIsr() { touchInterrupt = true; }
}  // namespace

bool PetBoard::read(uint8_t address, uint8_t reg, uint8_t* bytes, size_t count) {
  BusGuard guard(wireMutex_); if(!guard.locked) return false;
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 ||
      Wire.requestFrom(address, count) != count)
    return false;
  for (size_t i = 0; i < count; ++i) bytes[i] = Wire.read();
  return true;
}
bool PetBoard::write(uint8_t address, uint8_t reg, uint8_t value) {
  BusGuard guard(wireMutex_); if(!guard.locked) return false;
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool PetBoard::begin() {
  wireMutex_=xSemaphoreCreateRecursiveMutexStatic(&wireMutexStorage_);
  if(!wireMutex_) return false;
  pinMode(BATTERY_MEASUREMENT_CONTROL, OUTPUT);
  digitalWrite(BATTERY_MEASUREMENT_CONTROL, HIGH);
  pinMode(BATTERY_ADC_DATA, INPUT);
  analogReadResolution(12);
  pinMode(BREATHING_LIGHT, OUTPUT);
  ledcAttach(BREATHING_LIGHT, 20000, 8);
  ledcWrite(BREATHING_LIGHT, 255);
  pinMode(LCD_BL, OUTPUT);
  if (!ledcAttach(LCD_BL, 20000, 8)) return false;
  ledcWrite(LCD_BL, 255);

#if defined(T_QT_C6_Battery_V1_2)
  pinMode(LSM6DSL_EN, OUTPUT);
  digitalWrite(LSM6DSL_EN, HIGH);
#elif defined(T_QT_C6_Battery_V1_0_V1_1)
  pinMode(LSM6DSL_IIC_ADDRESS_MODE, OUTPUT);
  digitalWrite(LSM6DSL_IIC_ADDRESS_MODE, LOW);
#endif
  if (!Wire.begin(IIC_SDA, IIC_SCL, 400000)) return false;
  Wire.setTimeOut(20);
  iicBus_ = std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);
  delay(25);

  uint8_t id = 0;
  if (read(kPmuAddress, 0x0B, &id, 1)) {
    Serial.printf("PMU ID: 0x%02X\n", id);
    pmuRecognized_ = id == 0x04;
#if PET_CONFIGURE_KNOWN_SGM && defined(T_QT_C6_Battery_V1_2)
    if (id == 0x04) {
      // 与 Lvgl_CIT_SGM41562 使用的 SGM41562 demo 保持一致：
      // 关闭 PCB OTP、关闭 Battery V1.2 不具备的外部 NTC、关闭看门狗，
      // 开启充电，并解除输入电流限制。
      pmuConfigured_ = write(kPmuAddress, 0x07, 0xB7) &&
                       write(kPmuAddress, 0x06, 0x40) &&
                       write(kPmuAddress, 0x05, 0x1A) &&
                       write(kPmuAddress, 0x01, 0xA4) &&
                       write(kPmuAddress, 0x08, 0x40);

      // A4 的 bit3 必须为 0，确认充电没有仍被 CHG_CONFIG 禁用。
      uint8_t powerConfig = 0;
      pmuConfigured_ = pmuConfigured_ &&
                       read(kPmuAddress, 0x01, &powerConfig, 1) &&
                       (powerConfig & 0x08U) == 0U;
    }
#endif
  }
  if (!pmuConfigured_) {
#if PET_CONFIGURE_KNOWN_SGM
    Serial.println("PMU configuration unavailable or incomplete; verify board variant before battery use.");
#else
    Serial.println("PMU read-only: charging configuration is unchanged. Verify battery settings before battery use.");
#endif
  } else if (pmuRecognized_) {
    Serial.println("SGM41562 charging: ENABLED (Battery V1.2 reference configuration)");
  }

  // 与仓库 CST816T demo 一致：先让触摸进入稳定状态，再初始化 IMU。
  touchDevice_ = std::unique_ptr<Arduino_IIC>(
      new Arduino_CST816x(iicBus_, CST816T_DEVICE_ADDRESS,
                          TP_RST, TP_INT, touchIsr));
  touchReady_ = touchDevice_->begin(400000);
  if (touchReady_) {
    Serial.printf("CST816 ID: 0x%02X\n",
                  static_cast<unsigned>(touchDevice_->IIC_Device_ID()));
  } else {
    Serial.println("CST816 initialization failed");
  }

  // 再启动 IMU，游戏和房间巡游都可以使用；没有底板时自动退回触摸控制。
  imuReady_ = configureImu();
  Serial.printf("LSM6DSL motion: %s\n", imuReady_ ? "READY" : "NOT DETECTED");

  bus_ = new Arduino_HWSPI(LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, LCD_MISO);
  gfx_ = new T_QT_C6_LCD_CLASS(bus_, LCD_RST, 0, T_QT_C6_LCD_IPS,
                              LCD_WIDTH, LCD_HEIGHT,
                              T_QT_C6_LCD_COL_OFFSET_1, T_QT_C6_LCD_ROW_OFFSET_1,
                              T_QT_C6_LCD_COL_OFFSET_2, T_QT_C6_LCD_ROW_OFFSET_2);
  if (!gfx_ || !gfx_->begin()) return false;
  gfx_->fillScreen(0);
  brightness(65);
  samplePower();
  return true;
}

bool PetBoard::configureImu() {
  if (!iicBus_) return false;
  imuDevice_ = std::unique_ptr<Arduino_IIC>(
      new Arduino_LSM6DSL(iicBus_, LSM6DSL_DEVICE_ADDRESS,
                          DRIVEBUS_DEFAULT_VALUE, DRIVEBUS_DEFAULT_VALUE));
  if (!imuDevice_->begin(400000)) return false;

  const bool accelerationReady = imuDevice_->IIC_Write_Device_State(
      imuDevice_->Arduino_IIC_IMU::Device::IMU_ACCELERATION_POWER_MODE,
      imuDevice_->Arduino_IIC_IMU::Device_Mode::IMU_DEVICE_NORMAL_POWER);
  const bool accelerationRange = imuDevice_->IIC_Write_Device_Value(
      imuDevice_->Arduino_IIC_IMU::Device_Value::IMU_ACCELERATION_SENSITIVITY,
      16);
  const bool gyroReady = imuDevice_->IIC_Write_Device_State(
      imuDevice_->Arduino_IIC_IMU::Device::IMU_GYROSCOPE_POWER_MODE,
      imuDevice_->Arduino_IIC_IMU::Device_Mode::IMU_DEVICE_NORMAL_POWER);
  const bool gyroRange = imuDevice_->IIC_Write_Device_Value(
      imuDevice_->Arduino_IIC_IMU::Device_Value::IMU_GYROSCOPE_SENSITIVITY,
      2000);
  if (accelerationReady && accelerationRange && gyroReady && gyroRange &&
      imuDevice_->IIC_Device_ID() == 0x6A) {
    uint8_t control[3]{};
    if (!read(LSM6DSL_DEVICE_ADDRESS, LSM6DSL_RD_WR_CTRL1_XL, control, sizeof(control)) ||
        (control[0] & 0x0CU) != 0x04U ||
        (control[1] & 0x0EU) != 0x0CU ||
        (control[2] & 0x44U) != 0x44U)
      return false;
    Serial.printf("LSM6DSL motion: READY address=0x%02X accel=mg gyro=mdps burst=12\n",
                  LSM6DSL_DEVICE_ADDRESS);
    return true;
  }
  return false;
}

void PetBoard::setImuEnabled(bool enabled) {
  if (!imuReady_ || !imuDevice_) return;
  BusGuard guard(wireMutex_,70); if(!guard.locked) return;
  const auto mode = enabled
      ? imuDevice_->Arduino_IIC_IMU::Device_Mode::IMU_DEVICE_NORMAL_POWER
      : imuDevice_->Arduino_IIC_IMU::Device_Mode::IMU_DEVICE_OFF_POWER;
  imuDevice_->IIC_Write_Device_State(
      imuDevice_->Arduino_IIC_IMU::Device::IMU_ACCELERATION_POWER_MODE, mode);
  imuDevice_->IIC_Write_Device_State(
      imuDevice_->Arduino_IIC_IMU::Device::IMU_GYROSCOPE_POWER_MODE, mode);
}

bool PetBoard::motion(Motion& sample) {
  if (!imuReady_ || !imuDevice_) return false;
  BusGuard guard(wireMutex_); if(!guard.locked) return false;
  const uint32_t now = millis();
  if (now - lastMotionRead_ < 25) return false;
  lastMotionRead_ = now;

  // 单轴 API 各自检查 STATUS_REG，先读 X 后 Y/Z 可能只得到 -1。
  // 状态只检查一次，然后在同一事务读取六轴；失败时不发布半组样本。
  uint8_t status = 0;
  if (!read(LSM6DSL_DEVICE_ADDRESS, LSM6DSL_RD_STATUS_REG, &status, 1) ||
      (status & 0x03U) != 0x03U)
    return false;
  uint8_t bytes[12]{};
  if (!read(LSM6DSL_DEVICE_ADDRESS, LSM6DSL_RD_OUTX_L_G, bytes, sizeof(bytes)))
    return false;
  sample = decodeImuSample(bytes, now);
  return true;
}

Touch PetBoard::touch() {
  const uint32_t now = millis();
  const bool interrupted = touchInterrupt;
  if ((!point_.down && !interrupted && now - lastTouchRead_ < 50U) ||
      (point_.down && now - lastTouchRead_ < 15U)) return point_;
  lastTouchRead_ = now;
  touchInterrupt = false;
  // 触摸中断是快速路径；空闲时仍每 50 ms 轮询一次，避免中断丢失后整机失去触摸。
  uint8_t data[5]{};
  if (!read(kTouchAddress, 0x02, data, sizeof(data))) {
    if (now - lastGoodTouch_ > 100) point_.down = false;
    return point_;
  }
  touchReady_ = true;
  if ((data[0] & 0x0FU) == 0) {
    point_.down = false;
    return point_;
  }
  int x = ((data[1] & 0x0FU) << 8) | data[2];
  int y = ((data[3] & 0x0FU) << 8) | data[4];
  // 拒绝休眠或异常坐标，不能将无效值钳成边缘菜单点击。
  if (x >= 128 || y >= 128) {
    point_.down = false;
    return point_;
  }
#if PET_TOUCH_SWAP_XY
  const int temp = x;
  x = y;
  y = temp;
#endif
#if PET_TOUCH_FLIP_X
  x = 127 - x;
#endif
#if PET_TOUCH_FLIP_Y
  y = 127 - y;
#endif
  point_.x = static_cast<uint8_t>(x);
  point_.y = static_cast<uint8_t>(y);
  point_.down = true;
  lastGoodTouch_ = now;
  return point_;
}

void PetBoard::present(uint16_t* pixels) {
  if (gfx_) gfx_->draw16bitRGBBitmap(0, 0, pixels, 128, 128);
}
void PetBoard::brightness(uint8_t percent) {
  if (percent > 100) percent = 100;
  ledcWrite(LCD_BL, 255U - (255U * percent / 100U));
}
void PetBoard::screen(bool on) {
  if (!gfx_) return;
  if (on) {
    gfx_->displayOn();
    setImuEnabled(true);
  } else {
    setImuEnabled(false);
    brightness(0);
    gfx_->displayOff();
  }
}
void PetBoard::idleSleep() {
#if PET_ENABLE_LIGHT_SLEEP
  if (point_.down || digitalRead(TP_INT) == LOW) return;
  esp_sleep_enable_ext1_wakeup_io(1ULL << TP_INT, ESP_EXT1_WAKEUP_ANY_LOW);
  esp_sleep_enable_timer_wakeup(60000000ULL);
  esp_light_sleep_start();
  touchInterrupt = true;
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
#else
  delay(20);
#endif
}
void PetBoard::samplePower() {
  digitalWrite(BATTERY_MEASUREMENT_CONTROL, LOW);
  delay(2);
  uint32_t total = 0;
  for (uint8_t i = 0; i < 4; ++i) total += analogReadMilliVolts(BATTERY_ADC_DATA);
  digitalWrite(BATTERY_MEASUREMENT_CONTROL, HIGH);
  const uint32_t mv = total * 2U / 4U;
  batteryMv_ = mv >= 2800 && mv <= 4400 ? static_cast<uint16_t>(mv) : 0;
  uint8_t status = 0;
  charging_ = false;
  // 未识别型号的寄存器布局可能不同，不推断其充电状态。
  if (pmuRecognized_ && read(kPmuAddress, 0x08, &status, 1)) {
    const uint8_t phase = (status >> 3U) & 3U;
    charging_ = phase == 1 || phase == 2;
  }
}
}  // namespace pet
