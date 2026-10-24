// Keyboard session: device discovery, paced packet sending, response parsing and the mirrored device state.
#pragma once

#include <windows.h>

#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "hid_device.h"
#include "protocol.h"

constexpr int kRows = 6;
constexpr int kCols = 21;

struct KeyState {
    bool present = false;
    uint8_t row = 0, col = 0;
    std::array<uint16_t, 4> layers{};    // keycode per Fn layer
    std::array<uint16_t, 4> db{1500, 1400, 3000, 0};  // um: actuation + advanced-key depths
    uint8_t touchMode = 0;
    uint8_t advType = 0;
    std::array<uint16_t, 4> dks{};
    std::array<uint8_t, 4> trps{};
    uint16_t mtDelayMs = 200;
    uint16_t rtPress = 300, rtRelease = 300;  // um
    uint16_t deadPress = 200, deadRelease = 200;
    uint16_t releasePoint = 0;
    uint16_t axis = 0;  // index into the switch list
    uint32_t color = 0x00FFFF;
    uint16_t liveTravel = 0;  // um, from the travel test / calibration stream
    uint16_t liveAdc = 0;
    uint8_t livePress = 0;
};

struct SwitchType {
    uint16_t id;
    std::string name;
    uint16_t maxTravel;  // um
};

enum class ConnState { NoDevice, Opening, Syncing, Loading, Ready, Bootloader, Error };

struct DeviceState {
    ConnState conn = ConnState::NoDevice;
    std::string error;

    // Identity
    uint16_t vid = 0, pid = 0;
    uint32_t boardId = 0;
    uint8_t layoutId = 0, keyType = 0;
    std::string serial, firmware, buildDate, name;
    int protocol = 0;  // major*10000 + minor*100 + patch
    uint8_t caseColor = 0;

    // Device-wide settings
    uint16_t precision = 10, minTravel = 100, maxTravel = 4000;  // um
    int configId = -1;
    std::vector<SwitchType> switches;
    int defaultAxis = -1;
    int pollingIndex = -1;
    int sysMode = -1;  // 0 Win, 1 Mac
    int topDead = -1;  // -1 unknown
    uint16_t globalActuation = 1500, globalDeadPress = 200, globalDeadRelease = 200;

    proto::LightState mainLight;
    proto::LightState logoLight;
    bool hasLogoLight = false;

    // Keys, addressed by their keyValue (default HID code)
    std::array<KeyState, 256> keys{};
    std::array<std::array<uint8_t, kCols>, kRows> matrix{};  // keyValue per cell, 0 = empty
    int keysLoadedPackets = 0, keysTotalPackets = 0;

    // Last macro-mode / SOCD read-back
    struct {
        bool valid = false;
        uint16_t macroId = 0;
        uint8_t mode = 0;
        uint16_t repeatCount = 1;
        uint32_t repeatDelay = 100;
    } macroMode;
    struct {
        bool valid = false;
        uint8_t key1 = 0, key2 = 0, type = 0, mode = 0;
        uint16_t val1 = 0, val2 = 0;
    } socd;

    bool Has(int since) const { return protocol >= since; }
    bool WideAdvanced() const { return Has(10003); }
    bool MacroV2() const { return Has(10004); }
    bool SignalDead() const { return Has(10001); }
    bool SignalSwitch() const { return Has(10002); }
    bool TopDeadSupported() const { return Has(10006); }
    // SOCD page selection mirrors the web app: v2 wins over v3 on 1.2.0+.
    int SocdVersion() const {
        if ((protocol >= 10000 && protocol <= 10004) || protocol >= 10200) return 2;
        if (protocol >= 10005) return 3;
        return 1;
    }
    uint16_t KeyMaxTravel(const KeyState& k) const {
        if (SignalSwitch() && k.axis < switches.size()) return switches[k.axis].maxTravel;
        return maxTravel;
    }
    std::vector<uint8_t> AllKeys() const;
};

enum class LiveMode { Off, TravelTest, Calibration };

class Keyboard {
public:
    Keyboard();
    ~Keyboard();

    void Attach(HWND hwnd, UINT hidMsg);
    void Rescan();  // (re)connect if a supported keyboard is present
    void Disconnect();
    void OnHidNotify();  // drain and parse input reports (UI thread)
    void Tick();         // called periodically from a UI timer: timeouts and live stream watchdog

    DeviceState& State() { return st_; }
    const DeviceState& State() const { return st_; }
    bool Busy() const;
    std::string StatusText() const;

    // Queue a packet; `delayMs` is waited after sending it.
    void Send(const proto::Packet& p, unsigned delayMs = 2);

    // High-level operations
    void ReloadAll();
    void ReadKeyFields();
    void ReadAdvanced(uint8_t kv);
    void WriteKeyRecords(const std::vector<proto::KeyRecord>& recs);
    void WriteMainLight();
    void WriteLogoLight();
    void WriteGlobalDb();
    void WriteKeyColors(const std::vector<uint8_t>& keys);
    void ReadKeyColors();
    void SetLiveMode(LiveMode m);
    LiveMode GetLiveMode() const { return live_; }

private:
    struct Outgoing {
        proto::Packet packet;
        unsigned delayMs;
    };

    void SenderLoop();
    void StartInit();
    void Handle(const uint8_t* p, size_t len);
    void HandleCmd(const uint8_t* a);
    void HandleSync(const uint8_t* a);
    void HandleLight(const uint8_t* a, proto::LightState& s);
    void HandleLive(const uint8_t* a);
    void RequestLiveNext();
    void ClearQueue();

    HWND hwnd_ = nullptr;
    UINT hidMsg_ = 0;
    HidDevice dev_;
    DeviceState st_;

    std::thread sender_;
    mutable std::mutex qMutex_;
    std::condition_variable qCv_;
    std::deque<Outgoing> queue_;
    bool sending_ = false;
    bool quit_ = false;
    HANDLE timer_ = nullptr;

    std::vector<uint8_t> rx_;  // reassembly buffer for multi-report responses
    DWORD syncSentAt_ = 0;
    int syncAttempts_ = 0;
    int defkeyRowsSeen_ = 0;

    LiveMode live_ = LiveMode::Off;
    std::deque<uint8_t> pendingHalves_[7];  // requested halves per RM6X21 sub-command
    uint8_t nextHalf_ = 1;
    DWORD lastLiveAt_ = 0;
};
