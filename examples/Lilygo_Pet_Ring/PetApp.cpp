#include "PetApp.h"
#include "GroundMotion.h"
#include "GroundGeometry.h"
#include "RoomLayout.h"

#include <esp_random.h>
#include <esp_timer.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

namespace pet {
void PetApp::begin() {
  Serial.begin(115200);
  Serial.println("LILYGO Pet Ring - YAYA / pixel companion");
  state_ = newState(esp_random());
  const SaveStore::Load loaded = store_.begin(state_);
  view_.language = store_.language();
  storageReady_ = loaded == SaveStore::Load::Loaded || loaded == SaveStore::Load::Fresh;
  view_.saveError = !storageReady_;
  if (!storageReady_)
    Serial.println("SAVE ERROR: existing data preserved; this session cannot save.");

  const time_t epoch = time(nullptr);
  clockKnown_ = epoch >= 1704067200 && epoch < 4102444800LL &&
                synchronizeClock(state_, static_cast<uint32_t>(epoch));
  if (!clockKnown_) state_.clockTrusted = 0;
  ready_ = board_.begin();
  if (!ready_) {
    Serial.println("DISPLAY/I2C INIT FAILED: check pin_config.h and board connection.");
    return;
  }
  simulatedUs_ = esp_timer_get_time();
  lastInput_ = millis();
  lastMotionMs_ = lastInput_;
  lastMotionDebugMs_ = lastInput_;
  lastGreetingMs_ = lastInput_;
  lastTipMs_ = lastInput_;
  itemRandom_ = state_.randomSeed ^ lastInput_;
  lastSave_ = lastInput_;
  lastPower_ = lastInput_;
  view_.imuReady = board_.imuReady();
  if (state_.sleeping) {
    view_.worldX = 260;
    updateWorldPosition();
    view_.petY = 80;
    view_.petDepth = 85;
    view_.petScale = 35;
    view_.autoMoving = false;
  } else {
    updateWorldPosition();
    projectGroundPosition();
  }
  board_.brightness(state_.brightness);
  if (storageReady_) save();
  toast(!board_.touchReady() ? "TOUCH NOT DETECTED" :
        !storageReady_ ? "SAVE ERROR" : greetingText(state_, millis()), Reaction::Heart);
  Serial.println("Commands: STATUS | TIME <unix_seconds> | SAVE | HELP");
  Serial.println("TIME uses UTC Unix seconds; day/night display uses UTC+8.");
  if (!clockKnown_)
    Serial.println("Clock unknown: online growth works; offline time waits for TIME sync.");
}

void PetApp::tick() {
  const int64_t now = esp_timer_get_time();
  const uint32_t seconds = static_cast<uint32_t>((now - simulatedUs_) / 1000000LL);
  if (seconds) {
    elapse(state_, seconds, clockKnown_);
    simulatedUs_ += static_cast<int64_t>(seconds) * 1000000LL;
    dirty_ = true;
  }
}

void PetApp::toast(const char* text, Reaction reaction) {
  strncpy(toastText_, text, sizeof(toastText_) - 1);
  toastText_[sizeof(toastText_) - 1] = '\0';
  view_.toast = toastText_;
  view_.reaction = reaction;
  toastUntil_ = millis() + 2500U;
  Serial.println(text);
}

bool PetApp::save() {
  const bool success = storageReady_ && store_.save(state_);
  view_.saveError = !success;
  if (success) dirty_ = false;
  lastSave_ = millis();
  savePending_ = 0;
  if (!success) Serial.println("SAVE FAILED: keeping previous valid slot.");
  return success;
}

void PetApp::wake() {
  const bool wasOff = !screenOn_;
  if (wasOff) {
    screenOn_ = true;
    board_.screen(true);
  }
  dimmed_ = false;
  lastInput_ = millis();
  board_.brightness(state_.brightness);
  if (wasOff) {
    lastGreetingMs_ = lastInput_;
    toast(greetingText(state_, lastInput_), Reaction::Happy);
  }
}

void PetApp::feedback(const ActionResult& result) {
  toast(result.message, result.reaction);
  if (result.changed) {
    dirty_ = true;
    if (!savePending_) savePending_ = millis() + 3000U;
  }
}

ActionResult PetApp::action(Action value, uint16_t score) {
  if (view_.sleepTransition != 0)
    return {false, false, Reaction::Thinking, "ONE LITTLE MOMENT"};
  const ActionResult result = act(state_, value, score);
  feedback(result);
  if (result.changed && value == Action::Sleep) {
    view_.page = Page::Home;
    view_.scene = Scene::Room;
    view_.sceneUntilMs = 0;
    toast(bedPhrase(state_.sleeping),
          state_.sleeping ? Reaction::Sleeping : Reaction::Happy);
    startSleepWalk(state_.sleeping);
  }
  return result;
}

void PetApp::input() {
  const Touch point = board_.touch();
  const uint32_t now = millis();
  if (point.down && !pressed_) {
    pressed_ = true;
    pressTime_ = now;
    pressX_ = finalX_ = point.x;
    pressY_ = finalY_ = point.y;
    maxMove_ = 0;
    if (!screenOn_) {
      wake();
      ignoreTouch_ = true;
    } else {
      wake();
      ignoreTouch_ = false;
      if(view_.page==Page::Companion && driveRequested_ && point.y>=108) {
        driveRequested_=false;
        companionStopped_=driveGranted_;
        companionService_.stop(); companionScene_.cancel();
        ignoreTouch_=true; // 停止按下即生效，抬手时不能再次触发开始。
      }
    }
  }
  if (point.down && pressed_) {
    finalX_ = point.x;
    finalY_ = point.y;
    const uint16_t move = abs(static_cast<int>(finalX_) - pressX_) +
                          abs(static_cast<int>(finalY_) - pressY_);
    if (move > maxMove_) maxMove_ = move;
    lastInput_ = now;
    if (!ignoreTouch_ && maxMove_ > 12 && now - lastMotionMs_ >= 25U &&
        view_.page == Page::Home && view_.scene == Scene::Room &&
        view_.sleepTransition == 0 && !state_.sleeping &&
        point.y >= 20 && point.y < 108) {
      lastMotionMs_ = now;
      view_.autoMoving = false;
      const int deltaX = static_cast<int>(point.x) - view_.petX;
      const int deltaY = static_cast<int>(point.y) - view_.petY;
      if (abs(deltaX) > 2) view_.worldX += static_cast<int16_t>(deltaX > 0 ? 4 : -4);
      if (abs(deltaY) > 2) view_.petDepth += static_cast<int16_t>(deltaY > 0 ? 3 : -3);
      updateWorldPosition();
      projectGroundPosition();
    }
    if (view_.page == Page::Game && !ignoreTouch_ && point.y >= 20)
      view_.gameLane = point.x < 43 ? 0 : point.x < 85 ? 1 : 2;
  }
  if (!point.down && pressed_) {
    pressed_ = false;
    if (!ignoreTouch_)
      tap(pressX_, pressY_, now - pressTime_,
          static_cast<int>(finalX_) - pressX_, static_cast<int>(finalY_) - pressY_);
    ignoreTouch_ = false;
  }
}

bool PetApp::petHit(uint8_t x, uint8_t y) const {
  return abs(static_cast<int>(x) - view_.petX) <= 24 &&
         abs(static_cast<int>(y) - view_.petY) <= 27;
}

void PetApp::taskTouch(uint8_t x, uint8_t y, int dx, int dy) {
  const uint32_t now = millis();
  if (now - lastTaskActionMs_ < 350U) return;
  lastTaskActionMs_ = now;
  if (view_.scene == Scene::Restaurant) {
    if (y >= 22 && y < 45) {
      const uint8_t selected = x < 43 ? 0 : x < 86 ? 1 : 2;
      if (selected != view_.mealFood || view_.mealBites >= 3)
        view_.mealBites = 0;
      view_.mealFood = selected;
      if (maxMove_ < 20) {
        toast("DRAG FOOD FOR THREE BITES", Reaction::Thinking);
        return;
      }
      if (y + dy < 55 || x + dx < 36 || x + dx > 92) return;
    } else if (x < 38 || x > 90 || y < 65 || y >= 106) {
      toast("CHOOSE FOOD. TAP BOWL!", Reaction::Thinking);
      return;
    }
    if (state_.nursery) { toast("BACK FROM NURSERY?", Reaction::Sleeping); return; }
    if (view_.mealBites >= 3) view_.mealBites = 0;
    ++view_.mealBites;
    if (view_.mealBites < 3) {
      toast(view_.mealBites == 1 ? "NIBBLE! 1/3" : "NOM! 2/3", Reaction::Eating);
    } else {
      const uint32_t meals = state_.feedCount;
      action(Action::Feed, view_.mealFood);
      if (state_.feedCount == meals) view_.mealBites = 0;
    }
    return;
  }
  if (view_.scene == Scene::Washroom) {
    const bool shower = x >= 90 && y >= 28 && y <= 75;
    const bool soap = x <= 35 && y >= 28 && y <= 75;
    const bool rub = x >= 36 && x <= 89 && y >= 45 && y <= 90 && maxMove_ >= 20;
    const bool towel = y >= 85 && y < 108;
    if (view_.bathStep == 5) view_.bathStep = 0;
    if (state_.nursery) { toast("BACK FROM NURSERY?", Reaction::Sleeping); return; }
    if (view_.bathStep == 0 && shower) ++view_.bathStep;
    else if (view_.bathStep == 1 && soap) ++view_.bathStep;
    else if (view_.bathStep == 2 && rub) ++view_.bathStep;
    else if (view_.bathStep == 3 && shower) ++view_.bathStep;
    else if (view_.bathStep == 4 && towel) {
      const uint32_t baths = state_.cleanCount;
      action(Action::Clean);
      if (state_.cleanCount != baths) view_.bathStep = 5;
      return;
    } else {
      static const char* const hints[] = {"TAP SHOWER", "TAP SOAP", "RUB MY TUMMY", "RINSE: TAP SHOWER", "TAP TOWEL"};
      toast(hints[view_.bathStep], Reaction::Thinking);
      return;
    }
    toast("SPLISH SPLASH!", Reaction::Washing);
    return;
  }
  if (view_.scene == Scene::Clinic) {
    if (y >= 45 && y <= 100) feedback(treat(state_));
    return;
  }
  itemTouch(x, y);
}

void PetApp::itemTouch(uint8_t x, uint8_t y) {
  itemRandom_ = itemRandom_ * 1664525U + 1013904223U;
  const uint8_t variant = static_cast<uint8_t>((itemRandom_ >> 24U) % 5U);
  if (view_.scene == Scene::Park) {
    if (x < 64 && y >= 55) {
      startGame();
    } else if(x>=64 && y>=55) {
      openCompanion();
    }
    return;
  }

  const RoomObject object = roomObjectAt(x + view_.cameraX, y);
  if (object == RoomObject::Kitchen) {
    showScene(Scene::Restaurant, 180000);
    toast("WHAT'S COOKING?", Reaction::Eating);
    return;
  }
  if (object == RoomObject::GardenDoor) {
    showScene(Scene::Park, 180000);
    toast("LET'S GO OUTSIDE!", Reaction::Happy);
    return;
  }
  if (object == RoomObject::Baozai) {
    openCompanion(true);
    return;
  }
  if (object == RoomObject::Books || object == RoomObject::Clock ||
      object == RoomObject::Sofa || object == RoomObject::Aquarium ||
      object == RoomObject::Toy || object == RoomObject::Chest ||
      object == RoomObject::Lamp) {
    static const char* const books[] = {
        "A DRAGON IN A BOOK!", "ONE MORE PAGE?", "A LEAF BOOKMARK!",
        "I CAN READ 'HUG'!", "A STORY ABOUT US!"};
    static const char* const clock[] = {
        "TICK TOCK, SNACK TIME!", "TIME FOR A TINY HUG!", "THE CLOCK WINKED!",
        "IS IT COOKIE O'CLOCK?", "ONE SECOND OF MAGIC!"};
    static const char* const sofa[] = {
        "SOFT LITTLE CLOUD!", "FOUND A COZY SPOT!", "ROOM FOR YOU TOO!",
        "A CUSHION FORT!", "MY LEAF NEEDS A NAP!"};
    static const char* const fish[] = {
        "FISH SAYS BLUB!", "A BUBBLE KISS!", "WE MATCHED WIGGLES!",
        "FISH WANTS A NAME!", "A TINY UNDERSEA DANCE!"};
    static const char* const toy[] = {
        "BOING! MY TURN!", "THE BALL HAS A HUG!", "A TINY GOAL!",
        "I CAN BALANCE IT!", "CATCH MY HAPPY BOUNCE!"};
    static const char* const chest[] = {
        "A TREASURE OF HUGS!", "FOUND A SOFT FEATHER!", "A SECRET INSIDE!",
        "MY FAVORITE PEBBLE!", "JUST ONE SHINY LEAF!"};
    static const char* const lamp[] = {
        "A LITTLE SUN INDOORS!", "MY SHADOW WAVES!", "WARM AND COZY!",
        "THE LIGHT LOVES LEAVES!", "A GLOW FOR MY FRIEND!"};
    const bool curious = object == RoomObject::Books || object == RoomObject::Clock;
    const ActionResult played = interact(state_,
        object == RoomObject::Aquarium ? Form::Dew : curious ? Form::Leaf : Form::Fruit,
        curious ? Nature::Clever : Nature::Cuddly);
    if (played.changed) {
      dirty_ = true;
      if (!savePending_) savePending_ = millis() + 3000U;
    }
    const char* const* phrases = object == RoomObject::Books ? books :
        object == RoomObject::Clock ? clock : object == RoomObject::Sofa ? sofa :
        object == RoomObject::Aquarium ? fish : object == RoomObject::Toy ? toy :
        object == RoomObject::Chest ? chest : lamp;
    static const Reaction responses[] = {Reaction::Happy, Reaction::Heart,
        Reaction::Thinking, Reaction::Heart, Reaction::Happy};
    toast(phrases[variant], responses[variant]);
    view_.secretEffect = static_cast<uint8_t>(1U + variant % 2U);
    return;
  }

  const ActionResult bondResult = interact(state_,
      object == RoomObject::Plant ? Form::Leaf : Form::Dew, Nature::Explorer);
  if (bondResult.changed) {
    dirty_ = true;
    if (!savePending_) savePending_ = millis() + 3000U;
  }
  const int windowX = 28 - view_.cameraX;
  const int plantX = 48 - view_.cameraX;
  if (x >= windowX && x <= windowX + 31 && y >= 25 && y <= 57) {
    static const char* const phrases[] = {
        "A BIRD SAYS HI!", "THE CLOUD WAVES!", "LOOK, A BUTTERFLY!",
        "MOON IS BLUSHING!", "A TINY STAR FELL!"};
    static const Reaction reactions[] = {Reaction::Heart, Reaction::Happy,
                                         Reaction::Heart, Reaction::Thinking,
                                         Reaction::Heart};
    toast(phrases[variant], reactions[variant]);
    view_.secretEffect = static_cast<uint8_t>(1U + variant % 3U);
    return;
  }
  if (x >= plantX - 7 && x <= plantX + 10 && y >= 70 && y <= 101) {
    static const char* const phrases[] = {
        "THE PLANT LIKES YOU!", "NEW LEAF! REALLY!", "WATER? MAYBE LATER!",
        "IT KNOWS YOUR NAME!", "A LEAF GIVES A HUG!"};
    static const Reaction reactions[] = {Reaction::Happy, Reaction::Heart,
                                         Reaction::Thinking, Reaction::Heart,
                                         Reaction::Heart};
    toast(phrases[variant], reactions[variant]);
    view_.secretEffect = static_cast<uint8_t>(1U + variant % 3U);
  }
}

const char* PetApp::bedPhrase(bool goingToSleep) {
  itemRandom_ = itemRandom_ * 1664525U + 1013904223U;
  const uint8_t variant = static_cast<uint8_t>((itemRandom_ >> 24U) % 5U);
  static const char* const sleepPhrases[] = {
      "GOOD NIGHT, SUNBEAM!", "DREAM OF LITTLE STARS!", "COVER ME, PLEASE!",
      "BEDTIME IS COZY!", "SEE YOU IN DREAMLAND!"};
  static const char* const wakePhrases[] = {
      "GOOD MORNING, FRIEND!", "I HAD A TINY DREAM!", "WHERE IS BREAKFAST?",
      "READY FOR A NEW DAY!", "MY LEAF FEELS FRESH!"};
  return goingToSleep ? sleepPhrases[variant] : wakePhrases[variant];
}

void PetApp::startSleepWalk(bool goingToSleep) {
  sleepWalkStart_ = millis();
  sleepFromX_ = view_.petX;
  if (goingToSleep) {
    sleepFromWorldX_ = view_.worldX;
    sleepFromY_ = view_.petY;
  } else {
    sleepFromWorldX_ = 220;
    view_.petDepth = 60;
    sleepFromY_ = groundPose(view_.petDepth, stage(state_)).centerY;
  }
  view_.sleepTransition = goingToSleep ? 1 : 2;
  view_.sleepProgress = 0;
  view_.autoMoving = false;
}

void PetApp::returnHome() {
  view_.page = Page::Home;
  view_.scene = Scene::Room;
  view_.sceneUntilMs = 0;
  view_.mealBites = 0;
  view_.bathStep = 0;
  if (state_.sleeping) {
    view_.worldX = 260;
    updateWorldPosition();
    view_.petY = 80;
    view_.petScale = 35;
    view_.autoMoving = false;
  } else {
    updateWorldPosition();
    projectGroundPosition();
    view_.autoMoving = true;
  }
  lastInput_ = millis();
}

void PetApp::showScene(Scene scene, uint32_t durationMs) {
  view_.scene = scene;
  view_.sceneUntilMs = millis() + durationMs;
  view_.mealBites = 0;
  view_.bathStep = 0;
  view_.reaction = Reaction::Idle;
  view_.page = Page::Scene;
  view_.autoMoving = false;
  view_.petScale = 100;
  view_.petX = 64;
  view_.petY = scene == Scene::Restaurant ? 74 : 72;
  view_.petDepth = scene == Scene::Restaurant ? 70 : 55;
}

void PetApp::updateScene() {
  if (view_.scene == Scene::Room || view_.scene == Scene::Outdoor ||
      view_.sceneUntilMs == 0) return;
  if (static_cast<int32_t>(millis() - view_.sceneUntilMs) < 0) {
    if (view_.scene == Scene::Restaurant) {
      view_.petX = 64;
      view_.petY = 74;
    } else {
      view_.petX = 64;
      view_.petY = 72;
    }
    return;
  }
  returnHome();
  view_.reaction = Reaction::Idle;
}

void PetApp::updateSleepWalk() {
  if (view_.sleepTransition == 0) return;
  const uint32_t elapsed = millis() - sleepWalkStart_;
  const uint32_t duration = 3500;
  const uint32_t progress = elapsed >= duration ? 100U : elapsed * 100U / duration;
  view_.sleepProgress = static_cast<uint8_t>(progress);
  const int16_t bedWorldX = 260;
  const int16_t bedY = 80;
  if (view_.sleepTransition == 1) {
    if (progress < 35U) {
      view_.worldX = static_cast<int16_t>(sleepFromWorldX_ +
          (bedWorldX - sleepFromWorldX_) * static_cast<int32_t>(progress) / 35);
      updateWorldPosition();
      view_.petY = sleepFromY_;
      view_.petScale = 100;
    } else if (progress < 70U) {
      view_.worldX = bedWorldX;
      updateWorldPosition();
      view_.petY = 70;
      view_.petScale = static_cast<uint8_t>(100U - (progress - 35U) * 65U / 35U);
    } else {
      view_.worldX = bedWorldX;
      updateWorldPosition();
      view_.petY = bedY;
      view_.petScale = 35;
    }
    if (progress < 35U && ((elapsed / 180U) & 1U)) view_.petY -= 2;
  } else {
    if (progress < 30U) {
      view_.worldX = bedWorldX;
      updateWorldPosition();
      view_.petY = bedY;
      view_.petScale = 35;
    } else {
      view_.worldX = static_cast<int16_t>(bedWorldX +
          (sleepFromWorldX_ - bedWorldX) * static_cast<int32_t>(progress - 30U) / 70);
      updateWorldPosition();
      view_.petY = static_cast<int16_t>(bedY +
          (sleepFromY_ - bedY) * static_cast<int32_t>(progress - 30U) / 70);
      view_.petScale = static_cast<uint8_t>(35U + (progress - 30U) * 65U / 70U);
      if (progress < 90U && ((elapsed / 180U) & 1U)) view_.petY -= 2;
    }
  }
  if (progress >= 100) {
    view_.sleepTransition = 0;
    view_.sleepProgress = 0;
    if (state_.sleeping) {
      view_.worldX = bedWorldX;
      updateWorldPosition();
      view_.petY = bedY;
      view_.petScale = 35;
    } else {
      view_.worldX = sleepFromWorldX_;
      updateWorldPosition();
      projectGroundPosition();
      view_.autoMoving = true;
    }
    view_.reaction = Reaction::Idle;
  }
}

void PetApp::triggerDizzy() {
  const uint32_t now = millis();
  if (now - lastDizzyMs_ < 5000U) return;
  lastDizzyMs_ = now;
  view_.dizzyUntilMs = now + 4500U;
  view_.secretEffect = 3;
  toast("WHOOPS! SPINNY!", Reaction::Dizzy);
}

void PetApp::updateWorldPosition() {
  if (view_.worldX < 18) view_.worldX = 18;
  if (view_.worldX > 302) view_.worldX = 302;
  int16_t camera = static_cast<int16_t>(view_.worldX - 64);
  if (camera < 0) camera = 0;
  if (camera > 192) camera = 192;
  view_.cameraX = camera;
  view_.petX = static_cast<int16_t>(view_.worldX - camera);
}

void PetApp::projectGroundPosition() {
  const GroundPose pose = groundPose(view_.petDepth, stage(state_));
  view_.petDepth = pose.depth;
  view_.petY = pose.centerY;
  view_.petScale = pose.scale;
}

void PetApp::motion() {
  if(view_.page==Page::Companion || companionService_.ownsMotion()) return;
  Motion sample;
  if (!board_.motion(sample)) return;
  view_.imuReady = true;
  const uint32_t now = sample.timestampMs;
  if (previousMotionValid_) {
    const int32_t jerk = abs(static_cast<int32_t>(sample.ax) - previousMotion_.ax) +
                         abs(static_cast<int32_t>(sample.ay) - previousMotion_.ay) +
                         abs(static_cast<int32_t>(sample.az) - previousMotion_.az);
    const int32_t spin = abs(static_cast<int32_t>(sample.gx)) +
                         abs(static_cast<int32_t>(sample.gy)) +
                         abs(static_cast<int32_t>(sample.gz));
    if (jerk > 1800 || spin > 700000) {
      if (now - shakeWindowStart_ > 700U) {
        shakeWindowStart_ = now;
        shakeHits_ = 0;
      }
      if (shakeHits_ < 8) ++shakeHits_;
      if (shakeHits_ >= 3) {
        triggerDizzy();
        shakeHits_ = 0;
      }
    } else if (now - shakeWindowStart_ > 700U) {
      shakeHits_ = 0;
    }
  }
  previousMotion_ = sample;
  previousMotionValid_ = true;
  if (now - lastMotionDebugMs_ >= 1000U) {
    lastMotionDebugMs_ = now;
    if (Serial) {
      Serial.printf("IMU ax=%d ay=%d az=%d gx=%ld gy=%ld gz=%ld page=%u worldX=%d depth=%d\r\n",
                    sample.ax, sample.ay, sample.az,
                    static_cast<long>(sample.gx), static_cast<long>(sample.gy),
                    static_cast<long>(sample.gz), static_cast<unsigned>(view_.page),
                    view_.worldX, view_.petDepth);
    }
  }

  const int32_t horizontalTilt = static_cast<int32_t>(sample.ax) * PET_IMU_HORIZONTAL_SIGN;
  if (view_.page == Page::Game && view_.sleepTransition == 0 &&
      now - lastGameTiltMs_ >= 180U) {
    if (horizontalTilt < -500 && view_.gameLane > 0) {
      --view_.gameLane;
      lastGameTiltMs_ = now;
    } else if (horizontalTilt > 500 && view_.gameLane < 2) {
      ++view_.gameLane;
      lastGameTiltMs_ = now;
    }
  }
  if (view_.page != Page::Home || view_.scene != Scene::Room || pressed_ ||
      view_.sleepTransition != 0 || state_.sleeping ||
      isNight(state_) || now < view_.dizzyUntilMs) return;
  const uint32_t dt = now - lastMotionMs_;
  lastMotionMs_ = now;
  const int32_t tiltX = static_cast<int32_t>(sample.ax) * PET_IMU_HORIZONTAL_SIGN;
  const int32_t tiltY = static_cast<int32_t>(sample.ay) * PET_IMU_VERTICAL_SIGN;
  if (abs(tiltX) > 100 || abs(tiltY) > 100) {
    view_.autoMoving = false;
    lastInput_ = now;
    const int16_t deltaX = groundStep(tiltX, dt, motionMoveAccumulator_,
                                      kPetHorizontalMotionDivisor);
    const int16_t deltaY = groundStep(tiltY, dt, motionVerticalAccumulator_,
                                      kPetDepthMotionDivisor);
    view_.worldX += deltaX;
    view_.petDepth += deltaY;
    if (deltaX != 0) view_.petFacing = deltaX >= 0 ? 1 : -1;
  } else if (view_.autoMoving || now - lastInput_ > 1800U) {
    view_.autoMoving = true;
    const uint32_t phase = (now / 70U) % 256U;
    const int16_t targetX = phase < 128U
        ? static_cast<int16_t>(24 + phase * 272U / 127U)
        : static_cast<int16_t>(296 - (phase - 128U) * 272U / 127U);
    const uint32_t verticalPhase = (phase + 64U) % 256U;
    const int16_t targetDepth = verticalPhase < 128U
        ? static_cast<int16_t>(10 + verticalPhase * 80U / 127U)
        : static_cast<int16_t>(90 - (verticalPhase - 128U) * 80U / 127U);
    if (view_.worldX < targetX) { ++view_.worldX; view_.petFacing = 1; }
    if (view_.worldX > targetX) { --view_.worldX; view_.petFacing = -1; }
    if (view_.petDepth < targetDepth) ++view_.petDepth;
    if (view_.petDepth > targetDepth) --view_.petDepth;
  }
  updateWorldPosition();
  projectGroundPosition();
}

void PetApp::tap(uint8_t x, uint8_t y, uint32_t heldMs, int dx, int dy) {
  if(view_.page==Page::Companion) {
    if(maxMove_<15) companionTap(x,y,heldMs);
    return;
  }
  if (heldMs > 5000 || view_.sleepTransition != 0) return;
  if (view_.page == Page::Scene && y < 22 && x >= 96) {
    returnHome();
    return;
  }
  if (view_.page == Page::Scene) {
    taskTouch(x, y, dx, dy);
    return;
  }
  if (view_.page == Page::Home && view_.scene != Scene::Room) {
    itemTouch(x, y);
    return;
  }
  if (view_.page == Page::Home && view_.scene == Scene::Room) {
    const int worldTouch = x + view_.cameraX;
    if (state_.poopCount && worldTouch >= 69 && worldTouch <= 94 &&
        y >= 85 && y < 105 && maxMove_ < 12) {
      feedback(cleanWaste(state_));
      return;
    }
    if (roomObjectAt(worldTouch, y) == RoomObject::Lamp && maxMove_ < 12) {
      feedback(switchLight(state_));
      return;
    }
    if (state_.sleeping && roomObjectAt(worldTouch, y) == RoomObject::Bed &&
        (!state_.blanket || dy < -12)) {
      feedback(tuckIn(state_));
      return;
    }
  }
  if (view_.page == Page::Home && view_.scene == Scene::Room && maxMove_ < 12 &&
      !petHit(x, y) && roomObjectAt(x + view_.cameraX, y) == RoomObject::Bed) {
    action(Action::Sleep);
    return;
  }
  if (view_.page == Page::Home && heldMs >= 800 && maxMove_ < 12) {
    if (petHit(x, y)) {
      toast("SECRET HUG!", Reaction::Heart);
      view_.secretEffect = 1;
    } else {
      view_.page = Page::Settings;
    }
    return;
  }
  if (view_.page != Page::Game && abs(dx) >= 30 && abs(dx) > abs(dy)) {
    if (view_.page == Page::Memories) {
      view_.memoryIndex = (view_.memoryIndex + (dx < 0 ? 1 : kMilestoneCount - 1)) % kMilestoneCount;
    } else {
      view_.page = dx < 0 ? Page::Status : Page::Care;
    }
    return;
  }
  if (maxMove_ > 20) {
    if (view_.page == Page::Home && y >= 20 && y < 108) action(Action::Pet);
    return;
  }
  if (view_.page != Page::Home && y < 20) {
    returnHome();
    return;
  }
  switch (view_.page) {
    case Page::Home:
      if (y >= 108) view_.page = x < 64 ? Page::Care : Page::Status;
      else if (y < 20) {
        view_.page = x < 64 ? Page::Growth : Page::CareLog;
      } else if (!petHit(x, y) &&
                 roomObjectAt(x + view_.cameraX, y) != RoomObject::None) {
        itemTouch(x, y);
      } else {
        bool secretBoop = false;
        if (petHit(x, y)) {
          const uint32_t now = millis();
          if (now - boopWindowStart_ > 6000U) boopCount_ = 0;
          if (boopCount_ < 9) ++boopCount_;
          boopWindowStart_ = now;
          if (boopCount_ >= 3) {
            boopCount_ = 0;
            secretBoop = true;
          }
        }
        action(Action::Pet);
        if (petHit(x, y)) {
          if (secretBoop) {
            toast("BOOP BOOP! SECRET!", Reaction::Heart);
            view_.secretEffect = 1;
          } else {
            toast("BOOP!", Reaction::Heart);
          }
        }
      }
      break;
    case Page::Care:
      if (y >= 110) {
        if (x < 64) action(Action::Nursery);
        else showScene(Scene::Clinic, 180000);
      } else if (y < 66) showScene(x < 64 ? Scene::Restaurant : Scene::Washroom, 180000);
      else if (x < 64) showScene(Scene::Park, 180000);
      else action(Action::Sleep);
      break;
    case Page::Status:
      if (y >= 110) view_.page = x < 64 ? Page::Growth : Page::CareLog;
      break;
    case Page::Growth:
      if (y >= 110) {
        if (x < 64) {
          const ActionResult grown = evolve(state_);
          feedback(grown);
          if (grown.changed) {
            returnHome();
            view_.secretEffect = 2;
            view_.reaction = Reaction::Heart;
          }
        } else view_.page = Page::CareLog;
      }
      break;
    case Page::CareLog:
      if (y >= 110) {
        if (x < 64) view_.page = Page::Memories;
        else showScene(Scene::Clinic, 180000);
      }
      break;
    case Page::Memories:
      if (y >= 104)
        view_.memoryIndex = (view_.memoryIndex + (x < 64 ? kMilestoneCount - 1 : 1)) % kMilestoneCount;
      else toast(eventText(state_), Reaction::Thinking);
      break;
    case Page::Settings:
      if (y >= 24 && y < 50) {
        state_.brightness = state_.brightness >= 90 ? 30 : state_.brightness + 20;
        board_.brightness(state_.brightness);
        dirty_ = true;
        if (!savePending_) savePending_ = millis() + 3000U;
      } else if (y >= 52 && y < 78) {
        view_.language = view_.language == Language::Chinese ? Language::English : Language::Chinese;
        const bool stored = store_.setLanguage(view_.language);
        toast(stored ? "LANGUAGE SAVED" : "LANGUAGE SAVE ERROR",
              stored ? Reaction::Happy : Reaction::Thinking);
      } else toast("SERIAL: TIME <EPOCH>", Reaction::Thinking);
      break;
    case Page::Game:
      view_.gameLane = x < 43 ? 0 : x < 85 ? 1 : 2;
      break;
    case Page::Companion:
    case Page::Scene:
      break;
  }
}

void PetApp::nextDrop() {
  gameRandom_ = gameRandom_ * 1664525U + 1013904223U;
  view_.dropLane = (gameRandom_ >> 16U) % 3U;
  view_.dropY = 25;
  dropStart_ = millis();
}
void PetApp::startGame() {
  if (state_.nursery || state_.sleeping || isNight(state_)) {
    toast("TIME TO REST", Reaction::Sleeping);
    return;
  }
  view_.page = Page::Game;
  view_.scene = Scene::Outdoor;
  view_.sceneUntilMs = 0;
  view_.gameLane = 1;
  view_.gameScore = 0;
  view_.gameRemaining = 20;
  gameRandom_ = state_.randomSeed ^ millis();
  gameStart_ = millis();
  nextDrop();
  view_.toast = nullptr;
}
void PetApp::game() {
  if (view_.page != Page::Game) return;
  const uint32_t elapsed = millis() - gameStart_;
  if (elapsed >= 20000) {
    const uint8_t score = view_.gameScore;
    returnHome();
    action(Action::Play, score);
    return;
  }
  view_.gameRemaining = static_cast<uint8_t>((20000U - elapsed + 999U) / 1000U);
  const uint32_t fall = millis() - dropStart_;
  view_.dropY = static_cast<int16_t>(25U + (fall > 750 ? 750 : fall) * 76U / 750U);
  if (fall >= 750) {
    if (view_.dropLane == view_.gameLane && view_.gameScore < 30) ++view_.gameScore;
    nextDrop();
  }
}

void PetApp::serial() {
  // 每帧限制读取量，串口持续输入不能饿死触摸与动画。
  for (uint8_t i = 0; i < 64 && Serial.available(); ++i) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      if (!serialOverflow_ && serialUsed_) {
        serialLine_[serialUsed_] = '\0';
        command(serialLine_);
      } else if (serialOverflow_) Serial.println("Command too long.");
      serialUsed_ = 0;
      serialOverflow_ = false;
    } else if (serialUsed_ < sizeof(serialLine_) - 1) {
      serialLine_[serialUsed_++] = c;
    } else serialOverflow_ = true;
  }
}
void PetApp::command(const char* line) {
  if (strncmp(line, "TIME ", 5) == 0) {
    const char* digits = line + 5;
    char* end = nullptr;
    const unsigned long long raw = strtoull(digits, &end, 10);
    if (*digits < '0' || *digits > '9' || !end || *end || raw >= 4102444800ULL ||
        !synchronizeClock(state_, static_cast<uint32_t>(raw))) {
      Serial.println("Invalid time or backward clock; expected UTC Unix seconds (2024-2099).");
      return;
    }
    clockKnown_ = true;
    timeval tv{};
    tv.tv_sec = static_cast<time_t>(raw);
    settimeofday(&tv, nullptr);
    dirty_ = true;
    save();
    wake();
    toast("CLOCK SET!", Reaction::Happy);
  } else if (strcmp(line, "SAVE") == 0) {
    Serial.println(save() ? "SAVED" : "SAVE FAILED");
  } else if (strcmp(line, "STATUS") == 0) {
    Serial.printf("day=%lu active=%lu stage=%s nature=%s food=%u joy=%u rest=%u clean=%u bond=%u coins=%lu clock=%s battery=%umV imu=%s save=%s\n",
        static_cast<unsigned long>(day(state_)), static_cast<unsigned long>(state_.activeDays),
        stageName(stage(state_)), natureName(nature(state_)),
        percent(state_.hunger), percent(state_.joy), percent(state_.energy), percent(state_.hygiene),
        percent(state_.bond), static_cast<unsigned long>(state_.coins),
        clockKnown_ ? "known" : "unknown", board_.batteryMv(),
        board_.imuReady() ? "ready" : "missing", view_.saveError ? "error" : "ok");
  } else {
    Serial.println("STATUS | TIME <UTC Unix seconds> | SAVE | HELP");
  }
}

