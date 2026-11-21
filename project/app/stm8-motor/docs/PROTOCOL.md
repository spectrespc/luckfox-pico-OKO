# I²C Protocol Specification — STM8S103 Motor Controller

> 🇷🇺 [Русская версия](PROTOCOL.ru.md)

## Bus Parameters
- Speed: Standard mode (100 kHz)
- Slave address: 0x60 (7-bit)
- Write address: 0xC0, Read address: 0xC1
- External pull-ups: 4.7 kΩ on SDA/SCL

## Transaction Format
- **Write-only**: `[START][0xC0][CMD][MOTOR][PARAM...][STOP]`
- **Write+Read**: `[START][0xC0][CMD][MOTOR][REPEATED START][0xC1][DATA...][!ACK][STOP]`

## Command Reference

| Cmd | Write payload | Read payload | Description |
|-------|-------------|--------------|-------------|
| 0x01 | [cmd][motor][steps_hi][steps_lo] | — | Move motor by N steps (int16 BE, two's complement) |
| 0x02 | [cmd][motor][spd_hi][spd_lo] | — | Set speed (uint16 BE, steps/sec, max 5000) |
| 0x03 | [cmd][motor] | — | Stop motor |
| 0x04 | [cmd][motor] | [pos_0..pos_3] | Request current position (int32 BE) |
| 0x05 | [cmd][motor] | [status] | Request status (0=IDLE, 1=RUNNING, 2=ERROR) |
| 0x06 | [cmd] | — | Software reset controller |
| 0x07 | [cmd] | — | Enter the I2C bootloader (OKO fork) |
| 0x08 | [cmd] | [role][maj][min][pat] | Get role (0=app, 1=boot) and version (shared with the bootloader) |
| 0x09 | [cmd][motor][pos_0..pos_3] | — | Force the logical position (int32 BE) without moving; ignored while RUNNING (2.0.2+) |
| 0x0A | [cmd] | [mode][state][wake_ms][0][ent_hi][ent_lo][w_i2c][w_timer] | Get the I2C-idle sleep state and counters (2.2.0+; 2.3.2: `wake_ms` granularity, `w_timer` timer wakes) |
| 0x0B | [cmd][mode][idle_s] | — | Enable/disable I2C-idle sleep; `mode` 0=off 1=on, `idle_s` 0=default 3 s (2.2.0+); no reply, read back 0x0A |
| 0xFF | [cmd] | [maj][min][pat] | Get firmware version (3 bytes; application only) |

## Detailed Command Examples

### Version Request
```bash
i2ctransfer -y 3 w1@0x60 0xFF r3
# Response: 0x02 0x02 0x00 (version 2.2.0; test board 2.3.2)
```

### Set Speed
```bash
# Set focus motor speed to 500 steps/sec
i2ctransfer -y 3 w4@0x60 0x02 0x00 0x01 0xF4
```

### Move Motor
```bash
# Move focus motor +100 steps
i2ctransfer -y 3 w4@0x60 0x01 0x00 0x00 0x64
```

### Get Position
```bash
# Get focus motor position
i2ctransfer -y 3 w2@0x60 0x04 0x00 r4
# Response: [pos_0..pos_3] (int32 BE)
```

### Get Status
```bash
# Get zoom motor status
i2ctransfer -y 3 w2@0x60 0x05 0x01 r1
# Response: 0x00 (IDLE), 0x01 (RUNNING) or 0x02 (ERROR)
```

### Stop Motor
```bash
# Stop zoom motor
i2ctransfer -y 3 w2@0x60 0x03 0x01
```

### Software Reset
```bash
# Reset controller
i2ctransfer -y 3 w1@0x60 0x06
```

### Set Position (zero the scale)
```bash
# Force the focus position to 0 without driving the motor
i2ctransfer -y 3 w6@0x60 0x09 0x00 0x00 0x00 0x00 0x00
# Verify the readback with 0x04; EEPROM persistence follows within ~1 s
```

**Known defect (KI-042):** on 2.0.2/2.1.1 a move that ends below zero leaves
the logical position with a corrupted top byte (`-1` reads as `0xFBFFFFFF`),
although the motor travels the commanded number of steps. Do not use a
negative `GET_POS` value for control; send `SET_POS(0)` after homing instead.

### Sleep control (0x0A / 0x0B)

```bash
# Enable sleep after 3 s of bus silence (idle_s = 0 selects the default)
i2ctransfer -y 3 w3@0x60 0x0B 0x01 0x00
# Read back: mode, state, wake granularity in ms, reserved, entries (BE),
#            wake by I2C, wake by timer
i2ctransfer -y 3 w1@0x60 0x0A r8
# Response: 0x01 0x00 0x01 0x00 0x00 0x2A 0x03 0xFF
```

`state`: 0 = awake, 1 = saving EEPROM, 2 = armed, 3 = asleep (reported while
the frame that woke the controller is being serviced).

`wake_ms` (byte 2) is the wake granularity in milliseconds: 1 (one TIM4 tick)
since 2.3.2.  In 2.2.0/2.3.0 the byte was reported in 10 ms units (`40` =
400 ms Active-halt slice), so a host that upgrades to 2.3.2 must switch units.

`w_timer` (byte 7) counts timer wakes, saturating at 255: TIM4 ticks in 2.3.2
(it saturates quickly and acts as "the sleep really ran" evidence), the AWU
slice in 2.2.0/2.3.0.

## Write-command answers (bootloader >= 1.1.0)

Write commands (`0x11`-`0x14`) answer with **two bytes**: `[status][command
echo]`.  The host must check the echo: a status from a previous command means
the frame never reached the bootloader.  Before 1.1.0 the answer was a single
byte and a lost frame looked like success (KI-075).

## Power management (I2C-idle sleep)

- Idle source: any completed frame.  After `idle_s` seconds of silence, with
  no motor RUNNING, no pending frame and a clean EEPROM, the application
  enters **Wait mode** (`WFI`) for one TIM4 tick.
- fMASTER keeps running in Wait mode: the 1 ms TIM4 interrupt wakes the core
  and its handler reloads the IWDG, while the I2C slave keeps answering, so
  the sleep never delays a host frame.  The core clock is gated between ticks,
  so the current is below Run but above Halt.
- Why not Active-halt: on the bench STM8S003F3P6 sample the AWU block does not
  count (the `AWUEN/TBR/APR` registers read and write correctly, but `AWUF`
  never sets, not even through the MSR LSI measurement), so HALT cannot be
  woken and the IWDG resets the MCU after ~0.5 s.  Bench-verified 2026-11-21
  (see `KNOWN_ISSUES.md`, KI-077).
- `system_ticks` keeps advancing in Wait mode, so tick-based timeouts
  (including the 2 s I2C self-reset) behave exactly as in Run mode.
- `entries` counts one sleep episode per quiet period (not per tick), `w_i2c`
  counts frames served while asleep, `w_timer` counts timer wakes (saturating).
- Default: off on the production build (`BOARD=oko`, 2.2.0), on for the test
  board (`BOARD=test`, 2.3.2).  `oko-motor flash`/`/api/flash` disable the
  sleep before `ENTER_BOOT`.
- Keep in mind: while the host polls the controller more often than every
  `idle_s` (divinus polls `/api/firmware` about every 2 s with a 3 s
  threshold), the sleep never engages.  That is expected: the feature targets
  windows where the AP does not talk to the drive, including suspend.

## Error Handling
- Unknown commands are ignored (no NACK); 0x07 is ENTER_BOOT in this fork.
- I2C errors (AF/BERR/OVR) → automatic module reset
- Timeout 2 s without activity → module reset

## OKO bootloader

This fork adds command `0x07` (ENTER_BOOT): the application stores `0x42` in
EEPROM `0x4010` and lets the IWDG reset the MCU. The bootloader at
`0x8000–0x89FF` answers commands `0x10–0x14`; see `BOOTLOADER.md` for the
frames, CRC and the `0x8A00`/`0x8A10` application layout.
