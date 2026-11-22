# OKO I2C bootloader (STM8S103F3P6)

The bootloader lives at `0x8000–0x89FF`, owns the reset vector and is entered
when the application writes `0x42` to EEPROM `0x4010` and stops feeding the
IWDG. It is a polling design: interrupts stay disabled in boot mode, so the
SDCC vector table only matters after the jump to the application.

## Flash layout

| Range | Content |
|---|---|
| `0x8000–0x89FF` | bootloader (2560 bytes max; measured 2528) |
| `0x8A00–0x8A0F` | application header |
| `0x8A10–0x9FFF` | application (5616 bytes max; measured 5359) |
| EEPROM `0x4010` | boot request magic (`0x42`) |
| EEPROM `0x4000–0x4009` | motor positions (application) |

The header is 16 bytes: `OKO8`, big-endian length, major/minor/patch, flags,
big-endian CRC32 over the application bytes starting at `0x8A10`.

The split was changed from the first plan (`0x8810`) after the size gate:
the measured boot image is 2528 bytes, so 2 KiB was not enough. The application
still has 257 bytes of headroom, so boot-side changes are measured against
`boot: … limit 0x89FF` from `tools/mkapp.py` on every build.

## Vector forwarding

SDCC places interrupt vector N at table entry `N + 2`. A vector entry is four
bytes `82 00 <addr_hi> <addr_lo>`, so the handler address is a word at
`OKO_APP_BASE + (N + 2) * 4 + 2`. The boot image declares naked trampolines in
the module containing `main()` (SDCC's vector-table rule):

```c
void I2C_IRQHandler(void) __naked __interrupt(19)
{
    __asm__("ldw x, 0x8A66"); /* app table entry 21 + 2 */
    __asm__("jp (x)");
}
void TIM4_UPD_OVF_IRQHandler(void) __naked __interrupt(23)
{
    __asm__("ldw x, 0x8A76"); /* app table entry 25 + 2 */
    __asm__("jp (x)");
}
```

Boot itself never calls `rim()`, so these trampolines only execute after the
application has started.

## Protocol

All multi-byte fields are big-endian. A write frame is `[cmd][payload]`; the
status answer is read in the following transaction. `oko-motor` and
`tools/mkapp.py` implement the same layout.

| Command | Request | Response |
|---|---|---|
| `0x08 GET_STATE` | — | 4 bytes: role (1 = boot), major, minor, patch; answered by both roles |
| `0x10 BOOT_INFO` | — | 8 bytes: boot version, app header version, max block (32), state (1 = boot); bootloader only |
| `0x11 BOOT_ERASE` | start u32, length u32 | status + command echo (2 bytes) |
| `0x12 BOOT_WRITE` | address u32, length u8 (≤32, multiple of 4), data, CRC16 | status + command echo (2 bytes) |
| `0x13 BOOT_FINISH` | length u32, CRC32 u32 | status + command echo (2 bytes) |
| `0x14 BOOT_RESET` | — | jumps to the app if header + CRC are valid |

CRC16 is poly `0xA001`, init `0xFFFF` (the same variant as the EEPROM code).
CRC32 is the standard reflected `0xEDB88320` with init/xorout `0xFFFFFFFF`.
Status: 0 OK, 1 busy, 2 bad address, 3 bad CRC, 4 flash error, 5 verify error,
6 bad command.

Only word (4-byte) programming is used; it runs from flash without a RAM copy
and keeps the boot code small (bench-verified: a word written by `0x12` reads
back over SWIM). Erase is per 64-byte block (STM8S103 page size) and must not
run from flash: the controller stops serving instruction fetches while the
block is being erased, so the operation is performed by a 67-byte RAM stub
(`src/boot/erase_stub.asm`, copied to RAM at boot and called through a
function pointer). Its execution from RAM is verified with a live stub, but on
the current bench sample the IAP block erase itself never completes — see
KI-075 in the repository root `KNOWN_ISSUES.md`.

## Build and first flash

```sh
docker build -t oko-stm8-sdcc tools/stm8-sdcc
docker run --rm -v "$PWD:/work" -w /work \
  oko-stm8-sdcc make -f Makefile.stm8 all
# first time only (ST-LINK/v2 + SWIM)
docker run --rm --device=/dev/bus/usb -v "$PWD:/work" \
  -w /work oko-stm8-sdcc \
  make -f Makefile.stm8 flash
# later, over I2C from the device (the name carries the version)
oko-motor flash build/app-2.2.0.ihx
```
