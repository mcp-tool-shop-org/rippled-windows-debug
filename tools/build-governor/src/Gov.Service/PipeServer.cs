using System.IO.Pipes;
using System.Text.Json;
using Gov.Protocol;

namespace Gov.Service;

// Use camelCase for JSON serialization
file static class Json
{
    public static readonly JsonSerializerOptions Options = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        PropertyNameCaseInsensitive = true
    };
}

/// <summary>
/// Named pipe server for wrapper communication.
/// </summary>
public sealed class PipeServer : IAsyncDisposable
{
    private const string PipeName = "BuildGovernor";
    private readonly TokenPool _tokenPool;
    private readonly CancellationTokenSource _cts = new();
    private readonly List<Task> _connectionTasks = [];

    public PipeServer(TokenPool tokenPool)
    {
        _tokenPool = tokenPool;
    }

    public async Task RunAsync(CancellationToken ct = default)
    {
        using var linkedCts = CancellationTokenSource.CreateLinkedTokenSource(ct, _cts.Token);

        Console.WriteLine($"Build Governor listening on pipe: {PipeName}");
        Console.WriteLine($"Initial tokens: {_tokenPool.TotalTokens}");
        Console.WriteLine($"Recommended parallelism: {_tokenPool.GetStatus().RecommendedParallelism}");
        Console.WriteLine();

        while (!linkedCts.Token.IsCancellationRequested)
        {
            try
            {
                var pipe = new NamedPipeServerStream(
                    PipeName,
                    PipeDirection.InOut,
                    NamedPipeServerStream.MaxAllowedServerInstances,
                    PipeTransmissionMode.Byte,
                    PipeOptions.Asynchronous);

                await pipe.WaitForConnectionAsync(linkedCts.Token);

                // Handle connection in background
                var task = HandleConnectionAsync(pipe, linkedCts.Token);
                _connectionTasks.Add(task);

                // Cleanup completed tasks
                _connectionTasks.RemoveAll(t => t.IsCompleted);
            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error accepting connection: {ex.Message}");
            }
        }
    }

    private async Task HandleConnectionAsync(NamedPipeServerStream pipe, CancellationToken ct)
    {
        try
        {
            using var reader = new StreamReader(pipe);
            await using var writer = new StreamWriter(pipe) { AutoFlush = true };

            while (pipe.IsConnected && !ct.IsCancellationRequested)
            {
                var line = await reader.ReadLineAsync(ct);
                if (line == null) break;

                var response = await ProcessMessageAsync(line);
                await writer.WriteLineAsync(response);
            }
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            Console.WriteLine($"Connection error: {ex.Message}");
        }
        finally
        {
            await pipe.DisposeAsync();
        }
    }

    private async Task<string> ProcessMessageAsync(string json)
    {
        try
        {
            using var doc = JsonDocument.Parse(json);
            var type = doc.RootElement.GetProperty("type").GetString();

            return type switch
            {
                "acquire" => await HandleAcquireAsync(json),
                "release" => await HandleReleaseAsync(json),
                "status" => HandleStatus(),
                "heartbeat" => HandleHeartbeat(),
                _ => JsonSerializer.Serialize(new { error = $"Unknown message type: {type}" })
            };
        }
        catch (Exception ex)
        {
            return JsonSerializer.Serialize(new { error = ex.Message });
        }
    }

    private async Task<string> HandleAcquireAsync(string json)
    {
        var wrapper = JsonSerializer.Deserialize<MessageWrapper<AcquireTokensRequest>>(json, Json.Options)!;
        var req = wrapper.Data;

        var result = await _tokenPool.TryAcquireAsync(
            req.Tool,
            req.RequestedTokens,
            req.TimeoutMs);

        var response = new AcquireTokensResponse
        {
            Granted = result.Success,
            LeaseId = result.LeaseId ?? "",
            GrantedTokens = result.GrantedTokens,
            RecommendedParallelism = result.RecommendedParallelism,
            Reason = result.Reason,
            CommitRatio = result.CommitRatio
        };

        if (result.Success)
        {
            Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] ACQUIRE {req.Tool} tokens={result.GrantedTokens} lease={result.LeaseId} commit={result.CommitRatio:P0}");
        }
        else
        {
            Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] DENIED  {req.Tool} reason={result.Reason}");
        }

        return JsonSerializer.Serialize(new { type = "acquire_response", data = response }, Json.Options);
    }

    private async Task<string> HandleReleaseAsync(string json)
    {
        var wrapper = JsonSerializer.Deserialize<MessageWrapper<ReleaseTokensRequest>>(json, Json.Options)!;
        var req = wrapper.Data;

        var result = await _tokenPool.ReleaseAsync(
            req.LeaseId,
            req.PeakWorkingSetBytes,
            req.PeakCommitBytes,
            req.ExitCode,
            req.DurationMs,
            req.StderrHadDiagnostics);

        var response = new ReleaseTokensResponse
        {
            Acknowledged = result.Acknowledged,
            Classification = result.Classification,
            Message = result.Message,
            ShouldRetry = result.ShouldRetry,
            RetryWithTokens = result.RetryWithTokens
        };

        var status = req.ExitCode == 0 ? "OK" : result.Classification.ToString();
        Console.WriteLine($"[{DateTime.Now:HH:mm:ss}] RELEASE lease={req.LeaseId} exit={req.ExitCode} peak={req.PeakCommitBytes / 1024.0 / 1024:F0}MB {status}");

        return JsonSerializer.Serialize(new { type = "release_response", data = response }, Json.Options);
    }

    private string HandleStatus()
    {
        var status = _tokenPool.GetStatus();

        var response = new StatusResponse
        {
            TotalTokens = status.TotalTokens,
            AvailableTokens = status.AvailableTokens,
            ActiveLeases = status.ActiveLeases,
            CommitRatio = status.MemoryStatus.CommitRatio,
            CommitChargeBytes = status.MemoryStatus.CommitChargeBytes,
            CommitLimitBytes = status.MemoryStatus.CommitLimitBytes,
            AvailableMemoryBytes = status.MemoryStatus.AvailablePhysicalBytes,
            RecommendedParallelism = status.RecommendedParallelism
        };

        return JsonSerializer.Serialize(new { type = "status_response", data = response }, Json.Options);
    }

    private string HandleHeartbeat()
    {
        return JsonSerializer.Serialize(new { type = "heartbeat_response", data = new { alive = true, timestamp = DateTime.UtcNow } }, Json.Options);
    }

    public async ValueTask DisposeAsync()
    {
        _cts.Cancel();
        await Task.WhenAll(_connectionTasks);
        _cts.Dispose();
    }
}

internal record MessageWrapper<T>
{
    public required string Type { get; init; }
    public required T Data { get; init; }
}
