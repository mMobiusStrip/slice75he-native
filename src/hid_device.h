// Thin Win32 HID transport: enumerate, open, background reads, report writes.
#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct HidDeviceInfo {
    std::wstring path;
    uint16_t vendorId = 0;
    uint16_t productId = 0;
    uint16_t usagePage = 0;
    uint16_t usage = 0;
    uint16_t inputReportLen = 0;   // includes the report ID byte
    uint16_t outputReportLen = 0;  // includes the report ID byte
    std::string productName;       // UTF-8
};

struct HidFilter {
    uint16_t vendorId;
    uint16_t productId;
    uint16_t usagePage;
    uint16_t usage;
};

std::vector<HidDeviceInfo> EnumerateHidDevices(const std::vector<HidFilter>& filters);

class HidDevice {
public:
    HidDevice() = default;
    ~HidDevice();
    HidDevice(const HidDevice&) = delete;
    HidDevice& operator=(const HidDevice&) = delete;

    // notifyWindow/notifyMsg: posted (no payload) whenever new input reports are queued.
    bool Open(const HidDeviceInfo& info, HWND notifyWindow, UINT notifyMsg);
    void Close();
    bool IsOpen() const { return handle_ != INVALID_HANDLE_VALUE; }
    bool IsAlive() const { return IsOpen() && !readFailed_; }
    const HidDeviceInfo& Info() const { return info_; }

    // Sends one output report; data is padded with zeros to the report length.
    bool Write(uint8_t reportId, const uint8_t* data, size_t len);

    // Drains input reports received so far (report ID byte stripped).
    std::vector<std::vector<uint8_t>> TakeInputReports();

private:
    void ReadLoop();

    HidDeviceInfo info_;
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    HANDLE stopEvent_ = nullptr;
    HWND notifyWindow_ = nullptr;
    UINT notifyMsg_ = 0;
    std::thread reader_;
    std::atomic<bool> readFailed_{false};
    std::mutex queueMutex_;
    std::deque<std::vector<uint8_t>> queue_;
};
