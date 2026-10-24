// Packet builders for the Chilkey/"KB2" HE keyboard protocol (see docs/PROTOCOL.md).
// Every packet is 64 bytes: [0]=0x5C, [1]=payload length, [2]=command, [3]=checksum, [4..]=payload.
// Responses echo the command with bit 7 set.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace proto {

using Packet = std::array<uint8_t, 64>;

constexpr uint8_t kHead = 0x5C;
constexpr uint8_t kResponseBit = 0x80;

enum Cmd : uint8_t {
    CMD = 0x00,
    SYNC = 0x01,
    RM6X21 = 0x12,
    PRGB = 0x18,
    LOGORGB = 0x19,
    MACRO = 0x20,
    MACROMODE = 0x21,
    KEY = 0x23,
    MT = 0x24,
    TGL = 0x25,
    TDKS = 0x26,  // DKS
    DDKS = 0x27,  // MPT (three depths)
    END = 0x28,
    DB = 0x29,
    KRGB = 0x2A,
    DEFKEY = 0x2B,
    SOCD = 0x2C,
    RS = 0x2D,
};

// Sub-orders of Cmd::CMD.
enum Order : uint8_t {
    PROTOCOL_VERSION = 1,
    START_CALIBRATION = 12,
    SAVE_CALIBRATION = 13,
    FACTORY_RESET = 17,
    QUERY_SYS_WIN = 33,
    QUERY_SYS_MAC = 34,
    QUERY_PRECISION = 37,
    QUERY_NAME = 38,
    QUERY_CASE_COLOR = 39,
    DEFAULT_AXIS = 40,
    SET_SYS_WIN = 48,
    SET_SYS_MAC = 49,
    TOP_DEAD_SWITCH = 52,
    POLLING_RATE = 80,
    CONFIG_ID = 112,
    AXIS_LIST = 118,
};

// Per-key "layout" fields of Cmd::KEY.
enum Field : uint8_t {
    F_FN0 = 0,  // keymap layers 0..3
    F_DB0 = 4,  // actuation point (um)
    F_DB1 = 5,
    F_DB2 = 6,
    F_DB3 = 7,
    F_MODE = 8,  // touchMode << 4 | advanced type
    F_DKS1 = 9,
    F_TRPS1 = 13,
    F_MACRO_ADDR = 17,
    F_MACRO_SIZE = 18,
    F_MT_DELAY = 19,  // 10 ms units
    F_RT_PRESS = 20,
    F_RT_RELEASE = 21,
    F_DEAD_PRESS = 22,
    F_DEAD_RELEASE = 23,
    F_RELEASE_POINT = 24,
    F_AXIS = 25,
    F_COUNT = 26,  // fields 0..25 are read back
};

enum TouchMode : uint8_t { TOUCH_GLOBAL = 0, TOUCH_SINGLE = 1, TOUCH_RAPID = 2 };

enum AdvType : uint8_t {
    ADV_NONE = 0,
    ADV_DKS = 1,
    ADV_MPT = 2,
    ADV_MT = 3,
    ADV_TGL = 4,
    ADV_END = 5,
    ADV_MACRO = 6,
    ADV_SOCD = 8,
    ADV_RS = 9,
};

constexpr int kMaxKeyRecords = 14;  // per KEY / KRGB packet

uint8_t Checksum(const Packet& p);

// Sequential payload writer that fills in length and checksum.
class Builder {
public:
    explicit Builder(uint8_t cmd, uint8_t fill = 0);
    Builder& U8(uint32_t v);
    Builder& U16(uint32_t v);
    Builder& U24(uint32_t v);
    Builder& U32(uint32_t v);
    Packet Finish();

private:
    Packet p_{};
    size_t pos_ = 4;
};

struct KeyRecord {
    uint8_t key;
    uint8_t field;
    uint16_t value;
};

struct LightState {
    bool on = false;
    bool reverse = false;
    bool superResponse = false;
    uint8_t brightness = 3;  // 0..4
    uint8_t mode = 0;        // 0 static, 1..20 effects, 21 per-key (main light only)
    uint8_t speed = 3;       // 0..4
    uint8_t sleepMinutes = 0;
    uint8_t staticSlot = 0;  // 0..7
    std::array<uint32_t, 7> colors{0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF};  // 0xRRGGBB
};

struct MacroEvent {
    uint16_t code;
    bool down;
    uint32_t delayMs;  // delay before this event
};

Packet CmdPack(uint8_t order);
Packet CmdPack(uint8_t order, uint8_t param);
Packet SyncPack();
Packet DefKeyPack(uint8_t rowA, uint8_t rowB);
Packet KeyPack(bool write, const KeyRecord* recs, size_t n);  // n <= 14
Packet DbPack(bool write, uint16_t actuation, uint16_t deadPress, uint16_t deadRelease);
Packet LightPack(uint8_t cmd, bool write, const LightState& s);  // cmd = PRGB or LOGORGB
Packet KrgbPack(bool write, const uint8_t* keys, const uint32_t* rgb, size_t n);  // n <= 14
Packet RmPack(uint8_t sub, uint8_t half);

// Advanced keys. `wide` selects the protocol >= 1.0.3 layout with 16-bit keycodes.
Packet DksPack(bool wide, uint8_t key, const uint16_t codes[4], const uint8_t trps[4], uint16_t db1, uint16_t db2);
Packet MptPack(bool wide, uint8_t key, const uint16_t codes[3], const uint16_t depths[3]);
Packet MtPack(bool wide, uint8_t key, uint16_t hold, uint16_t tap, uint16_t delayMs);
Packet TglPack(bool wide, uint8_t key, uint16_t code, uint16_t delayMs);
Packet EndPack(bool wide, uint8_t key, uint16_t code);
Packet RsPack(uint8_t keyA, uint8_t keyB);
Packet SocdV1Pack(uint8_t keyA, uint8_t keyB);
Packet SocdV2Pack(uint8_t keyA, uint8_t keyB, uint8_t mode);
Packet SocdV3Pack(bool write, uint8_t key1, uint8_t key2, uint16_t val1, uint16_t val2, uint8_t type, uint8_t mode);

// Macro body: up to 9 (v2) or 14 (v1) events starting at step `first`.
Packet MacroPack(bool v2, uint16_t first, const MacroEvent* ev, size_t n);
Packet MacroModePack(bool write, uint8_t key, uint16_t macroId, uint8_t steps, uint8_t mode, uint16_t repeatCount,
                     uint32_t repeatDelay);

}  // namespace proto
