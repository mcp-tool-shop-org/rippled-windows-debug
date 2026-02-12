using System.Collections.Concurrent;
using Gov.Common;

namespace Gov.Service;

/// <summary>
/// Manages the token pool for build tool concurrency control.
/// Includes lease TTL for automatic reclamation if wrappers crash.
/// </summary>
public sealed class TokenPool : IDisposable
{
    private readonly TokenBudgetConfig _config;
    private readonly ConcurrentDictionary<string, Lease> _activeLeases = new();
    private readonly SemaphoreSlim _lock = new(1, 1);
    private readonly PeriodicTimer _monitorTimer;
    private readonly CancellationTokenSource _cts = new();

    // Lease TTL: if not released within this time, tokens are reclaimed
    private static readonly TimeSpan LeaseTimeout = TimeSpan.FromMinutes(30);
    private static readonly TimeSpan LeaseWarningThreshold = TimeSpan.FromMinutes(10);

    private int _totalTokens;
    private int _availableTokens;
    private MemoryStatus _lastMemoryStatus;
    private ThrottleLevel _throttleLevel;
    private int _expiredLeaseCount;

    public TokenPool(TokenBudgetConfig? config = null)
    {
        _config = config ?? new TokenBudgetConfig();
        _lastMemoryStatus = WindowsMemoryMetrics.GetMemoryStatus();
        RecalculateBudget();

        // Start background monitoring (includes lease expiry check)
        _monitorTimer = new PeriodicTimer(TimeSpan.FromMilliseconds(500));
        _ = MonitorLoopAsync(_cts.Token);
    }

    public int TotalTokens => _totalTokens;
    public int AvailableTokens => _availableTokens;
    public int ActiveLeaseCount => _activeLeases.Count;
    public int ExpiredLeaseCount => _expiredLeaseCount;
    public MemoryStatus LastMemoryStatus => _lastMemoryStatus;
    public ThrottleLevel ThrottleLevel => _throttleLevel;

    /// <summary>
    /// Try to acquire tokens for a build operation.
    /// </summary>
    public async Task<TokenAcquireResult> TryAcquireAsync(
        string tool,
        int requestedTokens,
        int timeoutMs,
        CancellationToken ct = default)
    {
        var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);

        while (DateTime.UtcNow < deadline && !ct.IsCancellationRequested)
        {
            await _lock.WaitAsync(ct);
            try
            {
                // Refresh memory status
                _lastMemoryStatus = WindowsMemoryMetrics.GetMemoryStatus();
                RecalculateBudget();

                // Check throttle level
                if (_throttleLevel == ThrottleLevel.HardStop)
                {
                    return new TokenAcquireResult
                    {
                        Success = false,
                        Reason = $"System under memory pressure (commit {_lastMemoryStatus.CommitRatio:P0}). Hard stop active.",
                        RecommendedParallelism = CalculateRecommendedParallelism()
                    };
                }

                // Try to grant tokens
                var grantedTokens = Math.Min(requestedTokens, _availableTokens);
                if (grantedTokens > 0 || requestedTokens == 0)
                {
                    var leaseId = Guid.NewGuid().ToString("N")[..12];
                    var lease = new Lease
                    {
                        LeaseId = leaseId,
                        Tool = tool,
                        Tokens = grantedTokens,
                        AcquiredAt = DateTime.UtcNow,
                        ExpiresAt = DateTime.UtcNow + LeaseTimeout,
                        CommitRatioAtAcquire = _lastMemoryStatus.CommitRatio
                    };

                    _activeLeases[leaseId] = lease;
                    _availableTokens -= grantedTokens;

                    return new TokenAcquireResult
                    {
                        Success = true,
                        LeaseId = leaseId,
                        GrantedTokens = grantedTokens,
                        RecommendedParallelism = CalculateRecommendedParallelism(),
                        CommitRatio = _lastMemoryStatus.CommitRatio
                    };
                }
            }
            finally
            {
                _lock.Release();
            }

            // Wait before retry (with backoff based on throttle level)
            var delay = _throttleLevel switch
            {
                ThrottleLevel.SoftStop => 500,
                ThrottleLevel.Caution => 200,
                _ => 100
            };

            await Task.Delay(delay, ct);
        }

