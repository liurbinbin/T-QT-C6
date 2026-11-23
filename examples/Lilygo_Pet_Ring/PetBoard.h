#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <memory>
#include <pin_config.h>
#include "Arduino_DriveBus_Library.h"
#include "PetConfig.h"
#include "ImuSample.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace pet {
struct Touch {
  bool down = false;
  uint8_t x = 0;
  uint8_t y = 0;
};

class PetBoard {
 public:
  bool begin();
  Touch touch();
  bool motion(Motion& sample);
  void present(uint16_t* pixels);
  void brightness(uint8_t percent);
  void screen(bool on);
  void idleSleep();
  void samplePower();
  uint16_t batteryMv() const { return batteryMv_; }
  bool charging() const { return charging_; }
  bool touchReady() const { return touchReady_; }
  bool pmuConfigured() const { return pmuConfigured_; }
  bool imuReady() const { return imuReady_; }

 private:
  StaticSemaphore_t wireMutexStorage_{};
  SemaphoreHandle_t wireMutex_=nullptr;
  bool read(uint8_t address, uint8_t reg, uint8_t* bytes, size_t count);
  bool write(uint8_t address, uint8_t reg, uint8_t value);
  bool configureImu();
  void setImuEnabled(bool enabled);
  Arduino_DataBus* bus_ = nullptr;
  Arduino_GFX* gfx_ = nullptr;
  std::shared_ptr<Arduino_IIC_DriveBus> iicBus_;
  std::unique_ptr<Arduino_IIC> touchDevice_;
  std::unique_ptr<Arduino_IIC> imuDevice_;
  Touch point_;
  uint32_t lastTouchRead_ = 0;
  uint32_t lastGoodTouch_ = 0;
  uint16_t batteryMv_ = 0;
  bool charging_ = false;
  bool touchReady_ = false;
  bool pmuConfigured_ = false;
  bool pmuRecognized_ = false;
  bool imuReady_ = false;
  uint32_t lastMotionRead_ = 0;
};
}  // namespace pet
