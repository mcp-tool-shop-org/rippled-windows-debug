using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using Gov.Common;

// Find the real link.exe
var realLink = FindRealLink();
if (realLink == null)
{
    Console.Error.WriteLine("[gov-link] Cannot find real link.exe. Set GOV_REAL_LINK or ensure link.exe is in PATH.");
    return 1;
}

// Calculate token cost based on args
var tokenCost = EstimateTokenCost(args);
var argsHash = ComputeArgsHash(args);
var hasLTCG = HasLTCG(args);

// Try to connect to governor (fail-safe: if unavailable, run ungoverned)
using var client = new GovernorClient();
var connected = client.TryConnect();
string? leaseId = null;

if (connected)
{
    var result = await client.TryAcquireAsync("link", tokenCost, argsHash, isLTCG: hasLTCG);
    leaseId = result?.LeaseId;
}

// Run the real linker
var startInfo = new ProcessStartInfo
{
    FileName = realLink,
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
        // Check for linker diagnostics
        if (e.Data.Contains("error") || e.Data.Contains("warning") ||
            e.Data.Contains("LNK") || e.Data.Contains(": fatal"))
            stderrHadDiagnostics = true;
    }
};

process.Start();
process.BeginOutputReadLine();
process.BeginErrorReadLine();

// Monitor process memory
var execResult = await ProcessMetrics.MonitorProcessAsync(process);

// Release tokens
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

static string? FindRealLink()
{
    var envLink = Environment.GetEnvironmentVariable("GOV_REAL_LINK");
    if (!string.IsNullOrEmpty(envLink) && File.Exists(envLink))
        return envLink;

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

            var candidate = Path.Combine(dir, "link.exe");
            if (File.Exists(candidate))
                return candidate;
        }
        catch { }
    }

    return null;
}

static int EstimateTokenCost(string[] args)
{
    // Linking is heavier than compiling
    var cost = 4;

    foreach (var arg in args)
    {
        var lower = arg.ToLowerInvariant();

        // LTCG massively increases memory
        if (lower.Contains("/ltcg") || lower.Contains("-ltcg"))
            cost += 4;

        // Whole program optimization
        if (lower.Contains("/gl") || lower.Contains("-gl"))
            cost += 2;

        // Debug info
        if (lower.Contains("/debug") || lower.Contains("-debug"))
            cost += 1;

        // Profile-guided optimization
        if (lower.Contains("/ltcg:pgi") || lower.Contains("/ltcg:pgo"))
            cost += 2;

        // Incremental linking (actually lighter)
        if (lower.Contains("/incremental") || lower.Contains("-incremental"))
            cost = Math.Max(2, cost - 2);
    }

    return Math.Clamp(cost, 2, 12);
}

static bool HasLTCG(string[] args)
{
    foreach (var arg in args)
    {
        var lower = arg.ToLowerInvariant();
        if (lower.Contains("/ltcg") || lower.Contains("-ltcg") ||
            lower.Contains("/gl") || lower.Contains("-gl"))
            return true;
    }
    return false;
}

static string ComputeArgsHash(string[] args)
{
    var combined = string.Join("|", args);
    var bytes = SHA256.HashData(Encoding.UTF8.GetBytes(combined));
    return Convert.ToHexString(bytes)[..16];
}
