# Changelog

## 0.2.0

- UI languages: Türkçe, English, Deutsch, 中文, 日本語 (switch in the top bar; the choice is remembered).
- New keyboard visual that matches the real Slice75 HE, with backlight glow in the Lighting tab.
- Redesigned menus: cards, segmented selectors, switches.
- Version info in the exe and an About card with release notes.
- Every write operation verified on a real keyboard with the new self-test (`--selftest <log>`): keymap,
  actuation, Rapid Trigger, deadzones, DKS, MPT, Mod-Tap, Toggle, END, RS, SOCD, macros, per-key colour,
  live travel.
- Fix: the DKS editor now reads the second depth from the right field.
- Fix: the Chinese/Japanese font data is freed after loading, so RAM stays around 50 MB.

## 0.1.0

- First release.
