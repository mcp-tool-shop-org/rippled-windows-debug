using Gov.Protocol;

namespace Gov.Common;

/// <summary>
/// Classifies build tool failures as OOM vs normal errors.
/// </summary>
public static class FailureClassifier
{
    /// <summary>
    /// Classify a tool exit based on process stats and system state.
    /// </summary>
    public static ClassificationResult Classify(ClassificationInput input)
    {
        // Success case
        if (input.ExitCode == 0)
        {
            return new ClassificationResult
            {
                Classification = FailureClassification.Success,
                Message = null,
                ShouldRetry = false,
                Confidence = 1.0
            };
        }

        // Gather evidence for OOM
        var oomEvidence = 0.0;
        var reasons = new List<string>();

        // High commit ratio at exit
        if (input.CommitRatioAtExit >= 0.92)
        {
            oomEvidence += 0.4;
            reasons.Add($"commit ratio {input.CommitRatioAtExit:P0} at exit");
        }
        else if (input.CommitRatioAtExit >= 0.88)
        {
            oomEvidence += 0.25;
            reasons.Add($"commit ratio {input.CommitRatioAtExit:P0} at exit");
        }

        // Peak commit ratio during execution
        if (input.PeakCommitRatioDuringExecution >= 0.95)
        {
            oomEvidence += 0.3;
            reasons.Add($"peak commit ratio {input.PeakCommitRatioDuringExecution:P0} during execution");
        }

        // Process used significant memory
        if (input.PeakProcessCommitGb >= 2.5)
        {
            oomEvidence += 0.2;
            reasons.Add($"process peaked at {input.PeakProcessCommitGb:F1} GB");
        }

        // No stderr diagnostics (silent death)
        if (!input.StderrHadDiagnostics)
        {
            oomEvidence += 0.2;
            reasons.Add("no compiler diagnostics in stderr");
        }

        // Short duration with high memory (crashed fast)
        if (input.DurationMs < 5000 && input.PeakProcessCommitGb >= 1.5)
        {
            oomEvidence += 0.15;
            reasons.Add("short duration with high memory");
        }

        // Classify based on evidence
        FailureClassification classification;
        string? message;
        bool shouldRetry;

        if (oomEvidence >= 0.6)
        {
            classification = FailureClassification.LikelyOOM;
            message = FormatOomMessage(input, reasons);
            shouldRetry = true;
        }
        else if (oomEvidence >= 0.4)
        {
            classification = FailureClassification.LikelyPagingDeath;
            message = FormatPagingMessage(input, reasons);
            shouldRetry = true;
        }
        else if (input.StderrHadDiagnostics)
        {
            classification = FailureClassification.NormalCompileError;
            message = null;
            shouldRetry = false;
        }
        else
        {
            classification = FailureClassification.Unknown;
            message = $"Build failed (exit code {input.ExitCode}). Unable to determine cause.";
            shouldRetry = false;
        }

        return new ClassificationResult
        {
            Classification = classification,
            Message = message,
            ShouldRetry = shouldRetry,
            Confidence = Math.Min(1.0, oomEvidence),
            Reasons = reasons
        };
    }

    private static string FormatOomMessage(ClassificationInput input, List<string> reasons)
    {
        var reasonStr = string.Join(", ", reasons);
        return $"""
            ╔══════════════════════════════════════════════════════════════════╗
            ║  BUILD FAILED: Memory Pressure Detected                          ║
            ╠══════════════════════════════════════════════════════════════════╣
            ║  Exit code: {input.ExitCode,-5}                                              ║
            ║  System commit: {input.CommitRatioAtExit:P0} ({input.CommitChargeGb:F1} GB / {input.CommitLimitGb:F1} GB)              ║
            ║  Process peak:  {input.PeakProcessCommitGb:F1} GB                                        ║
            ║  Evidence: {reasonStr,-52} ║
            ╠══════════════════════════════════════════════════════════════════╣
            ║  Recommendation: Reduce parallelism                              ║
            ║    CMAKE_BUILD_PARALLEL_LEVEL={input.RecommendedParallelism}                               ║
            ║    MSBuild: /m:{input.RecommendedParallelism}                                              ║
            ║    Ninja: -j{input.RecommendedParallelism}                                                 ║
            ╚══════════════════════════════════════════════════════════════════╝
            """;
    }

    private static string FormatPagingMessage(ClassificationInput input, List<string> reasons)
    {
        var reasonStr = string.Join(", ", reasons);
        return $"""
            ╔══════════════════════════════════════════════════════════════════╗
            ║  BUILD FAILED: Possible Paging Pressure                          ║
            ╠══════════════════════════════════════════════════════════════════╣
            ║  Exit code: {input.ExitCode,-5}                                              ║
            ║  System commit: {input.CommitRatioAtExit:P0}                                         ║
            ║  Process peak:  {input.PeakProcessCommitGb:F1} GB                                        ║
            ║  Evidence: {reasonStr,-52} ║
            ╠══════════════════════════════════════════════════════════════════╣
            ║  Will retry with reduced parallelism...                          ║
            ╚══════════════════════════════════════════════════════════════════╝
            """;
    }
}

public sealed record ClassificationInput
{
    public required int ExitCode { get; init; }
    public required int DurationMs { get; init; }
    public required double CommitRatioAtExit { get; init; }
    public required double PeakCommitRatioDuringExecution { get; init; }
    public required double PeakProcessCommitGb { get; init; }
    public required bool StderrHadDiagnostics { get; init; }
    public required double CommitChargeGb { get; init; }
    public required double CommitLimitGb { get; init; }
    public required int RecommendedParallelism { get; init; }
}

public sealed record ClassificationResult
{
    public required FailureClassification Classification { get; init; }
    public required string? Message { get; init; }
    public required bool ShouldRetry { get; init; }
    public required double Confidence { get; init; }
    public List<string>? Reasons { get; init; }
}
