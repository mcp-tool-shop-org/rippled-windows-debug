using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using Gov.Common;

// Find the real CL.exe
var realCl = FindRealCl();
if (realCl == null)
{
    Console.Error.WriteLine("[gov-cl] Cannot find real cl.exe. Set GOV_REAL_CL or ensure cl.exe is in PATH.");
    return 1;
}

// Calculate token cost based on args
var tokenCost = EstimateTokenCost(args);
var argsHash = ComputeArgsHash(args);
var sourceFile = GetSourceFile(args);

// Try to connect to governor (fail-safe: if unavailable, run ungoverned)
using var client = new GovernorClient();
var connected = client.TryConnect();
string? leaseId = null;

if (connected)
{
    var result = await client.TryAcquireAsync("cl", tokenCost, argsHash, sourceFile);
    leaseId = result?.LeaseId;
    // If denied but connected, we still proceed (just won't have a lease)
}

// Run the real compiler
var startInfo = new ProcessStartInfo
{
    FileName = realCl,
    UseShellExecute = false,
    RedirectStandardOutput = true,
    RedirectStandardError = true
};

foreach (var arg in args)
{
    startInfo.ArgumentList.Add(arg);
}

var process = new Process { StartInfo = startInfo };
var stderrBuilder = new StringBuilder();
var stderrHadDiagnostics = false;

process.OutputDataReceived += (_, e) =>
{
    if (e.Data != null) Console.WriteLine(e.Data);
};

process.ErrorDataReceived += (_, e) =>
{
    if (e.Data != null)
    {
        Console.Error.WriteLine(e.Data);
        stderrBuilder.AppendLine(e.Data);
        // Check for compiler diagnostics
        if (e.Data.Contains("error") || e.Data.Contains("warning") ||
            e.Data.Contains(": fatal") || e.Data.Contains("C1") || e.Data.Contains("C2"))
            stderrHadDiagnostics = true;
    }
};

process.Start();
process.BeginOutputReadLine();
process.BeginErrorReadLine();

// Monitor process memory while running
var execResult = await ProcessMetrics.MonitorProcessAsync(process);

// Release tokens and get classification (if we had a lease)
if (connected && leaseId != null)
{
    var releaseResult = await client.TryReleaseAsync(
        leaseId,
        execResult.PeakWorkingSetBytes,
        execResult.PeakCommitBytes,
        execResult.ExitCode,
        execResult.DurationMs,
        stderrHadDiagnostics,
        stderrBuilder.Length > 500 ? stderrBuilder.ToString()[..500] : stderrBuilder.ToString());

    // If it looks like OOM, print the diagnostic message
    if (releaseResult != null && execResult.ExitCode != 0)
    {
        if (releaseResult.Classification == Gov.Protocol.FailureClassification.LikelyOOM ||
            releaseResult.Classification == Gov.Protocol.FailureClassification.LikelyPagingDeath)
        {
            if (!string.IsNullOrEmpty(releaseResult.Message))
            {
                Console.Error.WriteLine();
                Console.Error.WriteLine(releaseResult.Message);
            }
        }
    }
}

return execResult.ExitCode;

// --- Helper functions ---

static string? FindRealCl()
{
    // Check environment variable first
    var envCl = Environment.GetEnvironmentVariable("GOV_REAL_CL");
    if (!string.IsNullOrEmpty(envCl) && File.Exists(envCl))
        return envCl;

    // Search PATH, skipping our own directory
    var myDir = Path.GetDirectoryName(Environment.ProcessPath) ?? "";
    var pathDirs = Environment.GetEnvironmentVariable("PATH")?.Split(Path.PathSeparator) ?? [];

    foreach (var dir in pathDirs)
    {
        if (string.IsNullOrWhiteSpace(dir)) continue;

        try
        {
            var dirNorm = Path.GetFullPath(dir).TrimEnd(Path.DirectorySeparatorChar);
            var myDirNorm = Path.GetFullPath(myDir).TrimEnd(Path.DirectorySeparatorChar);

            if (string.Equals(dirNorm, myDirNorm, StringComparison.OrdinalIgnoreCase))
                continue;

            var candidate = Path.Combine(dir, "cl.exe");
            if (File.Exists(candidate))
                return candidate;
        }
        catch { }
    }

    return null;
}

static int EstimateTokenCost(string[] args)
{
    var cost = 1;

    foreach (var arg in args)
    {
        var lower = arg.ToLowerInvariant();

        // Whole program optimization / LTCG
        if (lower.Contains("/gl") || lower.Contains("-gl"))
            cost += 3;

        // Debug info
        if (lower.Contains("/zi") || lower.Contains("/zI") || lower.Contains("-zi"))
            cost += 1;

        // Optimization levels
        if (lower.Contains("/o2") || lower.Contains("/ox") || lower.Contains("-o2"))
            cost += 1;

        // Multi-processor compilation (actually reduces cost per invocation)
        if (lower.StartsWith("/mp") || lower.StartsWith("-mp"))
            cost = Math.Max(1, cost - 1);
    }

    // Check source file for heavy headers
    var sourceFile = GetSourceFile(args);
    if (sourceFile != null)
    {
        var sourceLower = sourceFile.ToLowerInvariant();
        if (sourceLower.Contains("boost") || sourceLower.Contains("grpc") ||
            sourceLower.Contains("protobuf") || sourceLower.Contains("abseil"))
            cost += 2;
    }

    return Math.Clamp(cost, 1, 8);
}

static string ComputeArgsHash(string[] args)
{
    var combined = string.Join("|", args);
    var bytes = SHA256.HashData(Encoding.UTF8.GetBytes(combined));
    return Convert.ToHexString(bytes)[..16];
}

static string? GetSourceFile(string[] args)
{
    foreach (var arg in args)
    {
        if (arg.StartsWith("/") || arg.StartsWith("-"))
            continue;

        if (arg.EndsWith(".cpp", StringComparison.OrdinalIgnoreCase) ||
            arg.EndsWith(".cc", StringComparison.OrdinalIgnoreCase) ||
            arg.EndsWith(".cxx", StringComparison.OrdinalIgnoreCase) ||
            arg.EndsWith(".c", StringComparison.OrdinalIgnoreCase))
        {
            return arg;
        }
    }
    return null;
}
