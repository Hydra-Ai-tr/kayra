#include "core/system.h"

#include <windows.h>
#include <objidl.h>
#include <powrprof.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>

#include <ctime>
#include <mutex>
#include <vector>

#include "core/util.h"

namespace kayra {

namespace {

struct ComScope {
    HRESULT hr;
    ComScope() : hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComScope() {
        if (SUCCEEDED(hr)) CoUninitialize();
    }
};

template <class T>
struct ComPtr {
    T* p = nullptr;
    ~ComPtr() {
        if (p) p->Release();
    }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

const GUID kClsidMMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
const GUID kIidIMMDeviceEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
const GUID kIidIAudioEndpointVolume = {0x5CDF2C82, 0x841E, 0x4546, {0x97, 0x22, 0x0C, 0xF7, 0x40, 0x78, 0x22, 0x9A}};

bool endpoint(ComPtr<IAudioEndpointVolume>& vol) {
    ComPtr<IMMDeviceEnumerator> en;
    if (FAILED(CoCreateInstance(kClsidMMDeviceEnumerator, nullptr, CLSCTX_ALL, kIidIMMDeviceEnumerator, (void**)&en)))
        return false;
    ComPtr<IMMDevice> dev;
    if (FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))) return false;
    return SUCCEEDED(dev->Activate(kIidIAudioEndpointVolume, CLSCTX_ALL, nullptr, (void**)&vol));
}

uint64_t ft(const FILETIME& f) { return ((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime; }

}  // namespace

SysStats system_stats() {
    SysStats s;
    static std::mutex mu;
    static uint64_t prev_idle = 0, prev_total = 0;
    static double last_cpu = 0;
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        std::lock_guard<std::mutex> lk(mu);
        uint64_t i = ft(idle), t = ft(kernel) + ft(user);
        if (!prev_total) {
            // first call: measure over a short window instead of reporting 0 %
            prev_idle = i;
            prev_total = t;
            Sleep(250);
            GetSystemTimes(&idle, &kernel, &user);
            i = ft(idle);
            t = ft(kernel) + ft(user);
        }
        if (t > prev_total) {
            double di = (double)(i - prev_idle), dt = (double)(t - prev_total);
            last_cpu = std::clamp(100.0 * (1.0 - di / dt), 0.0, 100.0);
        }
        prev_idle = i;
        prev_total = t;
        s.cpu = last_cpu;
    }
    MEMORYSTATUSEX m{};
    m.dwLength = sizeof m;
    if (GlobalMemoryStatusEx(&m)) {
        s.ram_total_gb = m.ullTotalPhys / 1073741824.0;
        s.ram_used_gb = (m.ullTotalPhys - m.ullAvailPhys) / 1073741824.0;
        s.ram = m.dwMemoryLoad;
    }
    SYSTEM_POWER_STATUS p{};
    if (GetSystemPowerStatus(&p) && p.BatteryFlag != 128 && p.BatteryFlag != 255 && p.BatteryLifePercent <= 100) {
        s.battery = p.BatteryLifePercent;
        s.charging = p.ACLineStatus == 1;
    }
    ULARGE_INTEGER freeb, total, totfree;
    if (GetDiskFreeSpaceExW(L"C:\\", &freeb, &total, &totfree)) {
        s.disk_free_gb = freeb.QuadPart / 1073741824.0;
        s.disk_total_gb = total.QuadPart / 1073741824.0;
    }
    s.uptime_s = GetTickCount64() / 1000;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof pe;
        for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) s.processes++;
        CloseHandle(snap);
    }
    return s;
}

std::string cpu_name() {
    wchar_t buf[256];
    DWORD sz = sizeof buf;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString",
                     RRF_RT_REG_SZ, nullptr, buf, &sz) == ERROR_SUCCESS)
        return trim(narrow(buf));
    return "CPU";
}

std::string computer_name() {
    wchar_t buf[256];
    DWORD n = 256;
    if (GetComputerNameW(buf, &n)) return narrow(std::wstring(buf, n));
    return "PC";
}

int volume_get() {
    ComScope com;
    ComPtr<IAudioEndpointVolume> v;
    float f = 0;
    if (!endpoint(v) || FAILED(v->GetMasterVolumeLevelScalar(&f))) return -1;
    return (int)(f * 100 + 0.5f);
}

bool volume_set(int percent) {
    ComScope com;
    ComPtr<IAudioEndpointVolume> v;
    if (!endpoint(v)) return false;
    percent = std::clamp(percent, 0, 100);
    if (percent > 0) v->SetMute(FALSE, nullptr);
    return SUCCEEDED(v->SetMasterVolumeLevelScalar(percent / 100.0f, nullptr));
}

bool volume_mute(bool mute) {
    ComScope com;
    ComPtr<IAudioEndpointVolume> v;
    return endpoint(v) && SUCCEEDED(v->SetMute(mute ? TRUE : FALSE, nullptr));
}

bool volume_is_muted() {
    ComScope com;
    ComPtr<IAudioEndpointVolume> v;
    BOOL m = FALSE;
    return endpoint(v) && SUCCEEDED(v->GetMute(&m)) && m;
}

