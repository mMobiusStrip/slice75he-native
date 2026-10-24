#include "keyboard.h"

#include <algorithm>
#include <cstring>

#include "switches.h"

using namespace proto;

namespace {

const std::vector<HidFilter> kFilters = {
    {0x1CA3, 0x0701, 0xFFA0, 1},
    {0x1CA5, 0x0701, 0xFFA0, 1},
    {0x1CA3, 0x0D01, 0xFFA0, 1},
    {0x1CA3, 0x0703, 0xFFA0, 1},
};

// Other vendors' boards share these IDs; the official app refuses them too.
const char* const kForeignNames[] = {"Gaming Keyboard Pro", "Epomaker MagForce 68", "Epomaker MagForce 75",
                                     "Epomaker MagForce 33", "GATERON GT60", "GATERON GT60 Pro", "Zynoo75"};

constexpr DWORD kSyncTimeoutMs = 1500;
constexpr DWORD kLiveStallMs = 600;
constexpr size_t kLiveResponseBytes = 192;  // RM6X21 replies span three reports

uint16_t Le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

std::string Text(const uint8_t* p, size_t n) {
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        if (p[i] != 0) s.push_back(static_cast<char>(p[i]));
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

}  // namespace

std::vector<uint8_t> DeviceState::AllKeys() const {
    std::vector<uint8_t> out;
    for (const auto& row : matrix) {
        for (uint8_t kv : row) {
            if (kv != 0) out.push_back(kv);
        }
    }
    return out;
}

Keyboard::Keyboard() {
    timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer_) timer_ = CreateWaitableTimerW(nullptr, TRUE, nullptr);
    sender_ = std::thread(&Keyboard::SenderLoop, this);
}

Keyboard::~Keyboard() {
    Disconnect();
    {
        std::lock_guard<std::mutex> lock(qMutex_);
        quit_ = true;
    }
    qCv_.notify_all();
    sender_.join();
    if (timer_) CloseHandle(timer_);
}

void Keyboard::Attach(HWND hwnd, UINT hidMsg) {
    hwnd_ = hwnd;
    hidMsg_ = hidMsg;
}

void Keyboard::SenderLoop() {
    std::unique_lock<std::mutex> lock(qMutex_);
    for (;;) {
        qCv_.wait(lock, [&] { return quit_ || !queue_.empty(); });
        if (quit_) return;
        Outgoing item = queue_.front();
        queue_.pop_front();
        sending_ = true;
        lock.unlock();

        bool ok = dev_.Write(0, item.packet.data(), item.packet.size());
        if (ok && item.delayMs > 0) {
            LARGE_INTEGER due;
            due.QuadPart = -static_cast<LONGLONG>(item.delayMs) * 10000;  // relative, 100 ns units
            if (SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) {
                WaitForSingleObject(timer_, INFINITE);
            } else {
                Sleep(item.delayMs);
            }
        }

        lock.lock();
        sending_ = false;
        if (!ok) queue_.clear();  // device went away; the reader thread reports it
        qCv_.notify_all();
    }
}

void Keyboard::Send(const Packet& p, unsigned delayMs) {
    if (!dev_.IsOpen()) return;
    {
        std::lock_guard<std::mutex> lock(qMutex_);
        queue_.push_back({p, delayMs});
    }
    qCv_.notify_all();
}

void Keyboard::ClearQueue() {
    std::unique_lock<std::mutex> lock(qMutex_);
    queue_.clear();
    qCv_.wait(lock, [&] { return !sending_; });
}

bool Keyboard::Busy() const {
    std::lock_guard<std::mutex> lock(qMutex_);
    return sending_ || !queue_.empty();
}

void Keyboard::Disconnect() {
    ClearQueue();
    dev_.Close();
    live_ = LiveMode::Off;
    rx_.clear();
    st_ = DeviceState();
}

void Keyboard::Rescan() {
    if (dev_.IsOpen()) {
        if (dev_.IsAlive()) return;
        Disconnect();
    }
    for (const HidDeviceInfo& info : EnumerateHidDevices(kFilters)) {
        bool foreign = std::any_of(std::begin(kForeignNames), std::end(kForeignNames),
                                   [&](const char* n) { return info.productName == n; });
        if (foreign) continue;
        if (!dev_.Open(info, hwnd_, hidMsg_)) {
            st_.conn = ConnState::Error;
            st_.error = "Klavye açılamadı (başka bir program kullanıyor olabilir).";
            continue;
        }
        st_ = DeviceState();
        st_.vid = info.vendorId;
        st_.pid = info.productId;
        st_.name = info.productName;
        syncAttempts_ = 0;
        StartInit();
        return;
    }
}

