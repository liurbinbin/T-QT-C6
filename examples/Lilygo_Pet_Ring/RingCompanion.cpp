#include "RingCompanion.h"
#include <PetLinkDrive.h>
#include <stdlib.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_heap_caps.h>
#include "freertos/task.h"

namespace pet {
esp_err_t RingCompanion::open(PetBoard& board) {
  board_=&board;
  if(!task_) {
    if(xTaskCreate(task,"ring_link",6144,this,3,&task_)!=pdPASS) return ESP_ERR_NO_MEM;
    Serial.printf("LINK worker stack=6144 internal_free=%u largest=%u\n",
      unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
      unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
  }
  __atomic_store_n(&wanted_,true,__ATOMIC_RELEASE);
  __atomic_store_n(&requested_,false,__ATOMIC_RELEASE);
  post(1); return ESP_OK;
}
void RingCompanion::close() {
  __atomic_store_n(&wanted_,false,__ATOMIC_RELEASE); stop(); post(2);
}
bool RingCompanion::start() {
  const RingLinkView current=snapshot();
  if(!__atomic_load_n(&wanted_,__ATOMIC_ACQUIRE) || !current.link.connected || current.link.fault ||
      current.link.mismatch || current.link.unpair==petlink::UnpairState::Pending) return false;
  // UI 只提交意图与点击时间，取零/解锁/授权全部交给同一个 IMU 所有者。
  portENTER_CRITICAL(&mux_); startAt_=millis(); startEpoch_=current.link.sceneEpoch; event_=0; portEXIT_CRITICAL(&mux_);
  __atomic_store_n(&requested_,true,__ATOMIC_RELEASE); post(128); return true;
}
RingLinkView RingCompanion::snapshot() {
  portENTER_CRITICAL(&mux_); RingLinkView copy=view_; portEXIT_CRITICAL(&mux_); return copy;
}
void RingCompanion::task(void* self) { static_cast<RingCompanion*>(self)->run(); }
void RingCompanion::run() {
  bool active=false,granted=false,captured=false,armIssued=false,radioStopPending=false;
  uint32_t retryAt=0,requestAt=0,startEpoch=0,manualUntil=0,lastLoop=0,maxGap=0;
  petlink::Phase previousPhase=petlink::Phase::Off;
  petlink::StopReason previousReason=petlink::StopReason::None;
  petlink::InputReason previousInput=petlink::InputReason::Missing;
  bool previousPaused=false;
  petlink::DriveIntent rawDrive,sentDrive,loggedDrive;
  int loggedDirection=-1;
  uint32_t lastDriveLog=0;
  esp_err_t error=ESP_OK;
  TickType_t wake=xTaskGetTickCount();
  for(;;) {
    uint32_t now=millis();
    if(active && lastLoop && now-lastLoop>maxGap) maxGap=now-lastLoop;
    lastLoop=now;
    const uint32_t commands=__atomic_exchange_n(&commands_,0,__ATOMIC_ACQ_REL);
    if(commands&(1|2|16|32|64)) __atomic_store_n(&requested_,false,__ATOMIC_RELEASE);
    if(commands&(1|2|16|32|64|128)) driveFilter_.reset();
    if(commands&2) {
      if(initialized_) { endpoint_.drive(false,false,false,0,0); endpoint_.close(); endpoint_.poll(); }
      active=false; granted=captured=armIssued=false; manualUntil=0; radioStopPending=true;
      __atomic_store_n(&owned_,false,__ATOMIC_RELEASE);
    }
    if((commands&1) && __atomic_load_n(&wanted_,__ATOMIC_ACQUIRE)) {
      active=true; granted=captured=armIssued=false; retryAt=0; requestAt=0; error=ESP_OK;
      radioStopPending=false; __atomic_store_n(&owned_,true,__ATOMIC_RELEASE);
    }
    if(active && !__atomic_load_n(&wanted_,__ATOMIC_ACQUIRE)) {
      if(initialized_) { endpoint_.close(); endpoint_.poll(); }
      active=false; granted=captured=armIssued=false; radioStopPending=true;
      __atomic_store_n(&owned_,false,__ATOMIC_RELEASE);
    }
    if(active && ((commands&1) || !initialized_ || error!=ESP_OK) && int32_t(now-retryAt)>=0) {
      WiFi.persistent(false); WiFi.setAutoReconnect(false);
      error=WiFi.mode(WIFI_STA) ? ESP_OK : ESP_FAIL;
      if(error==ESP_OK) error=esp_wifi_start();
      if(error==ESP_OK) error=initialized_ ? endpoint_.resumeRadio() : endpoint_.begin(false);
      if(error==ESP_OK) { initialized_=true; endpoint_.open(); esp_wifi_set_ps(WIFI_PS_NONE); }
      else { esp_wifi_stop(); retryAt=now+3000; }
    }
    if(initialized_) {
      if(commands&4) endpoint_.pair();
      if(commands&8) endpoint_.confirm();
      if(commands&16) { endpoint_.stop(); captured=armIssued=granted=false; manualUntil=0; }
      if(commands&32) endpoint_.forget();
      if((commands&64) && active) manualUntil=now+500;
      if((commands&128) && !(commands&(2|16|32|64)) && __atomic_load_n(&requested_,__ATOMIC_ACQUIRE)) {
        captured=armIssued=granted=false;
        portENTER_CRITICAL(&mux_); requestAt=startAt_; startEpoch=startEpoch_; portEXIT_CRITICAL(&mux_);
      }
      if(active && error==ESP_OK) {
        bool requested=__atomic_load_n(&requested_,__ATOMIC_ACQUIRE);
        const bool expiredBeforeSample=requested && captured && tilt_.expired(now);
        Motion sample;
        if(board_->motion(sample)) tilt_.sample(sample.ax,sample.ay,sample.az,sample.gx,sample.gy,sample.gz,sample.timestampMs);
        now=millis(); // 必须在 I2C 完成后计算新鲜度。
        auto link=endpoint_.snapshot();
        if(manualUntil) {
          if(tilt_.capture(now) || int32_t(now-manualUntil)>=0) manualUntil=0;
        }
        if(requested && !captured) {
          captured=tilt_.capture(now);
          if(captured) driveFilter_.reset();
        }
        const bool hardInput=captured && (expiredBeforeSample || tilt_.expired(now));
        const bool firstEpochStopped=requested && !startEpoch && link.sceneEpoch &&
          link.reason!=petlink::StopReason::None && link.reason!=petlink::StopReason::Reconnecting;
        if(requested && !startEpoch && link.sceneEpoch) startEpoch=link.sceneEpoch;
        const bool remoteStopped=firstEpochStopped || (startEpoch && link.sceneEpoch && link.sceneEpoch!=startEpoch) ||
          (armIssued && (link.locked || (granted && link.phase!=petlink::Phase::Driving)));
        if(requested && (!link.connected || link.fault || link.mismatch || hardInput || remoteStopped ||
            (now-requestAt>=1800 && link.phase!=petlink::Phase::Driving))) {
          const auto reason=!link.connected ? petlink::StopReason::LinkLost : link.fault ? petlink::StopReason::MotionFault :
            hardInput || !captured ? petlink::StopReason::ImuStale :
            remoteStopped && link.reason!=petlink::StopReason::None ? link.reason : petlink::StopReason::RemoteStop;
          requested=false; __atomic_store_n(&requested_,false,__ATOMIC_RELEASE); endpoint_.stop(reason);
        }
        if(!requested) {
          requestAt=0; granted=captured=armIssued=false; driveFilter_.reset();
          loggedDirection=-1;
        }
        if(requested && captured && !armIssued && tilt_.ready(now) && !link.locked && link.sceneEpoch) {
          armIssued=true; startEpoch=link.sceneEpoch;
        }
        if(link.phase==petlink::Phase::Driving && requested) granted=true;
        const uint32_t profile=__atomic_load_n(&profile_,__ATOMIC_ACQUIRE);
        endpoint_.pet(profile&255,(profile>>8)&255,(profile>>16)&255);
        const bool paused=requested && captured && !tilt_.ready(now);
        sentDrive=petlink::DriveIntent();
        if(requested && armIssued && !paused) {
          rawDrive=petlink::DriveIntent(int16_t(tilt_.linear()*PET_IMU_VERTICAL_SIGN),
              int16_t(tilt_.angular()*PET_IMU_HORIZONTAL_SIGN));
          sentDrive=petlink::directDriveIntent(rawDrive.linear,rawDrive.angular);
          sentDrive.angular=driveFilter_.update(sentDrive.linear,sentDrive.angular).angular;
        }
        // 两轴独立发送；暂停/停止仍必须发零，不能复用最后一次非零速度。
        endpoint_.drive(requested && armIssued,!hardInput,true,sentDrive.linear,sentDrive.angular,paused);
        uint8_t event=0;
        portENTER_CRITICAL(&mux_);
        const int32_t eventAge=int32_t(millis()-eventAt_);
        if(event_ && eventAge>3000) event_=0;
        if(event_ && link.connected && link.sceneEpoch && link.phase!=petlink::Phase::Driving && !link.locked && !requested &&
            eventAge>=100) { event=event_; event_=0; }
        portEXIT_CRITICAL(&mux_);
        if(event) endpoint_.event(event);
      }
      endpoint_.poll();
    }
    if(radioStopPending && (!initialized_ || endpoint_.radioIdle())) {
      esp_wifi_stop(); radioStopPending=false;
    }
    RingLinkView next;
    if(initialized_) next.link=endpoint_.snapshot();
    now=millis(); next.active=active; next.error=error;
    next.ready=tilt_.ready(now); next.calibrated=tilt_.calibrated(); next.neutral=tilt_.neutral(now);
    next.imuAge=tilt_.age(now); next.rawAge=tilt_.rawAge(now); next.inputReason=tilt_.reason(now); next.maxWorkerGap=maxGap;
    next.requested=__atomic_load_n(&requested_,__ATOMIC_ACQUIRE);
    next.zeroPending=(next.requested && !captured) || manualUntil!=0;
    next.starting=next.requested && next.link.phase!=petlink::Phase::Driving;
    next.paused=next.requested && captured && !next.ready;
    const int direction=((sentDrive.linear>0)-(sentDrive.linear<0)+1)*3+
      (sentDrive.angular>0)-(sentDrive.angular<0)+1;
    if(next.requested && !next.paused && next.link.phase==petlink::Phase::Driving &&
        (direction!=loggedDirection || abs(int(sentDrive.linear)-loggedDrive.linear)>=100 ||
          abs(int(sentDrive.angular)-loggedDrive.angular)>=100) && now-lastDriveLog>=500) {
      // 只在发送之后记录，串口不能延后本次新鲜控制。duty由车端独立记录。
      Serial.printf("RING_DRIVE raw=(%d,%d) sent=(%d,%d) full_scale=1000\n",
        int(rawDrive.linear),int(rawDrive.angular),int(sentDrive.linear),int(sentDrive.angular));
      loggedDrive=sentDrive; loggedDirection=direction; lastDriveLog=now;
    }
    if(next.link.phase!=previousPhase || next.link.reason!=previousReason || next.paused!=previousPaused ||
        (next.requested && next.inputReason!=previousInput)) {
      Serial.printf("LINK phase=%u reason=%u hs=%u paused=%u input=%u good_age=%lu raw_age=%lu ack_age=%lu ack=%lu bus_age=%lu gap=%lu tx_err=%ld tx_nomem=%lu auth_bad=%lu reconnects=%lu\n",
        unsigned(next.link.phase),unsigned(next.link.reason),unsigned(next.link.handshake),unsigned(next.paused),unsigned(next.inputReason),
        static_cast<unsigned long>(next.imuAge),static_cast<unsigned long>(next.rawAge),static_cast<unsigned long>(next.link.ackAge),
        static_cast<unsigned long>(next.link.controlAck),static_cast<unsigned long>(next.link.businessAge),static_cast<unsigned long>(maxGap),
        static_cast<long>(next.link.lastTxError),static_cast<unsigned long>(next.link.txNoMem),static_cast<unsigned long>(next.link.authFailed),
        static_cast<unsigned long>(next.link.reconnects));
      previousPhase=next.link.phase; previousReason=next.link.reason; previousPaused=next.paused; previousInput=next.inputReason;
    }
    portENTER_CRITICAL(&mux_); view_=next; portEXIT_CRITICAL(&mux_);
    if(xTaskGetTickCount()-wake>pdMS_TO_TICKS(100)) wake=xTaskGetTickCount();
    vTaskDelayUntil(&wake,pdMS_TO_TICKS(active || radioStopPending ? 10 : 40));
  }
}
} // namespace pet
