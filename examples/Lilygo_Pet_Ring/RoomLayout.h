#pragma once

#include <stdint.h>

namespace pet {

enum class RoomObject : uint8_t {
  None, Window, Plant, Books, Clock, Kitchen, Sofa, Aquarium,
  Toy, Chest, Lamp, Bed, GardenDoor, Baozai
};

struct RoomHotspot {
  RoomObject object;
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
};

inline RoomObject roomObjectAt(int worldX, int screenY) {
  static const RoomHotspot items[] = {
      {RoomObject::Window, 27, 26, 33, 30},
      {RoomObject::Plant, 40, 68, 20, 32},
      {RoomObject::Books, 72, 34, 29, 31},
      {RoomObject::Clock, 104, 28, 20, 21},
      {RoomObject::Kitchen, 132, 41, 38, 28},
      {RoomObject::Sofa, 172, 69, 32, 27},
      {RoomObject::Aquarium, 205, 38, 29, 26},
      {RoomObject::Toy, 91, 83, 18, 20},
      {RoomObject::Chest, 7, 79, 26, 22},
      {RoomObject::Lamp, 281, 41, 18, 48},
      {RoomObject::Bed, 244, 66, 34, 32},
      {RoomObject::GardenDoor, 302, 34, 18, 51},
      {RoomObject::Baozai, 59, 66, 27, 33}};
  for (const RoomHotspot& item : items) {
    if (worldX >= item.x && worldX < item.x + item.w &&
        screenY >= item.y && screenY < item.y + item.h)
      return item.object;
  }
  return RoomObject::None;
}

}  // namespace pet
