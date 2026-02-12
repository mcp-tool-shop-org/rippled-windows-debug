using System.Diagnostics;
using System.Text.RegularExpressions;

namespace Gov.Common;

/// <summary>
/// GPU memory metrics via nvidia-smi (NVIDIA) or other tools.
/// </summary>
public static class GpuMetrics
{
    /// <summary>
    /// Get GPU memory status for all available GPUs.
    /// </summary>
    public static List<GpuStatus> GetGpuStatus()
    {
        var gpus = new List<GpuStatus>();

        // Try NVIDIA first
        var nvidiaGpus = GetNvidiaGpuStatus();
        if (nvidiaGpus.Count > 0)
        {
            gpus.AddRange(nvidiaGpus);
        }

        return gpus;
    }

    /// <summary>
    /// Get NVIDIA GPU status via nvidia-smi.
    /// </summary>
    private static List<GpuStatus> GetNvidiaGpuStatus()
    {
        var gpus = new List<GpuStatus>();

        try
        {
            var psi = new ProcessStartInfo
            {
                FileName = "nvidia-smi",
                Arguments = "--query-gpu=index,name,memory.total,memory.used,memory.free,utilization.gpu,temperature.gpu --format=csv,noheader,nounits",
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true
            };

            using var process = Process.Start(psi);
            if (process == null) return gpus;

            var output = process.StandardOutput.ReadToEnd();
            process.WaitForExit(5000);

            if (process.ExitCode != 0) return gpus;

            foreach (var line in output.Split('\n', StringSplitOptions.RemoveEmptyEntries))
            {
                var parts = line.Split(',').Select(p => p.Trim()).ToArray();
                if (parts.Length < 7) continue;

                if (int.TryParse(parts[0], out var index) &&
                    long.TryParse(parts[2], out var totalMb) &&
                    long.TryParse(parts[3], out var usedMb) &&
                    long.TryParse(parts[4], out var freeMb) &&
                    int.TryParse(parts[5], out var utilization) &&
                    int.TryParse(parts[6], out var temp))
                {
                    gpus.Add(new GpuStatus
                    {
                        Index = index,
                        Name = parts[1],
                        Vendor = GpuVendor.Nvidia,
                        TotalMemoryMb = totalMb,
                        UsedMemoryMb = usedMb,
                        FreeMemoryMb = freeMb,
                        UtilizationPercent = utilization,
                        TemperatureCelsius = temp
                    });
                }
            }
        }
        catch
        {
            // nvidia-smi not available or failed
        }

        return gpus;
    }

    /// <summary>
    /// Get aggregate GPU memory status across all GPUs.
    /// </summary>
    public static GpuAggregateStatus GetAggregateStatus()
    {
        var gpus = GetGpuStatus();

        if (gpus.Count == 0)
        {
            return new GpuAggregateStatus
            {
                Available = false,
                GpuCount = 0,
                TotalMemoryMb = 0,
                UsedMemoryMb = 0,
                FreeMemoryMb = 0,
                MemoryUsageRatio = 0,
                MaxUtilizationPercent = 0,
                MaxTemperatureCelsius = 0,
                Gpus = []
            };
        }

        var totalMem = gpus.Sum(g => g.TotalMemoryMb);
        var usedMem = gpus.Sum(g => g.UsedMemoryMb);
        var freeMem = gpus.Sum(g => g.FreeMemoryMb);

        return new GpuAggregateStatus
        {
            Available = true,
            GpuCount = gpus.Count,
            TotalMemoryMb = totalMem,
            UsedMemoryMb = usedMem,
            FreeMemoryMb = freeMem,
            MemoryUsageRatio = totalMem > 0 ? (double)usedMem / totalMem : 0,
            MaxUtilizationPercent = gpus.Max(g => g.UtilizationPercent),
            MaxTemperatureCelsius = gpus.Max(g => g.TemperatureCelsius),
            Gpus = gpus
        };
    }
}

public enum GpuVendor
{
    Unknown,
    Nvidia,
    Amd,
    Intel
}

public sealed record GpuStatus
{
    public required int Index { get; init; }
    public required string Name { get; init; }
    public required GpuVendor Vendor { get; init; }
    public required long TotalMemoryMb { get; init; }
    public required long UsedMemoryMb { get; init; }
    public required long FreeMemoryMb { get; init; }
    public required int UtilizationPercent { get; init; }
    public required int TemperatureCelsius { get; init; }

    public double TotalMemoryGb => TotalMemoryMb / 1024.0;
    public double UsedMemoryGb => UsedMemoryMb / 1024.0;
    public double FreeMemoryGb => FreeMemoryMb / 1024.0;
    public double MemoryUsageRatio => TotalMemoryMb > 0 ? (double)UsedMemoryMb / TotalMemoryMb : 0;
}

public sealed record GpuAggregateStatus
{
    public required bool Available { get; init; }
    public required int GpuCount { get; init; }
    public required long TotalMemoryMb { get; init; }
    public required long UsedMemoryMb { get; init; }
    public required long FreeMemoryMb { get; init; }
    public required double MemoryUsageRatio { get; init; }
    public required int MaxUtilizationPercent { get; init; }
    public required int MaxTemperatureCelsius { get; init; }
    public required List<GpuStatus> Gpus { get; init; }

    public double TotalMemoryGb => TotalMemoryMb / 1024.0;
    public double UsedMemoryGb => UsedMemoryMb / 1024.0;
    public double FreeMemoryGb => FreeMemoryMb / 1024.0;
}
