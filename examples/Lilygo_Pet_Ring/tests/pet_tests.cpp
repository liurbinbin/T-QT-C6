#include "../PetModel.h"
#include "../PetClock.h"
#include "../SaveCodec.h"
#include "../PixelRenderer.h"
#include "../ImuSample.h"
#include "../GroundMotion.h"
#include "../GroundGeometry.h"
#include "../PetConfig.h"
#include "../RoomLayout.h"
#include "../PetLocale.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <initializer_list>

namespace {
int checks = 0;
void check(bool condition, const char* expression, int line) {
  ++checks;
  if (!condition) {
    std::fprintf(stderr, "FAIL line %d: %s\n", line, expression);
    std::exit(1);
  }
}
#define CHECK(expression) check((expression), #expression, __LINE__)
using namespace pet;

void initialState() {
  State s = newState(42);
  CHECK(valid(s));
  CHECK(day(s) == 1);
  CHECK(stage(s) == Stage::Seed);
  CHECK(nature(s) == Nature::Undecided);
  CHECK(!isNight(s));
  CHECK(percent(10000) == 100);
  CHECK(percent(99999) == 100);
  CHECK(act(s, Action::Pet).changed);
  CHECK(s.activeDays == 1);
  CHECK(s.claimedMilestones == 1);
  CHECK(!act(s, Action::Pet).changed);
  advance(s, 8);
  CHECK(act(s, Action::Pet).changed);
  CHECK(s.activeDays == 1);
  advance(s, 592);
  CHECK(stage(s) == Stage::Seed);
  CHECK(growthProgress(s).ready);
  CHECK(evolve(s).changed);
  CHECK(stage(s) == Stage::Baby);
  CHECK(valid(s));
}
void chunkInvariant() {
  State a = newState(1234);
  State b = a;
  advance(a, 3 * kDay + 133);
  for (unsigned i = 0; i < 3 * kDay + 133; ++i) advance(b, 1);
  CHECK(std::memcmp(&a, &b, sizeof(a)) == 0);
  CHECK(a.hunger <= kStatMax);
  CHECK(a.joy <= kStatMax);
  CHECK(a.hygiene >= 1000);
  CHECK(a.energy >= 1000);
  CHECK(valid(a));
}
void longTerm() {
  State s = newState(98);
  for (unsigned d = 1; d <= 120; ++d) {
    CHECK(day(s) == d);
    CHECK(act(s, Action::Feed).changed);
    CHECK(act(s, Action::Clean).changed);
    CHECK(act(s, Action::Pet).changed);
    CHECK(act(s, Action::Play, 15).changed);
    CHECK(s.activeDays == d);
    if (growthProgress(s).ready) CHECK(evolve(s).changed);
    CHECK(valid(s));
    if (d == 4) CHECK(stage(s) == Stage::Toddler);
    if (d == 15) CHECK(stage(s) == Stage::Teen);
    if (d == 31) CHECK(stage(s) == Stage::Adult);
    if (d == 61) CHECK(stage(s) == Stage::Companion);
    if (d == 100) CHECK(stage(s) == Stage::Centennial);
    advance(s, kDay);
  }
  CHECK(s.claimedMilestones == 255);
  CHECK(s.bond <= kStatMax);
  CHECK(s.coins <= 9999);
  CHECK(s.ageSeconds == 120 * kDay);
  CHECK(nature(s) == Nature::Clever);
  const Nature locked = nature(s);
  for (unsigned i = 0; i < 200; ++i) {
    act(s, Action::Pet);
    advance(s, 8);
  }
  CHECK(nature(s) == locked);
  CHECK(valid(s));
  State absent = newState(18);
  advance(absent, 120 * kDay);
  CHECK(absent.claimedMilestones == 0);
  CHECK(absent.activeDays == 0);
  CHECK(stage(absent) != Stage::Centennial);
  CHECK(valid(absent));
}
void capsAndRest() {
  State s = newState(7);
  for (unsigned i = 0; i < 20; ++i) {
    act(s, Action::Pet);
    advance(s, 8);
  }
  CHECK(s.dailyRewards == 12);
  const unsigned bond = s.bond;
  CHECK(act(s, Action::Pet).changed);
  CHECK(s.bond == bond);
  advance(s, kDay);
  CHECK(s.dailyRewards == 0);
  CHECK(act(s, Action::Pet).rewarded);
  CHECK(s.activeDays == 2);
  CHECK(act(s, Action::Nursery).changed);
  CHECK(!act(s, Action::Feed).changed);
  advance(s, 5 * kDay);
  CHECK(s.hunger == kStatMax);
  CHECK(s.energy == kStatMax);
  CHECK(s.activeDays == 2);
  CHECK(act(s, Action::Nursery).changed);
  CHECK(act(s, Action::Sleep).changed);
  CHECK(!act(s, Action::Play).changed);
  advance(s, 2);
  CHECK(act(s, Action::Sleep).changed);
  CHECK(!s.sleeping);
  CHECK(valid(s));
}
void persistence() {
  State s = newState(46);
  act(s, Action::Feed);
  const SaveRecord original = makeRecord(s, UINT_MAX);
  CHECK(validRecord(original));
  for (size_t i = 0; i < sizeof(SaveRecord); ++i) {
    SaveRecord damaged = original;
    reinterpret_cast<unsigned char*>(&damaged)[i] ^= 0x01;
    CHECK(!validRecord(damaged));
  }
  SaveRecord invalidVersion = original;
  invalidVersion.version = 999;
  CHECK(!validRecord(invalidVersion));
  CHECK(newerSequence(0, UINT_MAX));
  CHECK(!newerSequence(UINT_MAX, 0));
  CHECK(!newerSequence(1, 1));
  s.hunger = 10001;
  CHECK(!valid(s));
  CHECK(!validRecord(makeRecord(s, 10)));
}
void clockRecovery() {
  State s = newState(88);
  constexpr uint32_t epoch = 1795000000;
  CHECK(synchronizeClock(s, epoch));
  CHECK(s.ageSeconds == 0);
  elapse(s, 120, true);
  CHECK(s.lastEpoch == epoch + 120);
  s.clockTrusted = 0;
  elapse(s, 60, false);
  CHECK(s.untrustedSeconds == 60);
  CHECK(synchronizeClock(s, epoch + 120 + kDay + 60));
  CHECK(s.ageSeconds == 180 + kDay);
  CHECK(s.untrustedSeconds == 0);
  CHECK(s.clockTrusted == 1);
  const State before = s;
  CHECK(!synchronizeClock(s, epoch));
  CHECK(std::memcmp(&s, &before, sizeof(s)) == 0);
  CHECK(!synchronizeClock(s, 0));
  CHECK(!synchronizeClock(s, UINT_MAX));
  CHECK(valid(s));
  // 未知时钟下保存并再次启动，已累计的在线时间仍不能被重复补算。
  s.clockTrusted = 0;
  elapse(s, 100, false);
  const SaveRecord record = makeRecord(s, 1);
  s = record.state;
  elapse(s, 50, false);
  const uint32_t age = s.ageSeconds;
  CHECK(synchronizeClock(s, before.lastEpoch + 200));
  CHECK(s.ageSeconds == age + 50);
  CHECK(valid(s));
}
void growthBranches() {
  for (unsigned food = 0; food < 3; ++food) {
    State s = newState(70 + food);
    s.ageSeconds = 30U * kDay;
    s.remainderSeconds = 0;
    s.activeDays = 8;
    s.cleanCount = 5;
    s.playCount = 5;
    for (unsigned meal = 0; meal < 8; ++meal) {
      s.hunger = 5000;
      CHECK(act(s, Action::Feed, static_cast<uint16_t>(food)).changed);
      advance(s, 30);
    }
    while (stage(s) != Stage::Adult) CHECK(evolve(s).changed);
    CHECK(static_cast<unsigned>(form(s)) == food + 1U);
    const Form locked = form(s);
    s.leafAffinity = s.dewAffinity = s.fruitAffinity = 9999;
    CHECK(form(s) == locked);
    CHECK(valid(s));
  }
  State waiting = newState(7);
  advance(waiting, 120U * kDay, false);
  CHECK(stage(waiting) == Stage::Seed);
  CHECK(evolve(waiting).changed);
  CHECK(stage(waiting) == Stage::Baby);
  CHECK(!growthProgress(waiting).ready);
  CHECK(!evolve(waiting).changed);
  State quality = newState(8);
  advance(quality, 14U * kDay, false);
  quality.activeDays = 5;
  quality.feedCount = 5;
  quality.playCount = 3;
  quality.cleanCount = 3;
  CHECK(evolve(quality).changed && quality.careStyle == 1);
  quality.careMisses = 2;
  CHECK(evolve(quality).changed && quality.careStyle == 2);
  quality.careMisses = 6;
  CHECK(evolve(quality).changed && quality.careStyle == 3);
  CHECK(quality.stageMissBaseline == 6);
  CHECK(valid(quality));
}

void careNeeds() {
  State s = newState(100);
  advance(s, 600);
  s.hunger = 0;
  advance(s, 60);
  CHECK((s.callMask & 1U) != 0);
  advance(s, 14 * 60);
  CHECK(s.careMisses == 0);
  advance(s, 60);
  CHECK(s.careMisses == 1);
  advance(s, 10 * 60);
  CHECK(s.careMisses == 1);
  CHECK(act(s, Action::Feed).changed);
  CHECK((s.callMask & 1U) == 0);
  CHECK(s.timelyResponses == 0);
  advance(s, 30);
  s.hunger = 0;
  advance(s, 60);
  CHECK(act(s, Action::Feed).changed);
  CHECK(s.timelyResponses == 1);
  State absent = newState(101);
  absent.hunger = absent.joy = 0;
  advance(absent, 5 * kDay, false);
  CHECK(absent.careMisses == 0 && absent.callMask == 0);
  State nursery = newState(102);
  nursery.nursery = 1;
  nursery.hunger = nursery.joy = 0;
  advance(nursery, kDay);
  CHECK(nursery.careMisses == 0);
  CHECK(valid(s) && valid(absent) && valid(nursery));
}

void healthAndSleep() {
  State s = newState(103);
  s.hunger = 9500;
  CHECK(act(s, Action::Feed).changed);
  CHECK(!act(s, Action::Feed).changed);
  advance(s, 30);
  CHECK(act(s, Action::Feed).changed);
  advance(s, 30);
  CHECK(act(s, Action::Feed).changed);
  CHECK((s.illness & Illness::Overfed) != 0);
  CHECK(s.feedCount == 0 && s.leafAffinity == 0);
  CHECK(treat(s).changed);
  CHECK(!treat(s).changed);
  s.hunger = 8000;
  CHECK(treat(s).changed && s.illness == 0);
  s.illness = Illness::Dirty;
  s.hygiene = 1000;
  s.poopCount = 3;
  CHECK(treat(s).changed);
  CHECK(!treat(s).changed);
  CHECK(cleanWaste(s).changed);
  CHECK(cleanWaste(s).changed);
  CHECK(cleanWaste(s).changed);
  CHECK(act(s, Action::Clean).changed);
  CHECK(treat(s).changed && s.illness == 0);
  CHECK(act(s, Action::Sleep).changed);
  CHECK(!s.blanket && !s.lightOff);
  CHECK(tuckIn(s).changed && s.blanket);
  CHECK(switchLight(s).changed && s.lightOff);
  CHECK(!tuckIn(s).changed);
  CHECK(valid(s));
  State capped = newState(105);
  capped.sleeping = 1;
  capped.unlitMinutes = 1440;
  capped.joy = 5000;
  advance(capped, 60);
  CHECK(capped.unlitMinutes == 1440);
  CHECK(capped.joy == 4999);
  CHECK(valid(capped));
}

void legacyMigration() {
  State oldState = newState(104);
  oldState.ageSeconds = 40 * kDay;
  oldState.remainderSeconds = 0;
  oldState.coins = 123;
  oldState.feedCount = 50;
  oldState.adultNature = static_cast<uint32_t>(Nature::Clever);
  LegacySaveRecord legacy{};
  legacy.magic = 0x59415941U;
  legacy.version = 1;
  legacy.sequence = 45;
  legacy.stateBytes = kLegacyStateBytes;
  std::memcpy(legacy.state, &oldState, kLegacyStateBytes);
  legacy.crc = checksum(&legacy, offsetof(LegacySaveRecord, crc));
  SaveRecord current{};
  CHECK(decodeRecord(&legacy, sizeof(legacy), current));
  CHECK(current.version == 2 && current.sequence == 45);
  CHECK(current.state.ageSeconds == oldState.ageSeconds);
  CHECK(current.state.coins == 123 && current.state.feedCount == 50);
  CHECK(stage(current.state) == Stage::Adult);
  CHECK(nature(current.state) == Nature::Clever);
  CHECK(validRecord(current));
  CHECK(!decodeRecord(&legacy, sizeof(legacy) - 1, current));
  legacy.state[0] ^= 1;
  CHECK(!decodeRecord(&legacy, sizeof(legacy), current));
}

void localization() {
  CHECK(std::strcmp(translate("CARE", Language::Chinese), "照顾") == 0);
  CHECK(std::strcmp(translate("CARE", Language::English), "CARE") == 0);
  CHECK(std::strcmp(translate("unknown-key", Language::Chinese), "unknown-key") == 0);
  CHECK(chineseGlyph(0x4E2DU) != nullptr);
  CHECK(utf8Length("中文AB") == 4);
  CHECK(utf8Width("中文AB") == 32);
  const char* input = "中文AB";
  CHECK(nextCodepoint(input) == 0x4E2DU);
  CHECK(nextCodepoint(input) == 0x6587U);
  CHECK(nextCodepoint(input) == 'A');
  CHECK(nextCodepoint(input) == 'B');
  CHECK(nextCodepoint(input) == 0);
  char line[16];
  const char* next = fitTextLine("中文AB", line, sizeof(line), 20);
  CHECK(std::strcmp(line, "中文") == 0);
  CHECK(std::strcmp(next, "AB") == 0);
  fitTextLine(next, line, sizeof(line), 12);
  CHECK(std::strcmp(line, "AB") == 0);
  char small[3];
  fitTextLine("中", small, sizeof(small), 20);
  CHECK(small[0] == '\0');
  const char* malformed = "\xE4\xB8";
  CHECK(nextCodepoint(malformed) == '?');
  CHECK(nextCodepoint(malformed) == '?');
  CHECK(nextCodepoint(malformed) == 0);
}

void motionSamples() {
  const uint8_t bytes[] = {
      0x64, 0x00, 0x38, 0xFF, 0x2C, 0x01,
      0xE8, 0x03, 0x30, 0xF8, 0xD0, 0x07};
  const Motion sample = decodeImuSample(bytes, 1234);
  CHECK(sample.gx == 7000);
  CHECK(sample.gy == -14000);
  CHECK(sample.gz == 21000);
  CHECK(sample.ax == 488);
  CHECK(sample.ay == -976);
  CHECK(sample.az == 976);
  CHECK(sample.timestampMs == 1234);
  const uint8_t minimum[] = {0x00, 0x80};
  const uint8_t maximum[] = {0xFF, 0x7F};
  CHECK(imuSignedWord(minimum) == -32768);
  CHECK(imuSignedWord(maximum) == 32767);

  int32_t remainder = 0;
  int position = 0;
  for (unsigned i = 0; i < 40; ++i)
    position += groundStep(500, 25, remainder, kPetHorizontalMotionDivisor);
  CHECK(position == 50);
  CHECK(remainder == 0);
  int32_t otherRemainder = 0;
  int otherPosition = 0;
  for (unsigned i = 0; i < 20; ++i)
    otherPosition += groundStep(500, 50, otherRemainder, kPetHorizontalMotionDivisor);
  CHECK(otherPosition == position);
  CHECK(groundStep(99, 25, remainder, kPetHorizontalMotionDivisor) == 0);
  CHECK(groundStep(-500, 100, remainder, kPetHorizontalMotionDivisor) == -5);
  CHECK(groundStep(500, 10000, remainder, kPetHorizontalMotionDivisor) == 5);
  remainder = 0;
  CHECK(groundStep(0, 25, remainder, kPetDepthMotionDivisor) == 0);
  CHECK(groundStep(500, 100, remainder, kPetDepthMotionDivisor) > 0);
}

void groundBounds() {
  for (unsigned value = 0; value < 7; ++value) {
    const Stage growth = static_cast<Stage>(value);
    int previousFoot = 0;
    int previousScale = 0;
    for (int depth = 0; depth <= 100; ++depth) {
      const GroundPose pose = groundPose(depth, growth);
      CHECK(pose.depth == depth);
      CHECK(pose.footY >= kGroundBackY);
      CHECK(pose.footY <= kGroundFrontY);
      CHECK(pose.centerY + petFootOffset(growth) * pose.scale / 100 == pose.footY);
      CHECK(pose.scale >= 65 && pose.scale <= 100);
      CHECK(pose.footY >= previousFoot);
      CHECK(pose.scale >= previousScale);
      previousFoot = pose.footY;
      previousScale = pose.scale;
    }
    const GroundPose far = groundPose(-10000, growth);
    const GroundPose near = groundPose(10000, growth);
    CHECK(far.depth == 0 && far.footY == kGroundBackY);
    CHECK(near.depth == 100 && near.footY == kGroundFrontY);
  }
}

void roomLayout() {
  CHECK(roomObjectAt(0, 0) == RoomObject::None);
  CHECK(roomObjectAt(44, 40) == RoomObject::Window);
  CHECK(roomObjectAt(47, 90) == RoomObject::Plant);
  CHECK(roomObjectAt(80, 45) == RoomObject::Books);
  CHECK(roomObjectAt(114, 38) == RoomObject::Clock);
  CHECK(roomObjectAt(148, 52) == RoomObject::Kitchen);
  CHECK(roomObjectAt(188, 82) == RoomObject::Sofa);
  CHECK(roomObjectAt(216, 48) == RoomObject::Aquarium);
  CHECK(roomObjectAt(100, 94) == RoomObject::Toy);
  CHECK(roomObjectAt(20, 88) == RoomObject::Chest);
  CHECK(roomObjectAt(289, 50) == RoomObject::Lamp);
  CHECK(roomObjectAt(256, 78) == RoomObject::Bed);
  CHECK(roomObjectAt(313, 60) == RoomObject::GardenDoor);
  CHECK(roomObjectAt(72, 76) == RoomObject::Baozai);
}

void rendererSafety() {
  PixelRenderer renderer;
  static uint16_t guarded[128 * 128 + 2];
  for (unsigned d : {0U, 1U, 4U, 15U, 31U, 61U, 100U, 120U}) {
    State s = newState(d);
    advance(s, d * kDay);
    s.activeDays = 30;
    initializeProgress(s, true);
    if (d == 4) s.growthStage = static_cast<uint32_t>(Stage::Toddler);
    s.claimedMilestones = 255;
    for (unsigned page = 0; page <= static_cast<unsigned>(Page::Companion); ++page) {
      for (unsigned reaction = 0; reaction < 8; ++reaction) {
        s.adultForm = reaction % 4;
        s.careStyle = reaction % 4;
        guarded[0] = 0x1234;
        guarded[128 * 128 + 1] = 0x5678;
        View view;
        view.page = static_cast<Page>(page);
        view.linkPhase = static_cast<uint8_t>(reaction);
        view.linkBound = reaction >= 3;
        view.linkConnected = reaction >= 4;
        view.linkCode = 123456;
        view.driveRequested = reaction == 5;
        view.carBattery = reaction == 5 ? 78 : -1;
        view.reaction = static_cast<Reaction>(reaction);
        view.animationMs = d * 333 + reaction * 127;
        view.memoryIndex = d % kMilestoneCount;
        view.toast = "HELLO LITTLE FRIEND!";
        view.dropY = reaction * 23 - 20;
        view.gameRemaining = 20;
        view.batteryMv = d % 2 ? 3850 : 0;
        for (Language language : {Language::Chinese, Language::English}) {
          view.language = language;
          renderer.render(s, view, guarded + 1);
          CHECK(guarded[0] == 0x1234);
          CHECK(guarded[128 * 128 + 1] == 0x5678);
        }
      }
    }
    const Scene scenes[] = {Scene::Restaurant, Scene::Washroom, Scene::Park, Scene::Outdoor, Scene::Clinic};
    for (unsigned scene = 0; scene < 5; ++scene) {
      View view;
      view.scene = scenes[scene];
      view.page = scenes[scene] == Scene::Outdoor ? Page::Game : Page::Scene;
      view.reaction = scene == 1 ? Reaction::Washing : Reaction::Eating;
      view.petX = scene == 0 ? 64 : 92;
      view.petY = scene == 0 ? 74 : 72;
      view.dropY = 70;
      view.gameRemaining = 10;
      view.toast = "SCENE TEST";
      guarded[0] = 0x1234;
      guarded[128 * 128 + 1] = 0x5678;
      renderer.render(s, view, guarded + 1);
      CHECK(guarded[0] == 0x1234);
      CHECK(guarded[128 * 128 + 1] == 0x5678);
    }
  }
  State s = newState(123);
  advance(s, 600);
  uint32_t previousImage = 0;
  for (unsigned camera = 0; camera <= 192; camera += 32) {
    View view;
    view.cameraX = static_cast<int16_t>(camera);
    view.petScale = 65 + camera % 36;
    guarded[0] = 0x1234;
    guarded[128 * 128 + 1] = 0x5678;
    renderer.render(s, view, guarded + 1);
    CHECK(guarded[0] == 0x1234);
    CHECK(guarded[128 * 128 + 1] == 0x5678);
    const uint32_t image = checksum(guarded + 1, 128 * 128 * sizeof(uint16_t));
    if (camera != 0) CHECK(image != previousImage);
    previousImage = image;
  }
}
void companionSafety() {
  CompanionScene scene;
  CompanionInput input;
  input.connected=true; input.generation=1;
  CHECK(scene.update(input,1000).mode==CompanionMode::Idle);
  CHECK(scene.update(input,1200).mode==CompanionMode::Welcome);
  CHECK(scene.update(input,2200).mode==CompanionMode::Idle);
  input.eventRevision=1; input.event=4; input.eventSeed=11; input.eventStarted=2300; input.eventResult=1;
  CHECK(scene.update(input,2400).mode==CompanionMode::Secret);
  CHECK(scene.update(input,2400).dialogue==11);
  input.eventResult=2;
  CHECK(scene.update(input,2500).mode==CompanionMode::Idle);
  input.driving=true;
  CHECK(scene.update(input,2600).mode==CompanionMode::Drive);
  input.driving=false; input.connected=false;
  CHECK(scene.update(input,2700).mode==CompanionMode::Waiting);
  scene.preview(2,2800,4);
  CHECK(scene.update(input,2900).mode==CompanionMode::Run);
  CHECK(scene.update(input,7300).mode==CompanionMode::Waiting);
  input.connected=true; input.generation=2;
  scene.update(input,7400);
  CHECK(scene.update(input,7600).mode==CompanionMode::Welcome);

  PixelRenderer renderer;
  static uint16_t pixels[260*128+2];
  State state=newState(11);
  for(unsigned stageIndex=0;stageIndex<7;++stageIndex) {
    state.growthStage=stageIndex;
    for(unsigned kind=1;kind<=6;++kind) {
      for(bool car : {false,true}) {
        CompanionScene animation;
        input=CompanionInput(); input.car=car; input.connected=true;
        animation.update(input,1);
        animation.preview(uint8_t(kind),2000,kind);
        for(uint32_t age : {0U,150U,800U,1800U,4200U,6000U}) {
          const CompanionFrame frame=animation.update(input,2000+age);
          for(unsigned formIndex=0;formIndex<4;++formIndex) {
            state.adultForm=formIndex; state.adultNature=formIndex;
            for(int width : {128,256}) {
              const int height=width==128 ? 59 : 112, stride=width+4;
              for(auto& pixel:pixels) pixel=0xCAFE;
              renderer.renderCompanionStage(state,frame,pixels+1,width,height,stride);
              CHECK(pixels[0]==0xCAFE);
              CHECK(pixels[1+height*stride]==0xCAFE);
              for(int y=0;y<height;++y)
                for(int x=width;x<stride;++x) CHECK(pixels[1+y*stride+x]==0xCAFE);
            }
          }
        }
      }
    }
  }
  static uint16_t ring[128*128+2];
  for(Language language : {Language::Chinese,Language::English})
    for(uint8_t menu=0;menu<4;++menu)
      for(uint8_t phase=0;phase<8;++phase) {
        View view; view.page=Page::Companion; view.language=language;
        view.companionMenu=menu; view.linkPhase=phase;
        view.linkCode=123456; view.linkMismatch=phase==7;
        ring[0]=0x1234; ring[128*128+1]=0x5678;
        renderer.render(state,view,ring+1);
        CHECK(ring[0]==0x1234 && ring[128*128+1]==0x5678);
      }
}
}  // namespace
int main(int argc, char** argv) {
  struct Test { const char* name; void (*run)(); };
  const Test tests[] = {
      {"initial", initialState}, {"time-chunks", chunkInvariant},
      {"growth", longTerm}, {"care", capsAndRest},
      {"save", persistence}, {"clock", clockRecovery}, {"render", rendererSafety},
      {"motion", motionSamples}, {"room", roomLayout}, {"ground", groundBounds},
      {"branches", growthBranches}, {"needs", careNeeds},
      {"health", healthAndSleep}, {"migration", legacyMigration}, {"locale", localization},
      {"companion", companionSafety}};
  bool matched = false;
  for (const Test& test : tests) {
    if (argc == 1 || std::strcmp(argv[1], test.name) == 0) {
      test.run();
      matched = true;
    }
  }
  if (!matched) {
    std::fprintf(stderr, "Unknown test. Use: initial time-chunks growth care save clock render motion room ground branches needs health migration locale companion\n");
    return 2;
  }
  std::printf("PASS: %d checks; 120-day growth, time recovery, CRC and renderer bounds.\n", checks);
  return 0;
}
