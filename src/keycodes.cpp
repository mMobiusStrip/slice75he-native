#include "keycodes.h"

#include <windows.h>

#include <cstdio>

const KeyCodeInfo* FindKeyCode(uint16_t code) {
    for (size_t i = 0; i < kKeyCodeCount; ++i) {
        if (kKeyCodes[i].code == code) return &kKeyCodes[i];
    }
    return nullptr;
}

std::string KeyCodeLabel(uint16_t code) {
    if (const KeyCodeInfo* k = FindKeyCode(code)) return k->label;
    char buf[16];
    snprintf(buf, sizeof(buf), "0x%04X", code);
    return buf;
}

uint16_t VirtualKeyToHid(unsigned vk, bool extended) {
    if (vk >= 'A' && vk <= 'Z') return static_cast<uint16_t>(0x04 + (vk - 'A'));
    if (vk >= '1' && vk <= '9') return static_cast<uint16_t>(0x1E + (vk - '1'));
    if (vk == '0') return 0x27;
    if (vk >= VK_F1 && vk <= VK_F12) return static_cast<uint16_t>(0x3A + (vk - VK_F1));
    if (vk >= VK_F13 && vk <= VK_F24) return static_cast<uint16_t>(0x68 + (vk - VK_F13));
    if (vk >= VK_NUMPAD1 && vk <= VK_NUMPAD9) return static_cast<uint16_t>(0x59 + (vk - VK_NUMPAD1));

    switch (vk) {
        case VK_NUMPAD0: return 0x62;
        case VK_RETURN: return extended ? 0x58 : 0x28;
        case VK_ESCAPE: return 0x29;
        case VK_BACK: return 0x2A;
        case VK_TAB: return 0x2B;
        case VK_SPACE: return 0x2C;
        case VK_OEM_MINUS: return 0x2D;
        case VK_OEM_PLUS: return 0x2E;
        case VK_OEM_4: return 0x2F;   // [
        case VK_OEM_6: return 0x30;   // ]
        case VK_OEM_5: return 0x31;   // backslash
        case VK_OEM_1: return 0x33;   // ;
        case VK_OEM_7: return 0x34;   // '
        case VK_OEM_3: return 0x35;   // `
        case VK_OEM_COMMA: return 0x36;
        case VK_OEM_PERIOD: return 0x37;
        case VK_OEM_2: return 0x38;   // /
        case VK_OEM_102: return 0x64; // ISO extra key
        case VK_CAPITAL: return 0x39;
        case VK_SNAPSHOT: return 0x46;
        case VK_SCROLL: return 0x47;
        case VK_PAUSE: return 0x48;
        case VK_INSERT: return 0x49;
        case VK_HOME: return 0x4A;
        case VK_PRIOR: return 0x4B;
        case VK_DELETE: return 0x4C;
        case VK_END: return 0x4D;
        case VK_NEXT: return 0x4E;
        case VK_RIGHT: return 0x4F;
        case VK_LEFT: return 0x50;
        case VK_DOWN: return 0x51;
        case VK_UP: return 0x52;
        case VK_NUMLOCK: return 0x53;
        case VK_DIVIDE: return 0x54;
        case VK_MULTIPLY: return 0x55;
        case VK_SUBTRACT: return 0x56;
        case VK_ADD: return 0x57;
        case VK_DECIMAL: return 0x63;
        case VK_APPS: return 0x65;
        case VK_LCONTROL: return 0xE0;
        case VK_LSHIFT: return 0xE1;
        case VK_LMENU: return 0xE2;
        case VK_LWIN: return 0xE3;
        case VK_RCONTROL: return 0xE4;
        case VK_RSHIFT: return 0xE5;
        case VK_RMENU: return 0xE6;
        case VK_RWIN: return 0xE7;
        case VK_CONTROL: return extended ? 0xE4 : 0xE0;
        case VK_MENU: return extended ? 0xE6 : 0xE2;
        case VK_VOLUME_MUTE: return 0x10E2;
        case VK_VOLUME_UP: return 0x10E9;
        case VK_VOLUME_DOWN: return 0x10EA;
        case VK_MEDIA_PLAY_PAUSE: return 0x10CD;
        case VK_MEDIA_NEXT_TRACK: return 0x10B5;
        case VK_MEDIA_PREV_TRACK: return 0x10B6;
        case VK_MEDIA_STOP: return 0x10B7;
    }
    return 0;
}
