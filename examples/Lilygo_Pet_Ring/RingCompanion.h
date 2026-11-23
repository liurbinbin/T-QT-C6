#pragma once
#include "PetBoard.h"
#include <PetLink.h>
#include <PetLinkDrive.h>
#include "freertos/task.h"

namespace pet {
struct RingLinkView {
  petlink::Snapshot link;
  bool active=false,ready=false,calibrated=false,neutral=false,requested=false;
  bool zeroPending=false,starting=false,paused=false;
  uint32_t imuAge=UINT32_MAX,rawAge=UINT32_MAX,maxWorkerGap=0;
  petlink::InputReason inputReason=petlink::InputReason::Missing;
  esp_err_t error=ESP_OK;
};
class RingCompanion {
 public:
  esp_err_t open(PetBoard& board);
  void close();
  void pair() { post(4); }
  void confirm() { post(8); }
  void stop() {
    __atomic_store_n(&requested_,false,__ATOMIC_RELEASE);
    portENTER_CRITICAL(&mux_); event_=0; portEXIT_CRITICAL(&mux_);
    post(16);
  }
  void forget() { stop(); post(32); }
  void recalibrate() { stop(); post(64); }
  void event(uint8_t value) {
    if(requested()) stop();
    portENTER_CRITICAL(&mux_); eventAt_=millis(); event_=value; portEXIT_CRITICAL(&mux_);
  }
  bool start();
  void pet(uint8_t stage,uint8_t form,uint8_t nature) {
    __atomic_store_n(&profile_,uint32_t(stage)|(uint32_t(form)<<8)|(uint32_t(nature)<<16),__ATOMIC_RELEASE);
  }
  bool ownsMotion() const { return __atomic_load_n(&owned_,__ATOMIC_ACQUIRE); }
  bool requested() const { return __atomic_load_n(&requested_,__ATOMIC_ACQUIRE); }
  RingLinkView snapshot();
 private:
  void post(uint32_t mask) { __atomic_fetch_or(&commands_,mask,__ATOMIC_RELEASE); }
  static void task(void*);
  void run();
  PetBoard* board_=nullptr;
  petlink::Endpoint endpoint_;
  petlink::Tilt tilt_;
  petlink::DriveIntentFilter driveFilter_;
  TaskHandle_t task_=nullptr;
  RingLinkView view_;
  portMUX_TYPE mux_=portMUX_INITIALIZER_UNLOCKED;
  uint32_t commands_=0,profile_=0,eventAt_=0,startAt_=0,startEpoch_=0;
  uint8_t event_=0;
  bool wanted_=false,requested_=false,owned_=false,initialized_=false;
};
} // namespace pet
