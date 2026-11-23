# LILYGO Pet Ring

A pixel-pet firmware for T-QT-C6. The pet can be fed, bathed, played with, put to sleep, and raised over time. It can also pair with and drive a T-Bao-S31 rover over ESP-NOW.

[中文 README](README.md)

## Build and upload

Run from the repository root:

```bash
pio run -e Lilygo_Pet_Ring
pio run -e Lilygo_Pet_Ring -t upload
pio device monitor -b 115200
```

Target configuration: ESP32-C6, 4 MB Flash, and USB CDC. The default display is GC9107 with Battery V1.2.

When Battery V1.2 detects SGM41562 with chip ID `0x04`, the firmware applies the charging setup used by the `Lvgl_CIT_SGM41562` demo. Check `libraries/common/pin_config.h` before using another battery or PMU variant.

## Pet gameplay

| Entry | Usage |
| --- | --- |
| Room | Tap or swipe the pet; tap Baozai near the bookshelf to open the rover link |
| `CARE` | `FEED`, `CLEAN`, `PLAY`, and `SLEEP` |
| `INFO` | View status, growth requirements, care logs, and memories |
| `GROW` | Tap `EVOLVE` when the requirements are complete |
| Park | Tap `RUN` for the dew game or `LINK` for the rover |
| Settings | Long-press empty space to change brightness or switch English / Chinese |

- Complete bathing in this order: `SHOWER → SOAP → RUB → SHOWER → TOWEL`.
- Three completed bites count as one full meal.
- Tilt the ring gently to move the pet around the room.
- Tap the pet three times for `BOOP`; long-press it for a secret hug.
- The first touch after the display turns off only wakes the display.

## Rover interaction

### Open the companion page

There are two entrances:

1. Tap Baozai near the bookshelf in the room.
2. Open the park and tap `LINK`.

All pairing and control are handled in `COMPANION`. No router or phone is required.

### Pair for the first time

1. Power on the ring and the T-Bao-S31 rover, then open `COMPANION`.
2. The ring searches automatically and displays a six-digit pairing code.
3. Compare the code shown on both devices.
4. Confirm the pairing once on each device.
5. The pairing is stored in the separate `petlink` NVS namespace and does not delete the pet save.

Both devices must run compatible `PetLink v3` firmware. If the UI shows `UPDATE BOTH DEVICES`, update the ring and rover together.

### Start and stop driving

1. Make sure the rover is online and has no fault.
2. Tap `START`.
3. The current wrist pose is captured as the zero point for this driving session. Wait for `SETTING CURRENT POSE` / `STARTING` to finish.
4. Tilt the ring to control forward, reverse, and steering.
5. Tap `STOP` to stop and revoke driving permission.

Every `START` captures a new zero point. After a stop, page exit, disconnect, persistent IMU failure, or rover fault, driving does not resume automatically; tap `START` again.

If the UI shows `INPUT PAUSED. KEEP STEADY.`, hold the ring steady. Driving can resume when reliable samples return; persistent bad input causes an automatic stop.

### Two-pet interactions

Open the `...` menu for:

- `WAVE` — wave.
- `RUN` — screen-only chase animation.
- `HUG` — hug.
- `SECRET` — two-character dialogue.
- `FIVE` — high five.
- `PET` — pet the other character.

An interaction stops an active drive before it starts. Companion `RUN` is only an animation and never starts the physical rover. Completing companion play can count as one `PLAY` action.

### Recalibrate and unpair

- `... → RECALIBRATE` stops the rover and recalibrates without deleting the pairing.
- `... → UNPAIR → UNPAIR` confirms unpairing. When online, both devices clear the pairing; offline unpairing may need to be repeated on the other device.
- Unpairing does not delete the pet save, language setting, or Wi-Fi configuration.

### Safety

Test with the rover wheels lifted first. Verify stop, pairing, zero capture, and low-speed control before putting it on the floor. The rover has no drop protection, obstacle avoidance, precise odometry, or automatic return function.

## Serial commands

Serial speed is `115200`; terminate each command with a newline:

```text
STATUS
SAVE
TIME <UTC Unix seconds>
HELP
```

`STATUS` reports IMU, save, and link state. `TIME` expects UTC Unix seconds, not a formatted date.

## Related documentation

- [`../../libraries/PetLink/README.md`](../../libraries/PetLink/README.md) — PetLink protocol and rover synchronization.
- [`../../libraries/common/pin_config.h`](../../libraries/common/pin_config.h) — shared pins and hardware variants.