void Keyboard::StartInit() {
    st_.conn = ConnState::Syncing;
    defkeyRowsSeen_ = 0;
    ++syncAttempts_;
    syncSentAt_ = GetTickCount();
    Send(SyncPack(), 200);
}

void Keyboard::ReloadAll() {
    // Queued writes (e.g. a profile switch) are kept so they land before the re-read.
    if (!dev_.IsOpen()) return;
    SetLiveMode(LiveMode::Off);
    syncAttempts_ = 0;
    StartInit();
}

void Keyboard::ReadAdvanced(uint8_t kv) {
    static const uint8_t kFields[] = {F_MODE, F_DB1,     F_DB2,         F_DB3,         F_DKS1,
                                      F_DKS1 + 1, F_DKS1 + 2, F_DKS1 + 3, F_TRPS1,   F_TRPS1 + 1,
                                      F_TRPS1 + 2, F_TRPS1 + 3, F_MT_DELAY};
    KeyRecord recs[kMaxKeyRecords];
    size_t n = 0;
    for (uint8_t f : kFields) recs[n++] = {kv, f, 0};
    Send(KeyPack(false, recs, n), 2);
}

void Keyboard::Tick() {
    if (dev_.IsOpen() && !dev_.IsAlive()) {
        Disconnect();
        Rescan();
        return;
    }
    DWORD now = GetTickCount();
    if (st_.conn == ConnState::Syncing && now - syncSentAt_ > kSyncTimeoutMs) {
        if (syncAttempts_ < 3) {
            StartInit();
        } else {
            st_.conn = ConnState::Error;
            st_.error = "Klavye cevap vermiyor.";
        }
    }
    if (st_.conn == ConnState::Loading && st_.keysTotalPackets > 0 && !Busy()) {
        st_.conn = ConnState::Ready;  // some reads went unanswered; show what we have
    }
    if (live_ != LiveMode::Off && now - lastLiveAt_ > kLiveStallMs && !Busy()) {
        for (auto& q : pendingHalves_) q.clear();
        RequestLiveNext();
    }
}

std::string Keyboard::StatusText() const {
    switch (st_.conn) {
        case ConnState::NoDevice: return "Klavye bulunamadı";
        case ConnState::Opening: return "Bağlanıyor…";
        case ConnState::Syncing: return "Klavye tanınıyor…";
        case ConnState::Loading: return "Ayarlar okunuyor…";
        case ConnState::Ready: return "Bağlı";
        case ConnState::Bootloader: return "Klavye bootloader modunda (firmware güncellemesini siteden yapın)";
        case ConnState::Error: return st_.error;
    }
    return {};
}

void Keyboard::OnHidNotify() {
    for (auto& report : dev_.TakeInputReports()) {
        if (!rx_.empty()) {
            rx_.insert(rx_.end(), report.begin(), report.end());
            if (rx_.size() >= kLiveResponseBytes) {
                Handle(rx_.data(), rx_.size());
                rx_.clear();
            }
            continue;
        }
        if (report.size() >= 4 && report[2] == (RM6X21 | kResponseBit)) {
            rx_ = std::move(report);
            continue;
        }
        Handle(report.data(), report.size());
    }
    if (dev_.IsOpen() && !dev_.IsAlive()) {
        Disconnect();
        Rescan();
    }
}

