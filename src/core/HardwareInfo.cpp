#include "core/HardwareInfo.h"
#include "core/Settings.h"
#include <QFile>
#include <QRegularExpression>
#include <QSysInfo>
#include <QThread>
#include <QTextStream>

#ifdef _WIN32
#include <windows.h>
#include <dxgi.h>
#include <psapi.h>
#include <pdh.h>
#include <vector>
#pragma comment(lib, "dxgi.lib")
#endif

namespace bc {

bool HardwareInfo::hasNvidia() const { for (auto &g : gpus) if (g.vendor == "nvidia") return true; return false; }
bool HardwareInfo::hasAmd() const    { for (auto &g : gpus) if (g.vendor == "amd") return true; return false; }
bool HardwareInfo::hasIntel() const  { for (auto &g : gpus) if (g.vendor == "intel") return true; return false; }

QString HardwareInfo::recommendedProfile() const {
    const bool dGpu = hasNvidia() || hasAmd();
    if (logicalCores >= 16 && ramMB >= 30000 && dGpu) return "pro";
    if (logicalCores >= 12 && ramMB >= 15000 && dGpu) return "high";
    if (logicalCores >= 6 && ramMB >= 7500) return "balanced";
    return "low";
}

HardwareInfo HardwareInfo::detect() {
    HardwareInfo hw;
    hw.logicalCores = QThread::idealThreadCount();
#ifdef _WIN32
    MEMORYSTATUSEX ms{sizeof(ms)};
    if (GlobalMemoryStatusEx(&ms)) hw.ramMB = ms.ullTotalPhys / (1024 * 1024);

    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      0, KEY_READ, &key) == ERROR_SUCCESS) {
        wchar_t buf[256]; DWORD sz = sizeof(buf);
        if (RegQueryValueExW(key, L"ProcessorNameString", nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(buf), &sz) == ERROR_SUCCESS)
            hw.cpuName = QString::fromWCharArray(buf).trimmed();
        RegCloseKey(key);
    }

    IDXGIFactory1 *factory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void **>(&factory)))) {
        IDXGIAdapter1 *adapter = nullptr;
        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 d{};
            adapter->GetDesc1(&d);
            if (!(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                GpuInfo g;
                g.name = QString::fromWCharArray(d.Description);
                g.vramMB = d.DedicatedVideoMemory / (1024 * 1024);
                g.vendor = d.VendorId == 0x10DE ? "nvidia" : d.VendorId == 0x1002 ? "amd"
                         : d.VendorId == 0x8086 ? "intel" : "other";
                hw.gpus << g;
            }
            adapter->Release();
        }
        factory->Release();
    }
#else
    QFile mi("/proc/meminfo");
    if (mi.open(QIODevice::ReadOnly)) {
        const QString first = QString::fromUtf8(mi.readLine());
        hw.ramMB = first.section(QRegularExpression("\\s+"), 1, 1).toULongLong() / 1024;
    }
    QFile ci("/proc/cpuinfo");
    if (ci.open(QIODevice::ReadOnly)) {
        while (!ci.atEnd()) {
            const QString l = QString::fromUtf8(ci.readLine());
            if (l.startsWith("model name")) { hw.cpuName = l.section(':', 1).trimmed(); break; }
        }
    }
#endif
    if (hw.cpuName.isEmpty()) hw.cpuName = QSysInfo::currentCpuArchitecture();
    return hw;
}

void applyPerformanceProfile(const QString &profile, const QString &enc) {
    auto &s = Settings::instance();
    struct P { int w, h, fps, kbps; const char *preset; };
    P p = {1920, 1080, 60, 6000, "balanced"};
    if (profile == "low")      p = {1280, 720, 30, 3000, "performance"};
    else if (profile == "balanced") p = {1920, 1080, 30, 4500, "balanced"};
    else if (profile == "high")     p = {1920, 1080, 60, 6000, "quality"};
    else if (profile == "pro")      p = {2560, 1440, 60, 9000, "quality"};
    s.set("video.outWidth", p.w);
    s.set("video.outHeight", p.h);
    s.set("video.fpsNum", p.fps);
    s.set("video.fpsDen", 1);
    s.set("output.bitrate", p.kbps);
    s.set("output.preset", QString::fromLatin1(p.preset));
    s.set("output.performanceProfile", profile);
    if (!enc.isEmpty()) s.set("output.encoder", enc);
}

