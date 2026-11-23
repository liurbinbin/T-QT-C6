#include "PetApp.h"

namespace pet {
void PetApp::openCompanion(bool returnToHome) {
  wake();
  driveRequested_=driveGranted_=false;
  companionRewarded_=companionForward_=companionTurn_=companionStopped_=companionGreeting_=false;
  companionScene_=CompanionScene(); view_.companionMenu=0;
  const esp_err_t error=companionService_.open(board_);
  if(error!=ESP_OK) { toast("LINK RADIO ERROR",Reaction::Thinking); return; }
  companionReturnHome_=returnToHome;
  view_.toast=nullptr; view_.page=Page::Companion; view_.sceneUntilMs=0;
  toast("CONNECT T-BAO-S31 TO PLAY TOGETHER",Reaction::Thinking);
}
void PetApp::closeCompanion() {
  driveRequested_=false; view_.companionMenu=0;
  companionService_.close();
  companionReturnHome_=false;
}
void PetApp::playCompanion(uint8_t kind) {
  const auto current=companionService_.snapshot();
  if(driveRequested_ || current.link.phase==petlink::Phase::Driving) {
    companionStopped_=driveGranted_; driveRequested_=false;
    companionService_.event(kind); toast("PARK THEN PLAY",Reaction::Thinking);
  } else if(current.link.connected) companionService_.event(kind);
  else companionScene_.preview(kind,millis(),state_.randomSeed^millis());
  if(kind==1) companionGreeting_=companionStopped_;
}
void PetApp::companionTap(uint8_t x,uint8_t y,uint32_t) {
  const RingLinkView current=companionService_.snapshot();
  const auto& s=current.link;
  if(y<20) {
    if(x<30) {
      const bool goHome=companionReturnHome_;
      closeCompanion();
      if(goHome) returnHome();
      else showScene(Scene::Park,180000);
    }
    else if(x>=98) { companionService_.stop(); companionScene_.cancel(); driveRequested_=false; view_.companionMenu=1; }
    return;
  }
  if(view_.companionMenu) {
    if(y>=108) {
      if(x<64) view_.companionMenu=0;
      else { companionService_.stop(); companionScene_.cancel(); driveRequested_=false; }
      return;
    }
    if(view_.companionMenu==1) {
      if(y>=26 && y<50) { companionService_.recalibrate(); view_.companionMenu=0; toast("SETTING CURRENT POSE",Reaction::Thinking); }
      else if(y>=55 && y<81) view_.companionMenu=2;
      else if(y>=85) view_.companionMenu=0;
    } else if(view_.companionMenu==2) {
      if(y>=76 && y<106) {
        if(x<64) view_.companionMenu=1;
        else { companionService_.forget(); view_.companionMenu=0; view_.unpairState=1; toast("UNPAIRING",Reaction::Thinking); }
      }
    } else if(view_.companionMenu==3 && y>=28 && y<104) {
      const uint8_t column=x<44 ? 0 : x<84 ? 1 : 2;
      const uint8_t kind=uint8_t(1+column+(y<65 ? 0 : 3));
      view_.companionMenu=0;
      playCompanion(kind);
    }
    return;
  }
  if(y>=108 && x<64) {
    if(s.phase==petlink::Phase::Confirm) companionService_.confirm();
    else if(s.bound) view_.companionMenu=3;
    else if(s.phase!=petlink::Phase::Pairing) companionService_.pair();
    else toast("FINDING FRIEND",Reaction::Thinking);
    return;
  }
  if(y>=108 && x>=64) {
    if(driveRequested_ || s.phase==petlink::Phase::Driving) {
      companionService_.stop(); companionScene_.cancel(); driveRequested_=false; companionStopped_=driveGranted_;
    } else if(!s.connected) toast("CONNECT CAR FIRST",Reaction::Thinking);
    else if(s.fault) toast("CAR NOT READY",Reaction::Thinking);
    else if(companionService_.start()) driveRequested_=true;
    else toast("ONE LITTLE MOMENT",Reaction::Thinking);
    return;
  }
  if(y>=24 && y<80) playCompanion(x<64 ? 6 : 1);
}
void PetApp::companion() {
  if(view_.page!=Page::Companion) return;
  const RingLinkView current=companionService_.snapshot();
  const auto& s=current.link;
  driveRequested_=companionService_.requested();
  if(s.phase==petlink::Phase::Driving) driveGranted_=true;
  if(!s.connected) {
    companionForward_=companionTurn_=companionStopped_=companionGreeting_=false;
    driveGranted_=false;
  }
  if(s.phase==petlink::Phase::Driving) {
    companionForward_=companionForward_ || s.linear>100;
    companionTurn_=companionTurn_ || s.angular>100 || s.angular< -100;
  }
  if(!companionRewarded_ && !driveRequested_ && companionStopped_ && companionForward_ &&
      companionTurn_ && companionGreeting_ && s.drivingMs>=30000 && s.event==1 && s.eventResult==1) {
    companionRewarded_=true; action(Action::Play,0);
  }
  companionService_.pet(uint8_t(stage(state_)),uint8_t(form(state_)),uint8_t(nature(state_)));
  CompanionInput input;
  input.connected=s.connected; input.driving=driveRequested_ && s.phase==petlink::Phase::Driving;
  input.generation=s.generation; input.eventRevision=s.eventRevision; input.event=s.event;
  input.eventId=s.eventId; input.eventOrigin=s.eventOrigin; input.eventSeed=s.eventSeed;
  input.eventStarted=s.eventStarted; input.eventResult=s.eventResult; input.nature=uint8_t(nature(state_));
  input.linear=s.linear; input.angular=s.angular;
  view_.companionFrame=companionScene_.update(input,millis());
  view_.linkPhase=current.error==ESP_OK ? uint8_t(s.phase) : 7;
  view_.linkBound=s.bound; view_.linkConnected=s.connected; view_.linkFault=s.fault;
  view_.linkConfirmed=s.confirmed; view_.linkCode=s.code; view_.linkMismatch=s.mismatch;
  view_.driveCalibrated=current.calibrated; view_.driveRequested=driveRequested_; view_.carBattery=s.battery;
  view_.driveZeroPending=current.zeroPending; view_.driveStarting=current.starting; view_.drivePaused=current.paused;
  view_.linkHandshake=s.handshake;
  view_.linkReason=uint8_t(s.reason); view_.unpairState=uint8_t(s.unpair);
  view_.companionSeconds=uint16_t(s.drivingMs/1000>999 ? 999 : s.drivingMs/1000);
  if(driveRequested_) lastInput_=millis();
}
} // namespace pet
