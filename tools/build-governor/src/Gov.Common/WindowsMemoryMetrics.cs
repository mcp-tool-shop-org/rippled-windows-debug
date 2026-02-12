using System.Runtime.InteropServices;
using System.Runtime.Versioning;

namespace Gov.Common;

/// <summary>
/// Windows memory metrics via GlobalMemoryStatusEx and performance counters.
/// </summary>
[SupportedOSPlatform("windows")]
public static class WindowsMemoryMetrics
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Auto)]
    private struct MEMORYSTATUSEX
    {
        public uint dwLength;
        public uint dwMemoryLoad;
        public ulong ullTotalPhys;
        public ulong ullAvailPhys;
        public ulong ullTotalPageFile;
        public ulong ullAvailPageFile;
        public ulong ullTotalVirtual;
        public ulong ullAvailVirtual;
        public ulong ullAvailExtendedVirtual;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GlobalMemoryStatusEx(ref MEMORYSTATUSEX lpBuffer);

    /// <summary>
    /// Get current system memory status.
    /// </summary>
    public static MemoryStatus GetMemoryStatus()
    {
        var memStatus = new MEMORYSTATUSEX { dwLength = (uint)Marshal.SizeOf<MEMORYSTATUSEX>() };

        if (!GlobalMemoryStatusEx(ref memStatus))
        {
            throw new InvalidOperationException($"GlobalMemoryStatusEx failed: {Marshal.GetLastWin32Error()}");
        }

        // Commit charge = TotalPageFile - AvailPageFile
        // Commit limit = TotalPageFile
        var commitCharge = memStatus.ullTotalPageFile - memStatus.ullAvailPageFile;
        var commitLimit = memStatus.ullTotalPageFile;
        var commitRatio = commitLimit > 0 ? (double)commitCharge / commitLimit : 0;

        return new MemoryStatus
        {
            TotalPhysicalBytes = (long)memStatus.ullTotalPhys,
            AvailablePhysicalBytes = (long)memStatus.ullAvailPhys,
            CommitChargeBytes = (long)commitCharge,
            CommitLimitBytes = (long)commitLimit,
            CommitRatio = commitRatio,
            MemoryLoadPercent = (int)memStatus.dwMemoryLoad
        };
    }

    /// <summary>
    /// Calculate recommended token budget based on system memory.
    /// </summary>
    public static TokenBudget CalculateTokenBudget(MemoryStatus status, TokenBudgetConfig config)
    {
        // Available commit headroom in GB
        var availableCommitGb = (status.CommitLimitBytes - status.CommitChargeBytes) / (1024.0 * 1024 * 1024);

        // Reserve safety buffer
        var usableCommitGb = Math.Max(0, availableCommitGb - config.SafetyReserveGb);

        // Calculate tokens
        var totalTokens = (int)Math.Floor(usableCommitGb / config.GbPerToken);
        totalTokens = Math.Clamp(totalTokens, config.MinTokens, config.MaxTokens);

        // Determine throttle level
        ThrottleLevel throttle;
        if (status.CommitRatio >= config.HardStopRatio)
            throttle = ThrottleLevel.HardStop;
        else if (status.CommitRatio >= config.SoftStopRatio)
            throttle = ThrottleLevel.SoftStop;
        else if (status.CommitRatio >= config.CautionRatio)
            throttle = ThrottleLevel.Caution;
        else
            throttle = ThrottleLevel.Normal;

        // Recommended parallelism (rough: 1 job per 2-4 GB available)
        var recommendedParallelism = Math.Max(1, (int)Math.Floor(usableCommitGb / 3.0));

        return new TokenBudget
        {
            TotalTokens = totalTokens,
            ThrottleLevel = throttle,
            RecommendedParallelism = recommendedParallelism,
            AvailableCommitGb = availableCommitGb
        };
    }
}

public sealed record MemoryStatus
{
    public required long TotalPhysicalBytes { get; init; }
    public required long AvailablePhysicalBytes { get; init; }
    public required long CommitChargeBytes { get; init; }
    public required long CommitLimitBytes { get; init; }
    public required double CommitRatio { get; init; }
    public required int MemoryLoadPercent { get; init; }

    public double TotalPhysicalGb => TotalPhysicalBytes / (1024.0 * 1024 * 1024);
    public double AvailablePhysicalGb => AvailablePhysicalBytes / (1024.0 * 1024 * 1024);
    public double CommitChargeGb => CommitChargeBytes / (1024.0 * 1024 * 1024);
    public double CommitLimitGb => CommitLimitBytes / (1024.0 * 1024 * 1024);
}

public sealed record TokenBudget
{
    public required int TotalTokens { get; init; }
    public required ThrottleLevel ThrottleLevel { get; init; }
    public required int RecommendedParallelism { get; init; }
    public required double AvailableCommitGb { get; init; }
}

public sealed record TokenBudgetConfig
{
    /// <summary>GB of commit per token. Default 2 GB.</summary>
    public double GbPerToken { get; init; } = 2.0;

    /// <summary>GB to always keep free. Default 8 GB.</summary>
    public double SafetyReserveGb { get; init; } = 8.0;

    /// <summary>Minimum tokens even under pressure. Default 1.</summary>
    public int MinTokens { get; init; } = 1;

    /// <summary>Maximum tokens to prevent runaway. Default 32.</summary>
    public int MaxTokens { get; init; } = 32;

    /// <summary>Commit ratio for caution (slow new leases). Default 0.80.</summary>
    public double CautionRatio { get; init; } = 0.80;

    /// <summary>Commit ratio for soft stop (delay new leases). Default 0.88.</summary>
    public double SoftStopRatio { get; init; } = 0.88;

    /// <summary>Commit ratio for hard stop (refuse new leases). Default 0.92.</summary>
    public double HardStopRatio { get; init; } = 0.92;
}

public enum ThrottleLevel
{
    Normal,
    Caution,
    SoftStop,
    HardStop
}