void Keyboard::Handle(const uint8_t* p, size_t len) {
    if (len < 64) return;
    const uint8_t cmd = p[2];
    const uint8_t* a = p + 4;  // payload; a[0] is a status / read-write echo byte

    switch (cmd) {
        case CMD | kResponseBit: HandleCmd(a); break;
        case SYNC | kResponseBit: HandleSync(a); break;
        case DEFKEY | kResponseBit: {
            for (int half = 0; half < 2; ++half) {
                const uint8_t* rowData = a + 1 + half * 22;
                uint8_t row = rowData[0];
                if (row >= kRows) continue;
                if (row == 0) {
                    for (auto& k : st_.keys) k.present = false;
                    for (auto& r : st_.matrix) r.fill(0);
                }
                for (int c = 0; c < kCols; ++c) {
                    uint8_t kv = rowData[1 + c];
                    st_.matrix[row][c] = kv;
                    if (kv == 0) continue;
                    KeyState& k = st_.keys[kv];
                    k.present = true;
                    k.row = row;
                    k.col = static_cast<uint8_t>(c);
                    if (k.layers[0] == 0) k.layers[0] = kv;
                }
                if (row == kRows - 1 && st_.conn == ConnState::Loading) {
                    ReadKeyFields();
                    ReadKeyColors();
                }
            }
            break;
        }
        case KEY | kResponseBit: {
            for (int i = 0; i < kMaxKeyRecords; ++i) {
                const uint8_t* r = a + 1 + i * 4;
                uint8_t kv = r[0], field = r[1];
                uint16_t v = Le16(r + 2);
                if (kv == 0) break;
                if (kv == 0xFF) continue;
                KeyState& k = st_.keys[kv];
                if (field <= 3) {
                    k.layers[field] = v;
                } else if (field >= F_DB0 && field <= F_DB3) {
                    k.db[field - F_DB0] = v;
                } else if (field == F_MODE) {
                    k.touchMode = (v >> 4) & 0x0F;
                    k.advType = v & 0x0F;
                } else if (field >= F_DKS1 && field < F_DKS1 + 4) {
                    k.dks[field - F_DKS1] = v;
                } else if (field >= F_TRPS1 && field < F_TRPS1 + 4) {
                    k.trps[field - F_TRPS1] = static_cast<uint8_t>(v);
                } else if (field == F_MT_DELAY) {
                    k.mtDelayMs = static_cast<uint16_t>(v * 10);
                } else if (field == F_RT_PRESS) {
                    k.rtPress = v;
                } else if (field == F_RT_RELEASE) {
                    k.rtRelease = v;
                } else if (field == F_DEAD_PRESS) {
                    k.deadPress = v;
                } else if (field == F_DEAD_RELEASE) {
                    k.deadRelease = v;
                } else if (field == F_RELEASE_POINT) {
                    k.releasePoint = v;
                } else if (field == F_AXIS) {
                    k.axis = v;
                }
            }
            if (st_.conn == ConnState::Loading && a[0] == 0 &&
                ++st_.keysLoadedPackets >= st_.keysTotalPackets) {
                st_.conn = ConnState::Ready;
            }
            break;
        }
        case DB | kResponseBit:
            st_.globalActuation = Le16(a + 3);
            st_.globalDeadPress = Le16(a + 5);
            st_.globalDeadRelease = Le16(a + 7);
            break;
        case PRGB | kResponseBit: HandleLight(a, st_.mainLight); break;
        case LOGORGB | kResponseBit:
            st_.hasLogoLight = true;
            HandleLight(a, st_.logoLight);
            break;
        case KRGB | kResponseBit:
            for (int i = 0; i < kMaxKeyRecords; ++i) {
                const uint8_t* r = a + 1 + i * 4;
                if (r[0] == 0 || r[0] == 0xFF) continue;
                st_.keys[r[0]].color = (r[1] << 16) | (r[2] << 8) | r[3];
            }
            break;
        case MACROMODE | kResponseBit:
            st_.macroMode.valid = true;
            st_.macroMode.macroId = Le16(a + 2);
            st_.macroMode.mode = a[5];
            st_.macroMode.repeatCount = Le16(a + 6);
            st_.macroMode.repeatDelay = a[8] | (a[9] << 8) | (a[10] << 16);
            break;
        case SOCD | kResponseBit:
            st_.socd.valid = true;
            st_.socd.key1 = a[1];
            st_.socd.key2 = a[2];
            st_.socd.val1 = Le16(a + 3);
            st_.socd.val2 = Le16(a + 5);
            st_.socd.type = a[7];
            st_.socd.mode = a[8];
            break;
        case RM6X21 | kResponseBit:
            if (len >= kLiveResponseBytes) HandleLive(a);
            break;
    }
}

