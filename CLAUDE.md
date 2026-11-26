# claudemon: working notes

ESP32 / Arduino firmware for the ESP32-2432S028R "Cheap Yellow Display" that
shows Claude Code usage. Built with PlatformIO; see README.md for setup, pages
and security notes.

- Branch + PR always, never push to `main`. The `protect-main` ruleset requires
  a PR, the `build` check and one approving review. Only Dave merges.
- PlatformIO environments: `cyd` (hardware), `emulator` and `wokwi`. The
  `FAKE_DATA` and `WOKWI` `#ifdef` guards select what each one builds, so a
  change can compile in one and break another.

## Review priorities

The Claude PR reviewer reads this section. Prioritise:

- correctness bugs and anything that could crash, hang or reboot-loop on-device;
- memory safety: buffer and `snprintf` sizes, Arduino `String` lifetimes, and
  the scope of pointers to objects handed to WiFiManager or the web server;
- build breakage across the `cyd`, `emulator` and `wokwi` environments and their
  `#ifdef` guards (`FAKE_DATA` / `WOKWI`);
- handling of the credentials the device holds (OAuth tokens and the Admin API
  key in NVS): never log them or serve them back over the setup AP or web pages;
- consistency with the idioms and style of the surrounding code.

Skip nitpicks and praise; flag only things worth changing.
