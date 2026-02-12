using Gov.Common;
using Gov.Service;

// Parse command line arguments
var isBackground = args.Contains("--background");
var isService = args.Contains("--service");
var isQuiet = isBackground || isService;

// Check for singleton - only one governor should run
const string mutexName = "Global\\BuildGovernorInstance";
using var instanceMutex = new Mutex(true, mutexName, out bool createdNew);

if (!createdNew)
{
    if (!isQuiet)
    {
        Console.WriteLine("Another governor instance is already running.");
    }
    return 0; // Not an error - another instance is handling it
}

if (!isQuiet)
{
    Console.WriteLine("╔══════════════════════════════════════════════════════════════════╗");
    Console.WriteLine("║              Build Reliability Governor v0.2.0                   ║");
    Console.WriteLine("╚══════════════════════════════════════════════════════════════════╝");
    Console.WriteLine();
}

// Show initial system status
var memStatus = WindowsMemoryMetrics.GetMemoryStatus();
if (!isQuiet)
{
    Console.WriteLine($"System Memory:");
    Console.WriteLine($"  Physical: {memStatus.AvailablePhysicalGb:F1} GB available / {memStatus.TotalPhysicalGb:F1} GB total");
    Console.WriteLine($"  Commit:   {memStatus.CommitChargeGb:F1} GB used / {memStatus.CommitLimitGb:F1} GB limit ({memStatus.CommitRatio:P0})");
    Console.WriteLine();

    // Show GPU status
    var gpuStatus = GpuMetrics.GetAggregateStatus();
    if (gpuStatus.Available)
    {
        Console.WriteLine($"GPU Memory:");
        foreach (var gpu in gpuStatus.Gpus)
        {
            Console.WriteLine($"  [{gpu.Index}] {gpu.Name}");
            Console.WriteLine($"      VRAM: {gpu.FreeMemoryGb:F1} GB free / {gpu.TotalMemoryGb:F1} GB total ({gpu.MemoryUsageRatio:P0} used)");
            Console.WriteLine($"      Util: {gpu.UtilizationPercent}%  Temp: {gpu.TemperatureCelsius}°C");
        }
        Console.WriteLine();
    }
    else
    {
        Console.WriteLine("GPU: Not detected (nvidia-smi not available)");
        Console.WriteLine();
    }
}

// Create token pool with default config
var config = new TokenBudgetConfig
{
    GbPerToken = 2.0,
    SafetyReserveGb = 8.0,
    MinTokens = 1,
    MaxTokens = 32,
    CautionRatio = 0.80,
    SoftStopRatio = 0.88,
    HardStopRatio = 0.92
};

using var tokenPool = new TokenPool(config);
var status = tokenPool.GetStatus();

if (!isQuiet)
{
    Console.WriteLine($"Token Pool:");
    Console.WriteLine($"  Total tokens:    {status.TotalTokens}");
    Console.WriteLine($"  Throttle level:  {status.ThrottleLevel}");
    Console.WriteLine($"  Recommended -j:  {status.RecommendedParallelism}");
    Console.WriteLine();
}

// Handle Ctrl+C (console mode)
using var cts = new CancellationTokenSource();
Console.CancelKeyPress += (_, e) =>
{
    e.Cancel = true;
    if (!isQuiet) Console.WriteLine("\nShutting down...");
    cts.Cancel();
};

// In background/service mode, also set up idle timeout
Task? idleTimeoutTask = null;
if (isBackground)
{
    // Auto-shutdown after 30 minutes of inactivity (no active leases)
    idleTimeoutTask = MonitorIdleTimeout(tokenPool, cts, TimeSpan.FromMinutes(30));
}

// Run pipe server
await using var server = new PipeServer(tokenPool);
await server.RunAsync(cts.Token);

if (!isQuiet) Console.WriteLine("Governor stopped.");

return 0;

// --- Background mode idle timeout ---
static async Task MonitorIdleTimeout(TokenPool pool, CancellationTokenSource cts, TimeSpan timeout)
{
    var lastActivity = DateTime.UtcNow;

    while (!cts.Token.IsCancellationRequested)
    {
        await Task.Delay(TimeSpan.FromMinutes(1), cts.Token).ConfigureAwait(ConfigureAwaitOptions.SuppressThrowing);

        var status = pool.GetStatus();
        if (status.ActiveLeases > 0)
        {
            lastActivity = DateTime.UtcNow;
        }
        else if (DateTime.UtcNow - lastActivity > timeout)
        {
            // No activity for timeout period - shutdown
            if (Environment.GetEnvironmentVariable("GOV_DEBUG") == "1")
            {
                Console.Error.WriteLine("[gov] Idle timeout - shutting down background governor");
            }
            cts.Cancel();
            break;
        }
    }
}
