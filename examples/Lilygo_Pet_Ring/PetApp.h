#pragma once

#include "PetBoard.h"
#include "PetClock.h"
#include "PixelRenderer.h"
#include "SaveStore.h"
#include "RingCompanion.h"

namespace pet {
class PetApp {
 public:
  void begin();
  void update();

 private:
  void tick();
  void input();
  void tap(uint8_t x, uint8_t y, uint32_t heldMs, int dx, int dy);
  ActionResult action(Action value, uint16_t score = 0);
  void feedback(const ActionResult& result);
  void taskTouch(uint8_t x, uint8_t y, int dx, int dy);
  void returnHome();
  void toast(const char* text, Reaction reaction = Reaction::Happy);
  bool save();
  void wake();
  void serial();
  void command(const char* line);
  void startGame();
  void game();
  void nextDrop();
  void motion();
  void projectGroundPosition();
  void updateWorldPosition();
  void updateSleepWalk();
  void triggerDizzy();
  bool petHit(uint8_t x, uint8_t y) const;
  void startSleepWalk(bool goingToSleep);
  void itemTouch(uint8_t x, uint8_t y);
  const char* bedPhrase(bool goingToSleep);
  void showScene(Scene scene, uint32_t durationMs);
  void updateScene();
  void openCompanion(bool returnToHome = false);
  void closeCompanion();
  void companion();
  void companionTap(uint8_t x, uint8_t y, uint32_t heldMs);
  void playCompanion(uint8_t kind);

  RingCompanion companionService_;
  CompanionScene companionScene_;
  bool driveRequested_ = false;
  bool driveGranted_ = false;
  bool companionRewarded_ = false;
  bool companionForward_ = false;
  bool companionTurn_ = false;
  bool companionStopped_ = false;
  bool companionGreeting_ = false;
  bool companionReturnHome_ = false;
  PetBoard board_;
  SaveStore store_;
  State state_{};
  View view_;
  PixelRenderer renderer_;
  uint16_t pixels_[128 * 128];
  int64_t simulatedUs_ = 0;
  uint32_t lastInput_ = 0;
  uint32_t lastFrame_ = 0;
  uint32_t lastPower_ = 0;
  uint32_t lastSave_ = 0;
  uint32_t savePending_ = 0;
  uint32_t toastUntil_ = 0;
  uint32_t pressTime_ = 0;
  uint32_t gameStart_ = 0;
  uint32_t dropStart_ = 0;
  uint32_t gameRandom_ = 1;
  uint32_t sleepWalkStart_ = 0;
  uint32_t lastMotionMs_ = 0;
  uint32_t lastMotionDebugMs_ = 0;
  uint32_t lastGreetingMs_ = 0;
  uint32_t lastTipMs_ = 0;
  uint32_t lastTaskActionMs_ = 0;
  int32_t motionMoveAccumulator_ = 0;
  int32_t motionVerticalAccumulator_ = 0;
  uint32_t itemRandom_ = 1;
  uint32_t shakeWindowStart_ = 0;
  uint32_t lastDizzyMs_ = 0;
  uint32_t boopWindowStart_ = 0;
  uint32_t lastGameTiltMs_ = 0;
  uint8_t shakeHits_ = 0;
  uint8_t boopCount_ = 0;
  int16_t sleepFromX_ = 64;
  int16_t sleepFromWorldX_ = 80;
  int16_t sleepFromY_ = 72;
  Motion previousMotion_{};
  bool previousMotionValid_ = false;
  uint8_t pressX_ = 0;
  uint8_t pressY_ = 0;
  uint8_t finalX_ = 0;
  uint8_t finalY_ = 0;
  uint16_t maxMove_ = 0;
  char toastText_[48]{};
  char serialLine_[64]{};
  uint8_t serialUsed_ = 0;
  bool serialOverflow_ = false;
  bool ready_ = false;
  bool screenOn_ = true;
  bool dimmed_ = false;
  bool pressed_ = false;
  bool ignoreTouch_ = false;
  bool dirty_ = false;
  bool clockKnown_ = false;
  bool storageReady_ = false;
};
}  // namespace pet
