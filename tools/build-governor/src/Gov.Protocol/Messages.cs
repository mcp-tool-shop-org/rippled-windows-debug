namespace Gov.Protocol;

/// <summary>
/// Request to acquire tokens before running a build tool.
/// </summary>
public sealed record AcquireTokensRequest
{
    public required string Tool { get; init; }           // "cl", "link", etc.
    public required string ArgsHash { get; init; }       // Hash of command line for dedup
    public required int RequestedTokens { get; init; }   // Token cost estimate
    public required int TimeoutMs { get; init; }         // Max wait time
    public string? WorkingDirectory { get; init; }
    public string? SourceFile { get; init; }             // For compile: the .cpp file
    public bool IsLTCG { get; init; }                    // Link-time code gen detected
}

/// <summary>
/// Response granting (or denying) tokens.
/// </summary>
public sealed record AcquireTokensResponse
{
    public required bool Granted { get; init; }
    public required string LeaseId { get; init; }
    public required int GrantedTokens { get; init; }
    public required int RecommendedParallelism { get; init; }
    public string? Reason { get; init; }                 // Why denied or throttled
    public double CommitRatio { get; init; }             // Current system commit %
}

/// <summary>
/// Report after tool execution completes.
/// </summary>
public sealed record ReleaseTokensRequest
{
    public required string LeaseId { get; init; }
    public required long PeakWorkingSetBytes { get; init; }
    public required long PeakCommitBytes { get; init; }
    public required int ExitCode { get; init; }
    public required int DurationMs { get; init; }
    public bool StderrHadDiagnostics { get; init; }
    public string? StderrDigest { get; init; }           // First N chars of stderr
}

/// <summary>
/// Response with failure classification.
/// </summary>
public sealed record ReleaseTokensResponse
{
    public required bool Acknowledged { get; init; }
    public required FailureClassification Classification { get; init; }
    public string? Message { get; init; }                // Human-readable diagnosis
    public bool ShouldRetry { get; init; }
    public int? RetryWithTokens { get; init; }           // Suggested lower token count
}

/// <summary>
/// Classification of build tool exit.
/// </summary>
public enum FailureClassification
{
    Success,
    NormalCompileError,
    LikelyOOM,
    LikelyPagingDeath,
    Unknown
}

/// <summary>
/// Heartbeat for long-running operations (optional).
/// </summary>
public sealed record HeartbeatRequest
{
    public required string LeaseId { get; init; }
}

public sealed record HeartbeatResponse
{
    public required bool LeaseValid { get; init; }
}

/// <summary>
/// Query current system status.
/// </summary>
public sealed record StatusRequest { }

public sealed record StatusResponse
{
    public required int TotalTokens { get; init; }
    public required int AvailableTokens { get; init; }
    public required int ActiveLeases { get; init; }
    public required double CommitRatio { get; init; }
    public required long CommitChargeBytes { get; init; }
    public required long CommitLimitBytes { get; init; }
    public required long AvailableMemoryBytes { get; init; }
    public required int RecommendedParallelism { get; init; }
}
