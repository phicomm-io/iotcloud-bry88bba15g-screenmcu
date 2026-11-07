# UART flashing and boot backup workflow

Screen target: BRY88BBA15G, SWM34SVET6-50. Images contain only the application
at `0x14000`, padded to `0x67000` bytes. The original bootloader and internal
configuration at `0x7e000` are preserved.

## Maintenance and activation

The full application serves a 30-second SRAM-only UART window before SDRAM,
LCD, sensors and business startup. Fault/WDT resets stay in maintenance; the
recovery-only variant stays there indefinitely. PM7 remains high to retain
power. WDT clocks and inherited control are recorded before explicit handoff;
foreground progress feeds the watchdog, with no ISR feeding.

`screen_recovery 3` announces the boot session, window/mode and inherited WDT.
`screen_image 1 <md5>` reports a digest computed from the running internal
application bytes, not the downloaded NOR image. Hardware PRNG unavailability
uses a nonzero session fallback and cannot disable the maintenance parser.

Binary `+bin:` packets consist of five prefix bytes and exactly the announced
payload length, with **no CR/LF suffix**, matching factory ESP32 function
`0x400dadd4`. The receive parser is length driven and tolerates the older extra
CR/LF between packets. ESP32 suppresses periodic business snapshot queries while
transferring an image.

The MCU checks streaming MD5, vectors and a complete NOR readback. Staging alone
does not activate an image. At the time of this test, `GUARD_INSTALL` required a complete, contiguous
80 KiB boot export in the same MCU session, a staged image and another complete
NOR MD5 check immediately before the flag write. The 2026-11-06 application
removes the repeated export condition: it retains the live bootloader MD5 check,
verified staged image and erased copy tail, and reuses the matching host backup. Dedicated
callbacks erase only the flag block at `0x7f0000`, program `A1`, read it back and
reset through the factory bootloader. A1/A2 are acknowledged at startup in the
same way as the factory application; unknown flag states remain untouched.
General NOR staging writes cannot access the installation flag.

## First installation

The ESP32's authenticated `screen.update.start` has an explicit
`factory-bootstrap` mode restricted to the observed factory MCU version
`SV.04.00.01.00`, a recovery-only image with the correct target/vectors, and an
exact SHA-256 confirmation. It sends the existing factory OTA request rather
than driving SWD or internal Flash directly. This first step necessarily uses
the existing factory application and bootloader before boot export is available.
The recovery-only image keeps the screen off while its UART services run.

The ESP32 repository provides:

```sh
python BRY88BBA15G/tools/flash_screen.py --url http://DEVICE \
  --image recovery/screen_firmware.bin --mode factory-bootstrap --log bootstrap.jsonl
python BRY88BBA15G/tools/backup_screen_boot.py --url http://DEVICE --output boot-backup
python BRY88BBA15G/tools/flash_screen.py --url http://DEVICE \
  --image full/screen_firmware.bin --mode install --boot-backup boot-backup --log full.jsonl
```

The backup reads `0x00000..0x13fff` twice, using 256-byte CRC frames, and requires
two matching complete SHA-256 hashes. Inspect the recovered bootloader before
activating the full image. A new MCU session invalidates the export gate and
requires a new backup. Installation success requires a new boot session and a
matching internal-application MD5; successful upload or `end_ota` alone is not
installation success.

The recovered loader is now identified by SHA-256
`ccb9d414186c5ea47823badf5083f7ab5b1b61b02c6a8d0510af6ee31bebdf09`.
See [the board-specific boot analysis](BOOTLOADER-20261105.md): its copy loop
exceeds the application size. Updated MCU activation checks the loader digest
and requires the entire extra NOR source region to be FF before writing A1.
Unknown contents are preserved and installation is rejected. The host installer
also rejects backups from an unreviewed loader.

## Validation and hardware record

Host tests exercise full-length transfers, exact raw packet length, corrupt
NOR readback, final digest mismatch, stale sessions/sequences, busy exclusion,
flag programming failures and boot export gating. Compiled ARM execution checks
startup without SDRAM, actual boot-flash reads, inherited WDT handoff and PRNG
fallback. Peripheral MMIO and ROM helpers are mocked in those CPU checks.

Current hardware logs and exact artifacts are retained in the ESP repository:
`private/bry88-screen-flash-20261105/`. The hardware outcome is recorded there in
`RESULT.md`; use that record to distinguish completed installation from staging.
The initial stock transfer failed; the completed fix uses a single buffered
prefix/payload write, no suffix, 512-byte stock packets and suppression of
interleaved business queries. The recovery-only image then booted and exported
two identical loader backups. Full-image installation initially exposed a
30-second deadline consumed by synchronous NOR hashing. MCU deadlines now
rearm after blocking work; ESP32 queues final metadata before that work for
compatibility with the already-installed recovery image.

The full application `57a7532de17b5382844f9af8acbc916cb17b77be86f1ae7974d73bb6bb96ab6a`
subsequently booted in a new session and reported internal MD5
`892716ff9456a963d973f5ab52781c8e`. The startup hold, two complete boot reads,
and resume succeeded. Both loader reads matched the pre-install backup.
The MCU entered normal mode, acknowledged the saved layout, and supplied live
temperature, humidity, CO2, PM2.5 and TVOC snapshots. The user confirmed normal
display and touch paging. These observations do not prove recovery from power
loss during internal Flash copying; the original loader has no MCU A/B slot.

An actual recovery-reset test then found that the loader erased a low-address
`.noinit` latch. The replacement reserves 16 bytes at `0x2000bff0` below the
16 KiB application stack and stores the recovery magic plus its complement.
The reviewed loader's actual scatter code and the compiled fault handler were
executed to check placement and operation without a valid stack. Full/recovery
CPU startup also covered retained and corrupted latches. The corrected full
image SHA-256 is
`91fc5f0c65ec1f2794d2efc358c5f3cb897eff22552f6cd5efab11a7b2e3a38f`,
internal MD5 `16ac007b9612d2c6960e23723ff11ba8`; it has booted in a new session.
The actual software reset retained forced maintenance in session `3449109535`.
Resume was rejected, and an install before boot export was rejected. After two
new complete matching loader reads, `exercise_screen_recovery.py` reinstalled
the same image and observed the 30-second window expire naturally. Session
`1107937412` entered normal mode and acknowledged the existing layout revision 6.
The completed trace is `recovery-roundtrip-retry/recovery-test.jsonl` in the
hardware evidence directory. Earlier interrupted attempts remain separately.

An ESP32-only software reboot then preserved MCU session `1107937412`, recovered
capability negotiation, acknowledged layout revision 6 from persistent storage,
and resumed all five sensor channels. See `esp-rejoin.jsonl`. This verifies
reconnection to the current community ESP32; reverting ESP32 to its factory
application has not been tested on hardware.