// ---------------- usage sampler ----------------
double UsageSampler::cpuPercent() {
    quint64 idle = 0, total = 0;
#ifdef _WIN32
    FILETIME fi, fk, fu;
    if (!GetSystemTimes(&fi, &fk, &fu)) return 0;
    auto v = [](FILETIME f) { return (quint64(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    idle = v(fi); total = v(fk) + v(fu);   // kernel time includes idle
#else
    QFile f("/proc/stat");
    if (!f.open(QIODevice::ReadOnly)) return 0;
    const auto parts = QString::fromUtf8(f.readLine()).simplified().split(' ');
    for (int i = 1; i < parts.size(); ++i) total += parts[i].toULongLong();
    if (parts.size() > 5) idle = parts[4].toULongLong() + parts[5].toULongLong();
#endif
    const quint64 dIdle = idle - m_lastIdle, dTotal = total - m_lastTotal;
    const bool first = m_lastTotal == 0;
    m_lastIdle = idle; m_lastTotal = total;
    if (first || dTotal == 0) return 0;
    return 100.0 * double(dTotal - dIdle) / double(dTotal);
}

quint64 UsageSampler::processMemoryMB() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return pmc.WorkingSetSize / (1024 * 1024);
    return 0;
#else
    QFile f("/proc/self/statm");
    if (!f.open(QIODevice::ReadOnly)) return 0;
    const auto parts = QString::fromUtf8(f.readAll()).split(' ');
    return parts.size() > 1 ? parts[1].toULongLong() * 4096 / (1024 * 1024) : 0;
#endif
}

quint64 UsageSampler::systemUsedMB() {
#ifdef _WIN32
    MEMORYSTATUSEX ms{sizeof(ms)};
    if (GlobalMemoryStatusEx(&ms)) return (ms.ullTotalPhys - ms.ullAvailPhys) / (1024 * 1024);
#endif
    return 0;
}

double UsageSampler::gpuPercent() {
#ifdef _WIN32
    if (!m_pdhQuery) {
        PDH_HQUERY q = nullptr; PDH_HCOUNTER c = nullptr;
        if (PdhOpenQueryW(nullptr, 0, &q) != ERROR_SUCCESS) return -1;
        if (PdhAddEnglishCounterW(q, L"\\GPU Engine(*engtype_3D)\\Utilization Percentage", 0, &c) != ERROR_SUCCESS) {
            PdhCloseQuery(q); return -1;
        }
        PdhCollectQueryData(q);
        m_pdhQuery = q; m_pdhCounter = c;
        return 0;
    }
    if (PdhCollectQueryData(static_cast<PDH_HQUERY>(m_pdhQuery)) != ERROR_SUCCESS) return -1;
    DWORD bufSize = 0, count = 0;
    PdhGetFormattedCounterArrayW(static_cast<PDH_HCOUNTER>(m_pdhCounter), PDH_FMT_DOUBLE, &bufSize, &count, nullptr);
    std::vector<BYTE> buf(bufSize);
    auto *items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(buf.data());
    if (PdhGetFormattedCounterArrayW(static_cast<PDH_HCOUNTER>(m_pdhCounter), PDH_FMT_DOUBLE, &bufSize, &count, items) != ERROR_SUCCESS) return -1;
    double total = 0;
    for (DWORD i = 0; i < count; ++i) total += items[i].FmtValue.doubleValue;
    return qMin(100.0, total);
#else
    return -1;
#endif
}

UsageSampler::~UsageSampler() {
#ifdef _WIN32
    if (m_pdhQuery) PdhCloseQuery(static_cast<PDH_HQUERY>(m_pdhQuery));
#endif
}

} // namespace bc