void Keyboard::HandleSync(const uint8_t* a) {
    st_.boardId = a[1] | (a[2] << 8) | (a[3] << 16) | (static_cast<uint32_t>(a[4]) << 24);
    st_.keyType = a[3];
    st_.layoutId = a[4];
    st_.serial = Text(a + 9, 16);
    bool boot = memcmp(a + 26, "Boot", 4) == 0;
    st_.firmware = Text(a + 26, boot ? 11 : 10);
    st_.buildDate = Text(a + 43, 11);

    if (a[7] == 0xFF) {
        st_.conn = ConnState::Bootloader;
        return;
    }
    if (st_.conn != ConnState::Syncing) return;
    st_.conn = ConnState::Loading;
    st_.keysLoadedPackets = st_.keysTotalPackets = 0;

    Send(CmdPack(PROTOCOL_VERSION));
    Send(CmdPack(QUERY_NAME));
    Send(CmdPack(QUERY_PRECISION));
    Send(CmdPack(CONFIG_ID, 4));
    Send(CmdPack(AXIS_LIST));
    Send(CmdPack(POLLING_RATE, 7));
    Send(CmdPack(QUERY_CASE_COLOR, 100));
    Send(CmdPack(DEFAULT_AXIS, 100));
    Send(CmdPack(QUERY_SYS_WIN));
    Send(CmdPack(QUERY_SYS_MAC));
    Send(CmdPack(TOP_DEAD_SWITCH, 2));
    Send(DbPack(false, 0, 0, 0));
    Send(LightPack(PRGB, false, LightState()), 50);
    Send(LightPack(LOGORGB, false, LightState()), 50);
    Send(DefKeyPack(0, 1));
    Send(DefKeyPack(2, 3));
    Send(DefKeyPack(4, 5), 500);  // the web app lets the board settle before the bulk key read
}

void Keyboard::HandleCmd(const uint8_t* a) {
    switch (a[1]) {
        case PROTOCOL_VERSION:
            st_.protocol = (a[3] & 0x0F) * 10000 + ((a[2] >> 4) & 0x0F) * 100 + (a[2] & 0x0F);
            break;
        case QUERY_NAME: {
            std::string name = Text(a + 2, 32);
            if (!name.empty()) st_.name = name;
            break;
        }
        case QUERY_PRECISION:
            st_.precision = a[2] ? a[2] : 10;
            // The Slice75 reports 0 as minimum; keep a floor so an actuation point of 0 can't be sent.
            st_.minTravel = std::max<uint16_t>(Le16(a + 3), 100);
            st_.maxTravel = Le16(a + 5);
            if (st_.maxTravel == 0) st_.maxTravel = 4000;
            break;
        case CONFIG_ID: st_.configId = a[2]; break;
        case AXIS_LIST:
            st_.switches.clear();
            for (int n = 0; n < 8; ++n) {
                uint16_t id = static_cast<uint16_t>((a[2 + 2 * n] << 8) | a[3 + 2 * n]);  // big-endian
                if (id == 0 || id == 0xFFFF) break;
                st_.switches.push_back({id, SwitchName(id), SwitchMaxTravel(id)});
            }
            break;
        case POLLING_RATE: st_.pollingIndex = a[2]; break;
        case QUERY_CASE_COLOR: st_.caseColor = a[2]; break;
        case DEFAULT_AXIS: st_.defaultAxis = a[2]; break;
        case QUERY_SYS_WIN:
            if (a[2] != 0xFF) st_.sysMode = a[2] == 1 ? 0 : 1;
            break;
        case QUERY_SYS_MAC:
            if (a[2] != 0xFF) st_.sysMode = a[2] == 1 ? 1 : 0;
            break;
        case SET_SYS_WIN:
            if (a[2] == 1) st_.sysMode = 0;
            break;
        case SET_SYS_MAC:
            if (a[2] == 1) st_.sysMode = 1;
            break;
        case TOP_DEAD_SWITCH: st_.topDead = a[2] != 0 ? 1 : 0; break;
    }
}

void Keyboard::HandleLight(const uint8_t* a, LightState& s) {
    for (int n = 0; n < 7; ++n) {
        const uint8_t* c = a + 5 + n * 4;  // B, G, R, pad
        s.colors[n] = (c[2] << 16) | (c[1] << 8) | c[0];
    }
    s.on = (a[37] & 0x01) != 0;
    s.reverse = (a[37] & 0x02) != 0;
    s.superResponse = (a[37] & 0x10) != 0;
    s.brightness = a[38];
    s.mode = a[39];
    s.speed = a[40];
    s.sleepMinutes = a[41];
    s.staticSlot = a[42];
}

void Keyboard::HandleLive(const uint8_t* a) {
    const uint8_t sub = a[1];
    const uint8_t* d = a + 2;
    ++liveFrames_;
    auto popHalf = [&](int s) -> uint8_t {
        if (pendingHalves_[s].empty()) return 1;
        uint8_t h = pendingHalves_[s].front();
        pendingHalves_[s].pop_front();
        return h;
    };

    if (sub == 2 || sub == 6) {
        int firstRow = popHalf(sub) == 1 ? 0 : 3;
        for (int r = firstRow; r < firstRow + 3; ++r) {
            for (int c = 0; c < kCols; ++c, d += 2) {
                uint8_t kv = st_.matrix[r][c];
                if (kv == 0) continue;
                (sub == 2 ? st_.keys[kv].liveTravel : st_.keys[kv].liveAdc) = Le16(d);
            }
        }
    } else if (sub == 3) {
        popHalf(3);
        for (int r = 0; r < kRows; ++r) {
            for (int c = 0; c < kCols; ++c, ++d) {
                if (uint8_t kv = st_.matrix[r][c]) st_.keys[kv].livePress = *d;
            }
        }
    }

    if ((live_ == LiveMode::TravelTest && sub == 2) || (live_ == LiveMode::Calibration && sub == 6)) {
        RequestLiveNext();
    }
}

