# CrossPoint: Enhanced Reading Mod

A custom firmware fork for the **Xteink X4** e-paper reader, built on top of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader).

![](./docs/images/cover.jpg)

## Project Intent (Why This Fork Exists)

This fork is focused on one practical goal:

- make highlighting and reading workflows easier on-device, and
- serve as the firmware base for a **Text-to-Speech pipeline that streams text over Bluetooth to the X4 workflow**.

This repo is the reader-side firmware foundation. The companion project for the TTS pipeline is:

- **AutoHighlightTTS**: https://github.com/leocvf/AutoHighlightTTS

If you are here for the end-to-end TTS path, use this firmware together with AutoHighlightTTS.

## What This Mod Adds

Compared to stock behavior, this fork improves fast reading controls and sentence-level interaction from hardware buttons.

### Reading Controls (Full Mod)

- **Single click** — Increase or decrease text size
- **Hold** — Cycle line spacing (Tight, Normal, Wide)
- **Double-click left** — Toggle paragraph alignment (Left/Justified)
- **Double-click right** — Toggle bold text
- **Hold right** — Rotate screen orientation
- **Double-click back** — Toggle Dark/Light mode
- **Hold menu** — Show on-screen control guide

> **Simple Mode:** Optional mode for text-size-only control if you want fewer accidental actions.

## Sentence Highlighting

You can highlight sentences and store them on SD (`/highlights/`) with persistence per book.

### Workflow

1. **Enter cursor mode**: Double-tap Power
2. **Select sentence**: Move with Up/Down, tap Power to auto-select sentence boundaries
3. **Adjust and save**:
   - Tap Power to move to next sentence
   - Down to expand selection
   - Up to shrink selection
   - Left rocker for start-word adjustment
   - Right rocker for end-word adjustment
   - Double-tap Power to save

### Cancel / Delete

- **Long-press Back** to cancel cursor mode
- To delete a saved highlight, position on highlighted line and **double-tap Power**

## Additional Improvements

- Gray background fix on first boot refresh
- Portrait/landscape button swap options
- Native bold font toggle
- Anti-aliased text rendering improvements

## Installing

### Recommended (Web Flasher)

1. Connect your Xteink X4 via USB-C
2. Download `firmware.bin` from the [latest release](https://github.com/leocvf/crosspoint-enhanced-reading-mod/releases)
3. Flash at https://xteink.dve.al/ using **OTA fast flash controls**

### Reverting to Stock

Use official firmware at https://xteink.dve.al/ or the **Swap boot partition** action at https://xteink.dve.al/debug.

## Building From Source

This project uses PlatformIO:

```sh
git clone --recursive https://github.com/leocvf/crosspoint-enhanced-reading-mod
pio run --target upload
```

## Credits

- [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)
- Inspiration from [diy-esp32-epub-reader](https://github.com/atomic14/diy-esp32-epub-reader)

---

*This project is not affiliated with Xteink or any manufacturer of X4 hardware.*
