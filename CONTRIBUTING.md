# Contributing

Thanks for helping! Issues and pull requests are welcome in English or Turkish.

## Building

Requirements: Windows 10/11 and Visual Studio 2022 Build Tools with the C++ workload.

```powershell
.\build.ps1          # Release build -> bin\Slice75HE.exe
.\build.ps1 -Debug
```

Dear ImGui is downloaded automatically on the first build. Every push and pull request is also built by
GitHub Actions; the exe is attached to the workflow run as an artifact.

## Code layout

| file | role |
|---|---|
| `src/hid_device.*` | Windows HID transport (enumerate, open, background reads, writes) |
| `src/protocol.*` | packet builders for every command |
| `src/keyboard.*` | session: init sequence, paced sending, reply parsing, mirrored device state |
| `src/app.cpp` | the whole UI (Dear ImGui), plus the self-test |
| `src/main.cpp` | Win32 window, Direct3D 11, fonts, idle-friendly render loop |
| `src/i18n.*` | translations |
| `src/keycodes*`, `src/switches.*` | keycode and switch tables |
| `docs/PROTOCOL.md` | protocol notes |

## Testing on a keyboard

```powershell
bin\Slice75HE.exe --selftest selftest.log
```

The self-test changes settings on the K and L keys, reads them back from the keyboard and restores the
original values. Please attach the log when you report a bug, together with your firmware version
(Device tab).

## Translations

All UI strings live in `src/i18n.cpp`. The source text is Turkish; each entry has English, German, Chinese
and Japanese columns. To add a language, add a value to `Lang`, a column to `Entry`, and the native name and
code in `kNames` / `kCodes`. Keep `printf` specifiers (`%d`, `%s`, …) in the same order. If the script needs
glyphs that Segoe UI lacks, extend `I18nCjkFontPath()`.

## Other keyboards

Boards that use the same "KB2" protocol (other Chilkey / NDNH HE boards) may work. If yours is detected but
the layout looks wrong, open an issue with a screenshot and the self-test log.

## Guidelines

- Match the style of the surrounding code (C++20, 4-space indent, 120 columns).
- Don't commit vendor files (the official web bundle, images or icons).
- Firmware update / bootloader commands are intentionally out of scope: a mistake can brick a keyboard.