void PetApp::update() {
  if (!ready_) { delay(100); return; }
  tick();
  serial();
  input();
  motion();
  companion();
  updateSleepWalk();
  updateScene();
  game();
  const uint32_t now = millis();
  if (view_.toast && static_cast<int32_t>(now - toastUntil_) >= 0) {
    view_.toast = nullptr;
    view_.secretEffect = 0;
    if (static_cast<int32_t>(now - view_.dizzyUntilMs) >= 0)
      view_.reaction = Reaction::Idle;
  }
  if (view_.reaction == Reaction::Dizzy &&
      static_cast<int32_t>(now - view_.dizzyUntilMs) >= 0)
    view_.reaction = Reaction::Idle;
  if (view_.page == Page::Home && view_.scene == Scene::Room && !pressed_ &&
      view_.toast == nullptr && now - lastGreetingMs_ >= 60000U) {
    lastGreetingMs_ = now;
    toast(greetingText(state_, now), Reaction::Happy);
  }
  if (view_.page == Page::Home && view_.scene == Scene::Room && !pressed_ &&
      view_.toast == nullptr && now - lastTipMs_ >= 90000U) {
    static const char* const tips[] = {
        "CONNECT T-BAO-S31 TO PLAY TOGETHER",
        "HOLD EMPTY SPACE FOR SETTINGS",
        "TILT GENTLY TO MOVE",
        "TAP BAO TO LINK"};
    lastTipMs_ = now;
    toast(tips[(now / 90000U) % 4U], Reaction::Thinking);
  }
  if (dirty_ && storageReady_ && !driveRequested_ &&
      ((savePending_ && static_cast<int32_t>(now - savePending_) >= 0) ||
       now - lastSave_ >= kPetCheckpointMs)) save();
  if (screenOn_ && !pressed_ && view_.page != Page::Game &&
      view_.page != Page::Scene) {
    if (now - lastInput_ >= kPetScreenOffMs) {
      if (dirty_ && storageReady_ && now - lastSave_ >= 3000) save();
      if(view_.page==Page::Companion) closeCompanion();
      board_.screen(false);
      screenOn_ = false;
      view_.page = Page::Home;
    } else if (!dimmed_ && now - lastInput_ >= kPetDimMs) {
      board_.brightness(state_.brightness / 3U);
      dimmed_ = true;
    }
  }
  if (screenOn_) {
    if (now - lastPower_ >= 10000) { board_.samplePower(); lastPower_ = now; }
    if (now - lastFrame_ >= kPetFrameMs) {
      view_.animationMs = now;
      view_.batteryMv = board_.batteryMv();
      view_.charging = board_.charging();
      view_.clockKnown = clockKnown_;
      renderer_.render(state_, view_, pixels_);
      board_.present(pixels_);
      lastFrame_ = now;
    }
    delay(3);
  } else board_.idleSleep();
}
}  // namespace pet