void Keyboard::RequestLiveNext() {
    lastLiveAt_ = GetTickCount();
    uint8_t h = nextHalf_;
    nextHalf_ = h == 1 ? 2 : 1;
    if (live_ == LiveMode::TravelTest) {
        pendingHalves_[3].push_back(1);
        Send(RmPack(3, 1), 2);
        pendingHalves_[2].push_back(h);
        Send(RmPack(2, h), 2);
    } else if (live_ == LiveMode::Calibration) {
        pendingHalves_[2].push_back(h);
        Send(RmPack(2, h), 5);
        pendingHalves_[6].push_back(h);
        Send(RmPack(6, h), 5);
    }
}

void Keyboard::SetLiveMode(LiveMode m) {
    if (m == live_ || st_.conn != ConnState::Ready) return;
    if (live_ == LiveMode::Calibration) Send(CmdPack(SAVE_CALIBRATION), 5);
    for (auto& q : pendingHalves_) q.clear();
    for (auto& k : st_.keys) {
        k.liveTravel = 0;
        k.liveAdc = 0;
        k.livePress = 0;
    }
    if (m == LiveMode::Calibration) Send(CmdPack(START_CALIBRATION), 5);
    live_ = m;
    if (m != LiveMode::Off) RequestLiveNext();
}

void Keyboard::ReadKeyFields() {
    std::vector<uint8_t> keys = st_.AllKeys();
    std::vector<uint8_t> fields = {F_MODE};
    for (uint8_t f = 0; f < F_COUNT; ++f) fields.push_back(f);

    st_.keysLoadedPackets = 0;
    st_.keysTotalPackets = 0;
    for (uint8_t f : fields) {
        for (size_t i = 0; i < keys.size(); i += kMaxKeyRecords) {
            KeyRecord recs[kMaxKeyRecords];
            size_t n = std::min<size_t>(kMaxKeyRecords, keys.size() - i);
            for (size_t j = 0; j < n; ++j) recs[j] = {keys[i + j], f, 0};
            Send(KeyPack(false, recs, n), 1);
            ++st_.keysTotalPackets;
        }
    }
}

void Keyboard::ReadFields(uint8_t kv, const std::vector<uint8_t>& fields) {
    KeyRecord recs[kMaxKeyRecords];
    size_t n = std::min<size_t>(fields.size(), kMaxKeyRecords);
    for (size_t i = 0; i < n; ++i) recs[i] = {kv, fields[i], 0};
    Send(KeyPack(false, recs, n), 2);
}

void Keyboard::WriteKeyRecords(const std::vector<KeyRecord>& recs) {
    for (size_t i = 0; i < recs.size(); i += kMaxKeyRecords) {
        size_t n = std::min<size_t>(kMaxKeyRecords, recs.size() - i);
        Send(KeyPack(true, recs.data() + i, n), 2);
    }
}

void Keyboard::WriteMainLight() { Send(LightPack(PRGB, true, st_.mainLight), 5); }

void Keyboard::WriteLogoLight() {
    LightState s = st_.logoLight;
    s.superResponse = true;  // the official logo page always sets this bit
    s.sleepMinutes = 0;
    Send(LightPack(LOGORGB, true, s), 5);
}

void Keyboard::WriteGlobalDb() {
    Send(DbPack(true, st_.globalActuation, st_.globalDeadPress, st_.globalDeadRelease));
}

void Keyboard::WriteKeyColors(const std::vector<uint8_t>& keys) {
    for (uint8_t kv : keys) {
        uint32_t c = st_.keys[kv].color;
        Send(KrgbPack(true, &kv, &c, 1), 2);
    }
}

void Keyboard::ReadKeyColors() {
    std::vector<uint8_t> keys = st_.AllKeys();
    for (size_t i = 0; i < keys.size(); i += kMaxKeyRecords) {
        size_t n = std::min<size_t>(kMaxKeyRecords, keys.size() - i);
        Send(KrgbPack(false, keys.data() + i, nullptr, n), 2);
    }
}