void media_key(MediaKey k) {
    WORD vk = VK_MEDIA_PLAY_PAUSE;
    if (k == MediaKey::Next) vk = VK_MEDIA_NEXT_TRACK;
    if (k == MediaKey::Previous) vk = VK_MEDIA_PREV_TRACK;
    if (k == MediaKey::Stop) vk = VK_MEDIA_STOP;
    INPUT in[2]{};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = vk;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

std::string take_screenshot() {
    static ULONG_PTR token = [] {
        ULONG_PTR t = 0;
        Gdiplus::GdiplusStartupInput in;
        Gdiplus::GdiplusStartup(&t, &in, nullptr);
        return t;
    }();
    (void)token;
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN), y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN), h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, screen, x, y, SRCCOPY | CAPTUREBLT);
    SelectObject(mem, old);

    std::string dir = known_folder("pictures") + "\\Kayra";
    CreateDirectoryW(widen(dir).c_str(), nullptr);
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    char name[64];
    std::strftime(name, sizeof name, "\\ekran-%Y%m%d-%H%M%S.png", &tm);
    std::string path = dir + name;

    bool ok = false;
    {
        Gdiplus::Bitmap image(bmp, nullptr);
        UINT num = 0, size = 0;
        Gdiplus::GetImageEncodersSize(&num, &size);
        std::vector<char> buf(size);
        auto* enc = (Gdiplus::ImageCodecInfo*)buf.data();
        Gdiplus::GetImageEncoders(num, size, enc);
        for (UINT i = 0; i < num; i++) {
            if (wcscmp(enc[i].MimeType, L"image/png") == 0) {
                ok = image.Save(widen(path).c_str(), &enc[i].Clsid, nullptr) == Gdiplus::Ok;
                break;
            }
        }
    }
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return ok ? path : "";
}

bool lock_workstation() { return LockWorkStation() != 0; }

static bool run_hidden(const std::wstring& cmdline) {
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    std::wstring cl = cmdline;
    if (!CreateProcessW(nullptr, cl.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, 5000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0;
}

bool power_action(const std::string& action, int delay_s) {
    std::wstring d = std::to_wstring(std::max(0, delay_s));
    if (action == "shutdown") return run_hidden(L"shutdown.exe /s /t " + d);
    if (action == "restart") return run_hidden(L"shutdown.exe /r /t " + d);
    if (action == "cancel") return run_hidden(L"shutdown.exe /a");
    if (action == "sleep") return SetSuspendState(FALSE, FALSE, FALSE) != 0;
    return false;
}

std::string clipboard_text() {
    std::string out;
    if (!OpenClipboard(nullptr)) return out;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        auto* p = (const wchar_t*)GlobalLock(h);
        if (p) {
            out = narrow(p);
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}

bool set_clipboard_text(const std::string& text) {
    std::wstring w = widen(text);
    if (!OpenClipboard(nullptr)) return false;
    EmptyClipboard();
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
    bool ok = false;
    if (g) {
        memcpy(GlobalLock(g), w.c_str(), (w.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(g);
        ok = SetClipboardData(CF_UNICODETEXT, g) != nullptr;
        if (!ok) GlobalFree(g);
    }
    CloseClipboard();
    return ok;
}

bool run_command(const std::string& command, std::string& output, int timeout_ms, size_t max_bytes) {
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = si.hStdError = wr;
    si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    std::wstring cl = L"cmd.exe /d /c chcp 65001>nul & " + widen(command);
    if (!CreateProcessW(nullptr, cl.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(rd);
        CloseHandle(wr);
        return false;
    }
    CloseHandle(wr);
    double start = now_seconds();
    char buf[4096];
    bool timed_out = false;
    for (;;) {
        DWORD avail = 0;
        if (PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            DWORD got = 0;
            if (ReadFile(rd, buf, std::min<DWORD>(avail, sizeof buf), &got, nullptr) && got > 0 &&
                output.size() < max_bytes * 2)
                output.append(buf, got);
            continue;
        }
        if (WaitForSingleObject(pi.hProcess, 30) == WAIT_OBJECT_0) {
            DWORD got = 0;
            while (PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) && avail > 0 &&
                   ReadFile(rd, buf, std::min<DWORD>(avail, sizeof buf), &got, nullptr) && got > 0)
                output.append(buf, got);
            break;
        }
        if ((now_seconds() - start) * 1000 > timeout_ms) {
            TerminateProcess(pi.hProcess, 1);
            timed_out = true;
            break;
        }
    }
    CloseHandle(rd);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (output.size() > max_bytes) output = output.substr(0, max_bytes) + "\n...";
    if (timed_out) output += "\n[timeout]";
    return true;
}

bool shell_open(const std::string& target, const std::string& args) {
    HINSTANCE r = ShellExecuteW(nullptr, L"open", widen(target).c_str(), args.empty() ? nullptr : widen(args).c_str(),
                                nullptr, SW_SHOWNORMAL);
    return (INT_PTR)r > 32;
}

bool set_start_with_windows(bool on) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &k) !=
        ERROR_SUCCESS)
        return false;
    LONG r;
    if (on) {
        wchar_t path[MAX_PATH * 2];
        DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH * 2);
        std::wstring v = L"\"" + std::wstring(path, n) + L"\" --minimized";
        r = RegSetValueExW(k, L"Kayra", 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    } else {
        r = RegDeleteValueW(k, L"Kayra");
        if (r == ERROR_FILE_NOT_FOUND) r = ERROR_SUCCESS;
    }
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

}  // namespace kayra
