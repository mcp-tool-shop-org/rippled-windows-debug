using System.IO.Pipes;
using System.Text.Json;
using Gov.Protocol;

namespace Gov.Common;

/// <summary>
/// Client for communicating with the Build Governor service.
/// Implements fail-safe behavior: if governor is unavailable, tools run ungoverned.
/// </summary>
public sealed class GovernorClient : IDisposable
{
    private const string PipeName = "BuildGovernor";
    private const int ConnectTimeoutMs = 2000;  // 2 seconds to connect
    private const int AcquireTimeoutMs = 60000; // 60 seconds max wait for tokens

    private NamedPipeClientStream? _pipe;
    private StreamReader? _reader;
    private StreamWriter? _writer;
    private bool _connected;

    public bool IsConnected => _connected;

    /// <summary>
    /// Try to connect to the governor. If not running, attempts to auto-start.
    /// Returns false if unavailable after all attempts (fail-safe).
    /// </summary>
    /// <param name="autoStart">If true, attempt to start governor if not running (default: true)</param>
    public bool TryConnect(bool autoStart = true)
    {
        try
        {
            _pipe = new NamedPipeClientStream(".", PipeName, PipeDirection.InOut);
            _pipe.Connect(ConnectTimeoutMs);
            _reader = new StreamReader(_pipe);
            _writer = new StreamWriter(_pipe) { AutoFlush = true };
            _connected = true;
            return true;
        }
        catch (TimeoutException)
        {
            // Governor not running - try to auto-start if enabled
            if (autoStart && GovernorAutoStart.EnsureRunning())
            {
                // Retry connection after auto-start
                try
                {
                    _pipe?.Dispose();
                    _pipe = new NamedPipeClientStream(".", PipeName, PipeDirection.InOut);
                    _pipe.Connect(ConnectTimeoutMs);
                    _reader = new StreamReader(_pipe);
                    _writer = new StreamWriter(_pipe) { AutoFlush = true };
                    _connected = true;
                    return true;
                }
                catch
                {
                    PrintWarning("Governor auto-started but connection failed — running ungoverned.");
                    return false;
                }
            }

            PrintWarning("Governor not running — running ungoverned.");
            return false;
        }
        catch (Exception ex)
        {
            PrintWarning($"Governor unavailable ({ex.Message}) — running ungoverned.");
            return false;
        }
    }

    /// <summary>
    /// Request tokens. Returns null if failed (fail-safe: caller should proceed).
    /// </summary>
    public async Task<AcquireResult?> TryAcquireAsync(
        string tool,
        int requestedTokens,
        string argsHash,
        string? sourceFile = null,
        bool isLTCG = false)
    {
        if (!_connected || _writer == null || _reader == null)
            return null;

        try
        {
            var request = new
            {
                type = "acquire",
                data = new AcquireTokensRequest
                {
                    Tool = tool,
                    ArgsHash = argsHash,
                    RequestedTokens = requestedTokens,
                    TimeoutMs = AcquireTimeoutMs,
                    SourceFile = sourceFile,
                    IsLTCG = isLTCG
                }
            };

            await _writer.WriteLineAsync(JsonSerializer.Serialize(request));

            // Read with timeout
            using var cts = new CancellationTokenSource(AcquireTimeoutMs + 5000);
            var response = await _reader.ReadLineAsync(cts.Token);

            if (response == null)
            {
                PrintWarning("Governor connection lost — running ungoverned.");
                _connected = false;
                return null;
            }

            using var doc = JsonDocument.Parse(response);
            var data = doc.RootElement.GetProperty("data");

            var granted = data.GetProperty("granted").GetBoolean();
            if (!granted)
            {
                var reason = data.TryGetProperty("reason", out var reasonProp)
                    ? reasonProp.GetString()
                    : "unknown";
                PrintWarning($"Token denied: {reason}");
                // Still return result so caller can decide
            }

            return new AcquireResult
            {
                Success = granted,
                LeaseId = data.TryGetProperty("leaseId", out var lid) ? lid.GetString() : null,
                GrantedTokens = data.TryGetProperty("grantedTokens", out var gt) ? gt.GetInt32() : 0,
                RecommendedParallelism = data.TryGetProperty("recommendedParallelism", out var rp) ? rp.GetInt32() : 4,
                CommitRatio = data.TryGetProperty("commitRatio", out var cr) ? cr.GetDouble() : 0,
                Reason = data.TryGetProperty("reason", out var r) ? r.GetString() : null
            };
        }
        catch (OperationCanceledException)
        {
            PrintWarning("Governor timeout — running ungoverned.");
            _connected = false;
            return null;
        }
        catch (Exception ex)
        {
            PrintWarning($"Governor error ({ex.Message}) — running ungoverned.");
            _connected = false;
            return null;
        }
    }

    /// <summary>
    /// Release tokens and get failure classification.
    /// </summary>
    public async Task<ReleaseResult?> TryReleaseAsync(
        string leaseId,
        long peakWorkingSetBytes,
        long peakCommitBytes,
        int exitCode,
        int durationMs,
        bool stderrHadDiagnostics,
        string? stderrDigest = null)
    {
        if (!_connected || _writer == null || _reader == null)
            return null;

        try
        {
            var request = new
            {
                type = "release",
                data = new ReleaseTokensRequest
                {
                    LeaseId = leaseId,
                    PeakWorkingSetBytes = peakWorkingSetBytes,
                    PeakCommitBytes = peakCommitBytes,
                    ExitCode = exitCode,
                    DurationMs = durationMs,
                    StderrHadDiagnostics = stderrHadDiagnostics,
                    StderrDigest = stderrDigest
                }
            };

            await _writer.WriteLineAsync(JsonSerializer.Serialize(request));

            using var cts = new CancellationTokenSource(5000);
            var response = await _reader.ReadLineAsync(cts.Token);

            if (response == null)
                return null;

            using var doc = JsonDocument.Parse(response);
            var data = doc.RootElement.GetProperty("data");

            return new ReleaseResult
            {
                Classification = Enum.TryParse<FailureClassification>(
                    data.GetProperty("classification").GetString(), out var c) ? c : FailureClassification.Unknown,
                Message = data.TryGetProperty("message", out var m) ? m.GetString() : null,
                ShouldRetry = data.TryGetProperty("shouldRetry", out var sr) && sr.GetBoolean(),
                RetryWithTokens = data.TryGetProperty("retryWithTokens", out var rt) ? rt.GetInt32() : null
            };
        }
        catch
        {
            return null;
        }
    }

    private static void PrintWarning(string message)
    {
        var oldColor = Console.ForegroundColor;
        Console.ForegroundColor = ConsoleColor.Yellow;
        Console.Error.WriteLine($"[gov] {message}");
        Console.ForegroundColor = oldColor;
    }

    public void Dispose()
    {
        _writer?.Dispose();
        _reader?.Dispose();
        _pipe?.Dispose();
    }
}

public sealed record AcquireResult
{
    public required bool Success { get; init; }
    public string? LeaseId { get; init; }
    public int GrantedTokens { get; init; }
    public required int RecommendedParallelism { get; init; }
    public double CommitRatio { get; init; }
    public string? Reason { get; init; }
}

public sealed record ReleaseResult
{
    public required FailureClassification Classification { get; init; }
    public string? Message { get; init; }
    public bool ShouldRetry { get; init; }
    public int? RetryWithTokens { get; init; }
}
