# Verification — 2026-11-05

The current results, exact image hashes, memory use and hardware limits are recorded in [IMPLEMENTATION-20261105.md](IMPLEMENTATION-20261105.md). The early prototype record is retained in [IMPLEMENTATION.md](IMPLEMENTATION.md).

| Layer | Current verification |
| --- | --- |
| Dependencies | Pinned LVGL, cJSON, official SWM341 SDK and generated-font hashes checked during CMake configuration |
| MCU application | Arm GNU 13.3.Rel1 builds ELF, map and 421888-byte application; no bootloader image included |
| Native screen | 7/7 core, widgets, update, sensors, services, maintenance and factory tests, also under ASan/UBSan |
| ESP bridge | 6/6 bridge, real screen endpoint, stock frames, power-loss storage, weather and production OTA owner tests, also under ASan/UBSan |
| UART OTA | Two full transfers between production ESP owner and production MCU maintenance code; actual generated MCU image also verified through this path |
| Sensors | 223 SHT CPU fixtures, 180 calibration transitions, three dual-channel sequences and 30 parsed-handler CPU cases |
| Protocol | Independent screen source and paired ESP snapshot hashes/bytes match |
| UI | Actual LVGL RGB565 rendering, JS/C scene codec round trips and Chromium MessagePack HTTP scenarios in two languages and two widths |
| Live ESP | Original MCU communication, layout/reminder persistence, API notices, image upload/readback, reboot and stock-MCU OTA activation gate; exact final assertions are in the delivery record |

Native tests retain assertions in Release (`-UNDEBUG`). Frame tests use the real parsers, failed/partial queueing, fallback, capability reset, replay rejection, expiry and 32-bit uptime wrap. Storage tests interrupt 36 write steps and require an intact old or new record. OTA tests include timeout, wrong offset, NOR read/write/erase failure, corruption and cancellation.

The live screen MCU remains original SV.04.00.01.00. Community MCU hardware acceptance, mixed original/community firmware combinations, power transitions and installed OTA recovery remain pending. Original bootloader recovery or a reliable SWD recovery path is required before enabling activation. Current staging code never writes the A1 install marker at NOR 0x7f0000. Demo preview readings are simulated.
