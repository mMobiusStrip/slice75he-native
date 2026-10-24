#include "protocol.h"

#include <algorithm>

namespace proto {

uint8_t Checksum(const Packet& p) {
    // Not a real checksum: header bytes plus only the last payload byte.
    unsigned sum = 53u + p[0] + p[1] + p[2];
    unsigned len = p[1];
    if (len > 0 && len + 3 < p.size()) sum += p[len + 3];
    return static_cast<uint8_t>(sum);
}

Builder::Builder(uint8_t cmd, uint8_t fill) {
    p_.fill(fill);
    p_[0] = kHead;
    p_[1] = 0;
    p_[2] = cmd;
    p_[3] = 0;
}

Builder& Builder::U8(uint32_t v) {
    if (pos_ < p_.size()) p_[pos_++] = static_cast<uint8_t>(v);
    return *this;
}

Builder& Builder::U16(uint32_t v) { return U8(v).U8(v >> 8); }
Builder& Builder::U24(uint32_t v) { return U8(v).U8(v >> 8).U8(v >> 16); }
Builder& Builder::U32(uint32_t v) { return U8(v).U8(v >> 8).U8(v >> 16).U8(v >> 24); }

Packet Builder::Finish() {
    p_[1] = static_cast<uint8_t>(pos_ - 4);
    p_[3] = Checksum(p_);
    return p_;
}

Packet CmdPack(uint8_t order) { return Builder(CMD).U8(order).U8(0xFF).U8(0xFF).Finish(); }

Packet CmdPack(uint8_t order, uint8_t param) {
    return Builder(CMD).U8(order).U8(param).U8(0xFF).U8(0xFF).Finish();
}

Packet SyncPack() { return Builder(SYNC).U8(1).U8(2).U8(3).U8(4).U8(0xFF).U8(0xFF).Finish(); }

Packet DefKeyPack(uint8_t rowA, uint8_t rowB) { return Builder(DEFKEY).U8(0).U8(rowA).U8(rowB).Finish(); }

Packet KeyPack(bool write, const KeyRecord* recs, size_t n) {
    // The web app always sends 14 slots; unused ones are 0 on writes and 0xFF on reads.
    Builder b(KEY);
    b.U8(write ? 1 : 0);
    for (size_t i = 0; i < kMaxKeyRecords; ++i) {
        if (i < n) {
            b.U8(recs[i].key).U8(recs[i].field).U16(recs[i].value);
        } else if (write) {
            b.U8(0).U8(0).U16(0);
        } else {
            b.U8(0xFF).U8(0xFF).U16(0x00FF);
        }
    }
    return b.Finish();
}

Packet DbPack(bool write, uint16_t actuation, uint16_t deadPress, uint16_t deadRelease) {
    Builder b(DB);
    b.U8(write ? 1 : 0).U8(0).U8(0).U16(actuation).U16(deadPress).U16(deadRelease);
    for (int i = 0; i < 6; ++i) b.U8(0);
    return b.Finish();
}

Packet LightPack(uint8_t cmd, bool write, const LightState& s) {
    Builder b(cmd);
    b.U8(write ? 1 : 0).U8(0).U8(0).U8(0).U8(0);
    for (uint32_t c : s.colors) b.U8(c & 0xFF).U8((c >> 8) & 0xFF).U8((c >> 16) & 0xFF).U8(0xFF);  // B, G, R, 0xFF
    b.U8(0).U8(0).U8(0).U8(0);
    uint8_t flags = (s.on ? 0x01 : 0) | (s.reverse ? 0x02 : 0) | (s.superResponse ? 0x10 : 0);
    b.U8(flags).U8(s.brightness).U8(s.mode).U8(s.speed).U8(s.sleepMinutes).U8(s.staticSlot);
    return b.Finish();
}

Packet KrgbPack(bool write, const uint8_t* keys, const uint32_t* rgb, size_t n) {
    Builder b(KRGB, 0xFF);
    b.U8(write ? 1 : 0);
    for (size_t i = 0; i < n && i < kMaxKeyRecords; ++i) {
        uint32_t c = rgb ? rgb[i] : 0;
        b.U8(keys[i]).U8((c >> 16) & 0xFF).U8((c >> 8) & 0xFF).U8(c & 0xFF);  // R, G, B
    }
    return b.Finish();
}

Packet RmPack(uint8_t sub, uint8_t half) { return Builder(RM6X21).U8(sub).U8(half).U8(0xFF).U8(0xFF).Finish(); }

namespace {
void Code(Builder& b, bool wide, uint16_t code) {
    if (wide) {
        b.U16(code);
    } else {
        b.U8(code);
    }
}
}  // namespace

Packet DksPack(bool wide, uint8_t key, const uint16_t codes[4], const uint8_t trps[4], uint16_t db1, uint16_t db2) {
    Builder b(TDKS);
    b.U8(1).U8(key);
    for (int i = 0; i < 4; ++i) Code(b, wide, codes[i]);
    for (int i = 0; i < 4; ++i) b.U8(trps[i]);
    b.U16(db1).U16(db2);
    return b.Finish();
}

Packet MptPack(bool wide, uint8_t key, const uint16_t codes[3], const uint16_t depths[3]) {
    Builder b(DDKS);
    b.U8(1).U8(key);
    for (int i = 0; i < 3; ++i) Code(b, wide, codes[i]);
    for (int i = 0; i < 3; ++i) b.U16(depths[i]);
    return b.Finish();
}

Packet MtPack(bool wide, uint8_t key, uint16_t hold, uint16_t tap, uint16_t delayMs) {
    Builder b(MT);
    b.U8(1).U8(key);
    Code(b, wide, hold);
    Code(b, wide, tap);
    b.U8(std::min<uint16_t>(delayMs / 10, 255));
    return b.Finish();
}

Packet TglPack(bool wide, uint8_t key, uint16_t code, uint16_t delayMs) {
    Builder b(TGL);
    b.U8(1).U8(key);
    Code(b, wide, code);
    b.U8(std::min<uint16_t>(delayMs / 10, 255));
    return b.Finish();
}

Packet EndPack(bool wide, uint8_t key, uint16_t code) {
    Builder b(END);
    b.U8(1).U8(key);
    Code(b, wide, code);
    return b.Finish();
}

Packet RsPack(uint8_t keyA, uint8_t keyB) { return Builder(RS).U8(1).U8(keyA).U8(keyB).U8(keyB).U8(keyA).Finish(); }

Packet SocdV1Pack(uint8_t keyA, uint8_t keyB) {
    return Builder(SOCD).U8(1).U8(keyA).U8(keyB).U8(keyB).U8(keyA).Finish();
}

Packet SocdV2Pack(uint8_t keyA, uint8_t keyB, uint8_t mode) {
    static const uint8_t kModeBits[4][2] = {{0, 0}, {0, 1}, {1, 0}, {1, 1}};
    mode &= 3;
    return Builder(SOCD)
        .U8(1)
        .U8(keyA).U8(keyB).U8(kModeBits[mode][0])
        .U8(keyB).U8(keyA).U8(kModeBits[mode][1])
        .Finish();
}

Packet SocdV3Pack(bool write, uint8_t key1, uint8_t key2, uint16_t val1, uint16_t val2, uint8_t type, uint8_t mode) {
    return Builder(SOCD).U8(write ? 1 : 0).U8(key1).U8(key2).U16(val1).U16(val2).U8(type).U8(mode).Finish();
}

Packet MacroPack(bool v2, uint16_t first, const MacroEvent* ev, size_t n) {
    // Slots after `n` are still serialized as zeros, like the web app does.
    const size_t slots = v2 ? 9 : 14;
    Builder b(MACRO);
    b.U8(1).U16(256 + first).U8(static_cast<uint8_t>(std::min(n, slots)));
    for (size_t i = 0; i < slots; ++i) {
        uint16_t code = i < n ? ev[i].code : 0;
        uint32_t flag = i < n ? (ev[i].down ? 1u : 8u) : 0u;
        uint32_t delay = i < n ? ev[i].delayMs : 0u;
        b.U16(code);
        if (v2) {
            b.U32((flag << 24) | (delay & 0xFFFFFF));
        } else {
            b.U16((flag << 12) | (std::min<uint32_t>(delay, 4095)));
        }
    }
    return b.Finish();
}

Packet MacroModePack(bool write, uint8_t key, uint16_t macroId, uint8_t steps, uint8_t mode, uint16_t repeatCount,
                     uint32_t repeatDelay) {
    return Builder(MACROMODE)
        .U8(write ? 1 : 0)
        .U8(key)
        .U16(macroId)
        .U8(steps)
        .U8(mode)
        .U16(repeatCount)
        .U24(repeatDelay)
        .Finish();
}

}  // namespace proto
