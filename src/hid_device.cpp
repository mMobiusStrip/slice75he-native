#include "hid_device.h"

#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>

#include <cstring>

namespace {

std::string ToUtf8(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

bool QueryDevice(const std::wstring& path, HidDeviceInfo& info) {
    // Zero access rights are enough to read attributes, even for devices the OS holds exclusively.
    HANDLE h = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    HIDD_ATTRIBUTES attr{};
    attr.Size = sizeof(attr);
    PHIDP_PREPARSED_DATA pp = nullptr;
    HIDP_CAPS caps{};
    bool ok = HidD_GetAttributes(h, &attr) && HidD_GetPreparsedData(h, &pp);
    if (ok) {
        ok = HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS;
        HidD_FreePreparsedData(pp);
    }
    if (ok) {
        wchar_t name[128] = {};
        if (HidD_GetProductString(h, name, sizeof(name))) info.productName = ToUtf8(name);
        info.path = path;
        info.vendorId = attr.VendorID;
        info.productId = attr.ProductID;
        info.usagePage = caps.UsagePage;
        info.usage = caps.Usage;
        info.inputReportLen = caps.InputReportByteLength;
        info.outputReportLen = caps.OutputReportByteLength;
    }
    CloseHandle(h);
    return ok;
}

bool Matches(const HidDeviceInfo& d, const std::vector<HidFilter>& filters) {
    for (const HidFilter& f : filters) {
        if (f.vendorId == d.vendorId && f.productId == d.productId && f.usagePage == d.usagePage &&
            f.usage == d.usage) {
            return true;
        }
    }
    return filters.empty();
}

}  // namespace

std::vector<HidDeviceInfo> EnumerateHidDevices(const std::vector<HidFilter>& filters) {
    std::vector<HidDeviceInfo> out;
    GUID guid;
    HidD_GetHidGuid(&guid);
    HDEVINFO set = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return out;

    SP_DEVICE_INTERFACE_DATA ifd{};
    ifd.cbSize = sizeof(ifd);
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &guid, i, &ifd); ++i) {
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &size, nullptr);
        if (size == 0) continue;
        std::vector<BYTE> raw(size);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(raw.data());
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &ifd, detail, size, nullptr, nullptr)) continue;

        HidDeviceInfo info;
        if (QueryDevice(detail->DevicePath, info) && Matches(info, filters)) out.push_back(std::move(info));
    }
    SetupDiDestroyDeviceInfoList(set);
    return out;
}

HidDevice::~HidDevice() { Close(); }

bool HidDevice::Open(const HidDeviceInfo& info, HWND notifyWindow, UINT notifyMsg) {
    Close();
    handle_ = CreateFileW(info.path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) return false;
    info_ = info;
    notifyWindow_ = notifyWindow;
    notifyMsg_ = notifyMsg;
    readFailed_ = false;
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    reader_ = std::thread(&HidDevice::ReadLoop, this);
    return true;
}

void HidDevice::Close() {
    if (handle_ == INVALID_HANDLE_VALUE) return;
    SetEvent(stopEvent_);
    if (reader_.joinable()) reader_.join();
    CloseHandle(handle_);
    CloseHandle(stopEvent_);
    handle_ = INVALID_HANDLE_VALUE;
    stopEvent_ = nullptr;
    std::lock_guard<std::mutex> lock(queueMutex_);
    queue_.clear();
}

bool HidDevice::Write(uint8_t reportId, const uint8_t* data, size_t len) {
    if (!IsOpen() || info_.outputReportLen == 0 || len + 1 > info_.outputReportLen) return false;
    std::vector<uint8_t> buf(info_.outputReportLen, 0);
    buf[0] = reportId;
    memcpy(buf.data() + 1, data, len);

    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD written = 0;
    BOOL ok = WriteFile(handle_, buf.data(), static_cast<DWORD>(buf.size()), nullptr, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, 1000) != WAIT_OBJECT_0) CancelIoEx(handle_, &ov);
        ok = GetOverlappedResult(handle_, &ov, &written, TRUE);
    }
    CloseHandle(ov.hEvent);
    return ok != FALSE;
}

std::vector<std::vector<uint8_t>> HidDevice::TakeInputReports() {
    std::lock_guard<std::mutex> lock(queueMutex_);
    std::vector<std::vector<uint8_t>> out(std::make_move_iterator(queue_.begin()),
                                          std::make_move_iterator(queue_.end()));
    queue_.clear();
    return out;
}

void HidDevice::ReadLoop() {
    std::vector<uint8_t> buf(info_.inputReportLen ? info_.inputReportLen : 65);
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE waits[2] = {ov.hEvent, stopEvent_};

    for (;;) {
        ResetEvent(ov.hEvent);
        if (!ReadFile(handle_, buf.data(), static_cast<DWORD>(buf.size()), nullptr, &ov) &&
            GetLastError() != ERROR_IO_PENDING) {
            readFailed_ = true;
            break;
        }
        DWORD got = 0;
        if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0) {
            CancelIoEx(handle_, &ov);
            GetOverlappedResult(handle_, &ov, &got, TRUE);
            break;
        }
        if (!GetOverlappedResult(handle_, &ov, &got, FALSE)) {
            readFailed_ = true;
            break;
        }
        if (got <= 1) continue;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            queue_.emplace_back(buf.begin() + 1, buf.begin() + got);
        }
        PostMessageW(notifyWindow_, notifyMsg_, 0, 0);
    }
    CloseHandle(ov.hEvent);
    if (readFailed_) PostMessageW(notifyWindow_, notifyMsg_, 0, 0);
}