        return new TokenAcquireResult
        {
            Success = false,
            Reason = "Timeout waiting for available tokens",
            RecommendedParallelism = CalculateRecommendedParallelism()
        };
    }

    /// <summary>
    /// Release tokens back to the pool.
    /// </summary>
    public async Task<TokenReleaseResult> ReleaseAsync(
        string leaseId,
        long peakWorkingSetBytes,
        long peakCommitBytes,
        int exitCode,
        int durationMs,
        bool stderrHadDiagnostics)
    {
        await _lock.WaitAsync();
        try
        {
            if (!_activeLeases.TryRemove(leaseId, out var lease))
            {
                return new TokenReleaseResult { Acknowledged = false };
            }

            _availableTokens += lease.Tokens;

            // Get current memory status for classification
            _lastMemoryStatus = WindowsMemoryMetrics.GetMemoryStatus();

            // Classify the result
            var classificationInput = new ClassificationInput
            {
                ExitCode = exitCode,
                DurationMs = durationMs,
                CommitRatioAtExit = _lastMemoryStatus.CommitRatio,
                PeakCommitRatioDuringExecution = Math.Max(lease.CommitRatioAtAcquire, _lastMemoryStatus.CommitRatio),
                PeakProcessCommitGb = peakCommitBytes / (1024.0 * 1024 * 1024),
                StderrHadDiagnostics = stderrHadDiagnostics,
                CommitChargeGb = _lastMemoryStatus.CommitChargeGb,
                CommitLimitGb = _lastMemoryStatus.CommitLimitGb,
                RecommendedParallelism = CalculateRecommendedParallelism()
            };

            var classification = FailureClassifier.Classify(classificationInput);

            return new TokenReleaseResult
            {
                Acknowledged = true,
                Classification = classification.Classification,
                Message = classification.Message,
                ShouldRetry = classification.ShouldRetry,
                RetryWithTokens = classification.ShouldRetry ? Math.Max(1, lease.Tokens / 2) : null
            };
        }
        finally
        {
            _lock.Release();
        }
    }

    /// <summary>
    /// Get current pool status including active leases.
    /// </summary>
    public PoolStatus GetStatus()
    {
        var leases = _activeLeases.Values
            .OrderByDescending(l => l.AcquiredAt)
            .Take(10)
            .Select(l => new LeaseInfo
            {
                LeaseId = l.LeaseId,
                Tool = l.Tool,
                Tokens = l.Tokens,
                DurationSecs = (int)(DateTime.UtcNow - l.AcquiredAt).TotalSeconds,
                ExpiresInSecs = (int)(l.ExpiresAt - DateTime.UtcNow).TotalSeconds
            })
            .ToList();

        return new PoolStatus
        {
            TotalTokens = _totalTokens,
            AvailableTokens = _availableTokens,
            ActiveLeases = _activeLeases.Count,
            ExpiredLeases = _expiredLeaseCount,
            ThrottleLevel = _throttleLevel,
            MemoryStatus = _lastMemoryStatus,
            RecommendedParallelism = CalculateRecommendedParallelism(),
            TopLeases = leases
        };
    }

    private void RecalculateBudget()
    {
        var budget = WindowsMemoryMetrics.CalculateTokenBudget(_lastMemoryStatus, _config);

        var usedTokens = _totalTokens - _availableTokens;
        _totalTokens = budget.TotalTokens;
        _availableTokens = Math.Max(0, _totalTokens - usedTokens);
        _throttleLevel = budget.ThrottleLevel;
    }

    private int CalculateRecommendedParallelism()
    {
        var budget = WindowsMemoryMetrics.CalculateTokenBudget(_lastMemoryStatus, _config);
        return budget.RecommendedParallelism;
    }

    private void CheckExpiredLeases()
    {
        var now = DateTime.UtcNow;
        var expiredIds = new List<string>();
        var warningIds = new List<(string id, Lease lease)>();

        foreach (var (id, lease) in _activeLeases)
        {
            if (now >= lease.ExpiresAt)
            {
                expiredIds.Add(id);
            }
            else if (now >= lease.AcquiredAt + LeaseWarningThreshold && !lease.WarningLogged)
            {
                warningIds.Add((id, lease));
            }
        }

        // Log warnings for long-running leases
        foreach (var (id, lease) in warningIds)
        {
            var duration = (now - lease.AcquiredAt).TotalMinutes;
            Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] WARNING: Lease {id} ({lease.Tool}) running for {duration:F0} min");
            lease.WarningLogged = true;
        }

        // Reclaim expired leases
        foreach (var id in expiredIds)
        {
            if (_activeLeases.TryRemove(id, out var lease))
            {
                _availableTokens += lease.Tokens;
                _expiredLeaseCount++;
                Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] EXPIRED: Lease {id} ({lease.Tool}) reclaimed {lease.Tokens} tokens");
            }
        }
    }

    private async Task MonitorLoopAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            try
            {
                await _monitorTimer.WaitForNextTickAsync(ct);

                await _lock.WaitAsync(ct);
                try
                {
                    _lastMemoryStatus = WindowsMemoryMetrics.GetMemoryStatus();
                    RecalculateBudget();
                    CheckExpiredLeases();
                }
                finally
                {
                    _lock.Release();
                }
            }
            catch (OperationCanceledException)
            {
                break;
            }
        }
    }

    public void Dispose()
    {
        _cts.Cancel();
        _monitorTimer.Dispose();
        _lock.Dispose();
        _cts.Dispose();
    }
}

public sealed record Lease
{
    public required string LeaseId { get; init; }
    public required string Tool { get; init; }
    public required int Tokens { get; init; }
    public required DateTime AcquiredAt { get; init; }
    public required DateTime ExpiresAt { get; init; }
    public required double CommitRatioAtAcquire { get; init; }
    public bool WarningLogged { get; set; }
}

public sealed record TokenAcquireResult
{
    public required bool Success { get; init; }
    public string? LeaseId { get; init; }
    public int GrantedTokens { get; init; }
    public required int RecommendedParallelism { get; init; }
    public string? Reason { get; init; }
    public double CommitRatio { get; init; }
}

public sealed record TokenReleaseResult
{
    public required bool Acknowledged { get; init; }
    public Protocol.FailureClassification Classification { get; init; }
    public string? Message { get; init; }
    public bool ShouldRetry { get; init; }
    public int? RetryWithTokens { get; init; }
}

public sealed record PoolStatus
{
    public required int TotalTokens { get; init; }
    public required int AvailableTokens { get; init; }
    public required int ActiveLeases { get; init; }
    public required int ExpiredLeases { get; init; }
    public required ThrottleLevel ThrottleLevel { get; init; }
    public required MemoryStatus MemoryStatus { get; init; }
    public required int RecommendedParallelism { get; init; }
    public List<LeaseInfo>? TopLeases { get; init; }
}

public sealed record LeaseInfo
{
    public required string LeaseId { get; init; }
    public required string Tool { get; init; }
    public required int Tokens { get; init; }
    public required int DurationSecs { get; init; }
    public required int ExpiresInSecs { get; init; }
}
