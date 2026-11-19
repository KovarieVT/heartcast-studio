#pragma once
#include <QString>
#include <QStringList>

namespace bc {

struct GpuInfo { QString name; QString vendor; quint64 vramMB = 0; };

struct HardwareInfo {
    QString cpuName;
    int     logicalCores = 0;
    quint64 ramMB = 0;
    QList<GpuInfo> gpus;

    bool hasNvidia() const;
    bool hasAmd() const;
    bool hasIntel() const;

    // "low" | "balanced" | "high" | "pro"
    QString recommendedProfile() const;

    static HardwareInfo detect();
};

// Applies a performance profile to Settings (resolution, fps, bitrate, encoder preset).
void applyPerformanceProfile(const QString &profile, const QString &preferredEncoder);

// Live system usage sampler (CPU %, RAM MB of process & system).
class UsageSampler {
public:
    double cpuPercent();         // whole-system CPU usage since last call
    quint64 processMemoryMB();
    quint64 systemUsedMB();
    double gpuPercent();         // 3D engine utilisation (Windows PDH); -1 if unavailable
    ~UsageSampler();
private:
    void *m_pdhQuery = nullptr, *m_pdhCounter = nullptr;
    quint64 m_lastIdle = 0, m_lastTotal = 0;
};

} // namespace bc
