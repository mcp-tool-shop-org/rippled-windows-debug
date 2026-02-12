using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Runtime.Versioning;

namespace Gov.Common;

/// <summary>
/// Process-level memory metrics.
/// </summary>
[SupportedOSPlatform("windows")]
public static class ProcessMetrics
{
    [StructLayout(LayoutKind.Sequential)]
    private struct PROCESS_MEMORY_COUNTERS_EX
    {
        public uint cb;
        public uint PageFaultCount;
        public nuint PeakWorkingSetSize;
        public nuint WorkingSetSize;
        public nuint QuotaPeakPagedPoolUsage;
        public nuint QuotaPagedPoolUsage;
        public nuint QuotaPeakNonPagedPoolUsage;
        public nuint QuotaNonPagedPoolUsage;
        public nuint PagefileUsage;
        public nuint PeakPagefileUsage;
        public nuint PrivateUsage;
    }

    [DllImport("psapi.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetProcessMemoryInfo(
        IntPtr hProcess,
        out PROCESS_MEMORY_COUNTERS_EX ppsmemCounters,
        uint cb);

    /// <summary>
    /// Get memory info for a process by handle.
    /// </summary>
    public static ProcessMemoryInfo? GetProcessMemoryInfo(IntPtr processHandle)
    {
        var counters = new PROCESS_MEMORY_COUNTERS_EX
        {
            cb = (uint)Marshal.SizeOf<PROCESS_MEMORY_COUNTERS_EX>()
        };

        if (!GetProcessMemoryInfo(processHandle, out counters, counters.cb))
        {
            return null;
        }

        return new ProcessMemoryInfo
        {
            WorkingSetBytes = (long)counters.WorkingSetSize,
            PeakWorkingSetBytes = (long)counters.PeakWorkingSetSize,
            PagefileUsageBytes = (long)counters.PagefileUsage,
            PeakPagefileUsageBytes = (long)counters.PeakPagefileUsage,
            PrivateUsageBytes = (long)counters.PrivateUsage,
            PageFaultCount = counters.PageFaultCount
        };
    }

    /// <summary>
    /// Get memory info for a Process object.
    /// </summary>
    public static ProcessMemoryInfo? GetProcessMemoryInfo(Process process)
    {
        try
        {
            return GetProcessMemoryInfo(process.Handle);
        }
        catch
        {
            return null;
        }
    }

    /// <summary>
    /// Monitor a process and return peak memory stats after it exits.
    /// </summary>
    public static async Task<ProcessExecutionResult> MonitorProcessAsync(
        Process process,
        CancellationToken ct = default)
    {
        var startTime = DateTime.UtcNow;
        var peakWorkingSet = 0L;
        var peakCommit = 0L;
        var samples = 0;

        // Sample memory periodically while process runs
        using var timer = new PeriodicTimer(TimeSpan.FromMilliseconds(100));

        var monitorTask = Task.Run(async () =>
        {
            while (!process.HasExited && !ct.IsCancellationRequested)
            {
                try
                {
                    var info = GetProcessMemoryInfo(process);
                    if (info != null)
                    {
                        peakWorkingSet = Math.Max(peakWorkingSet, info.PeakWorkingSetBytes);
                        peakCommit = Math.Max(peakCommit, info.PrivateUsageBytes);
                        samples++;
                    }
                }
                catch { /* Process may have exited */ }

                await timer.WaitForNextTickAsync(ct);
            }
        }, ct);

        await process.WaitForExitAsync(ct);
        await monitorTask;

        // Final sample
        try
        {
            var finalInfo = GetProcessMemoryInfo(process);
            if (finalInfo != null)
            {
                peakWorkingSet = Math.Max(peakWorkingSet, finalInfo.PeakWorkingSetBytes);
                peakCommit = Math.Max(peakCommit, finalInfo.PrivateUsageBytes);
            }
        }
        catch { }

        return new ProcessExecutionResult
        {
            ExitCode = process.ExitCode,
            DurationMs = (int)(DateTime.UtcNow - startTime).TotalMilliseconds,
            PeakWorkingSetBytes = peakWorkingSet,
            PeakCommitBytes = peakCommit,
            MemorySamples = samples
        };
    }
}

public sealed record ProcessMemoryInfo
{
    public required long WorkingSetBytes { get; init; }
    public required long PeakWorkingSetBytes { get; init; }
    public required long PagefileUsageBytes { get; init; }
    public required long PeakPagefileUsageBytes { get; init; }
    public required long PrivateUsageBytes { get; init; }
    public required uint PageFaultCount { get; init; }

    public double WorkingSetMb => WorkingSetBytes / (1024.0 * 1024);
    public double PeakWorkingSetMb => PeakWorkingSetBytes / (1024.0 * 1024);
    public double PrivateUsageMb => PrivateUsageBytes / (1024.0 * 1024);
}

public sealed record ProcessExecutionResult
{
    public required int ExitCode { get; init; }
    public required int DurationMs { get; init; }
    public required long PeakWorkingSetBytes { get; init; }
    public required long PeakCommitBytes { get; init; }
    public required int MemorySamples { get; init; }

    public double PeakWorkingSetGb => PeakWorkingSetBytes / (1024.0 * 1024 * 1024);
    public double PeakCommitGb => PeakCommitBytes / (1024.0 * 1024 * 1024);
}
