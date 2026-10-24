// 16-bit keycodes understood by the firmware (HID usages, consumer 0x10xx, mouse 0x73xx, Fn 0xF0xx, system 0xF1xx/0xF3xx).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

enum class KeyCat { Letter, Number, Symbol, Basic, Function, FunctionAdv, Control, Mouse, Hidden };

struct KeyCodeInfo {
    uint16_t code;
    const char* label;
    const char* hint;  // longer English description, may be empty
    KeyCat cat;
};

extern const KeyCodeInfo kKeyCodes[];
extern const size_t kKeyCodeCount;

const KeyCodeInfo* FindKeyCode(uint16_t code);
std::string KeyCodeLabel(uint16_t code);

// Windows virtual key (+ extended flag) to HID keyboard usage, 0 if unknown. Used for macro recording.
uint16_t VirtualKeyToHid(unsigned vk, bool extended);
