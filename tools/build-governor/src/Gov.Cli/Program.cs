using System.Diagnostics;
using System.IO.Pipes;
using System.Text.Json;

/// <summary>
/// Build Governor CLI - one-command wrapper for governed builds.
///
/// Usage:
///   gov run -- cmake --build . -j 16
///   gov run -- msbuild My.sln /m
///   gov status
///   gov help
/// </summary>

if (args.Length == 0)
{
    PrintUsage();
    return 0;
}

var command = args[0].ToLowerInvariant();

return command switch
{
    "run" => await RunCommand(args.Skip(1).ToArray()),
    "status" => await StatusCommand(),
    "help" or "--help" or "-h" => PrintUsage(),
    _ => PrintUsage()
};

static int PrintUsage()
{
    Console.WriteLine("""
        Build Governor - Prevents build system memory exhaustion

        Usage:
          gov run [--no-start] -- <command> [args...]
              Run a command with governed cl.exe/link.exe
              --no-start  Don't auto-start governor if not running

          gov status
              Show governor status and active leases

          gov help
              Show this help message

        Examples:
          gov run -- cmake --build . --parallel 16
          gov run -- msbuild MyProject.sln /m
          gov run -- ninja -j 8

        Environment:
          GOV_REAL_CL     Path to real cl.exe (auto-detected if not set)
          GOV_REAL_LINK   Path to real link.exe (auto-detected if not set)
        """);
    return 0;
}

static async Task<int> RunCommand(string[] args)
{
    // Parse options
    var autoStart = true;
    var commandStart = 0;

    for (var i = 0; i < args.Length; i++)
    {
        if (args[i] == "--")
        {
            commandStart = i + 1;
            break;
        }
        if (args[i] == "--no-start")
        {
            autoStart = false;
        }
    }

    if (commandStart >= args.Length)
    {
        Console.Error.WriteLine("Error: No command specified after '--'");
        Console.Error.WriteLine("Usage: gov run -- <command> [args...]");
        return 1;
    }

    var buildCommand = args[commandStart];
    var buildArgs = args.Skip(commandStart + 1).ToArray();

    // Find wrapper directory - check multiple possible locations
    var exePath = Environment.ProcessPath ?? "";
    var exeDir = Path.GetDirectoryName(exePath) ?? "";

    // Try locations in order of priority
    string[] candidatePaths =
    [
        Path.GetFullPath(Path.Combine(exeDir, "..", "wrappers")),                           // Published layout
        Path.GetFullPath(Path.Combine(exeDir, "..", "..", "..", "..", "bin", "wrappers")), // From published cli
        Path.GetFullPath(Path.Combine(exeDir, "..", "..", "..", "..", "..", "..", "bin", "wrappers")), // dotnet run deep nesting
        Path.GetFullPath(Path.Combine(Environment.CurrentDirectory, "bin", "wrappers")),   // From current working dir
    ];

    var wrapperDir = candidatePaths.FirstOrDefault(p => Directory.Exists(p) && File.Exists(Path.Combine(p, "cl.exe")));

    if (wrapperDir == null)
    {
        Console.Error.WriteLine("Error: Wrapper directory not found. Build the wrappers first:");
        Console.Error.WriteLine("  dotnet publish src/Gov.Wrapper.CL -c Release -o bin/wrappers");
        Console.Error.WriteLine("  dotnet publish src/Gov.Wrapper.Link -c Release -o bin/wrappers");
        return 1;
    }

    // Check if governor is running
    var governorRunning = IsGovernorRunning();

    if (!governorRunning)
    {
        if (autoStart)
        {
            Console.WriteLine("[gov] Starting governor...");
            if (!TryStartGovernor(exeDir))
            {
                Console.WriteLine("[gov] Warning: Could not start governor. Running ungoverned.");
            }
            else
            {
                // Wait for it to be ready
                for (var i = 0; i < 10; i++)
                {
                    await Task.Delay(200);
                    if (IsGovernorRunning())
                    {
                        Console.WriteLine("[gov] Governor ready.");
                        break;
                    }
                }
            }
        }
        else
        {
            Console.WriteLine("[gov] Governor not running (use without --no-start to auto-start)");
        }
    }

    // Set up environment with wrappers in PATH
    var env = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
    foreach (System.Collections.DictionaryEntry e in Environment.GetEnvironmentVariables())
    {
        env[e.Key?.ToString() ?? ""] = e.Value?.ToString() ?? "";
    }

    // Prepend wrapper directory to PATH
    var currentPath = env.GetValueOrDefault("PATH", "");
    env["PATH"] = $"{wrapperDir};{currentPath}";
    env["GOV_ENABLED"] = "1";

    // Auto-detect real tool paths if not set
    if (!env.ContainsKey("GOV_REAL_CL") || string.IsNullOrEmpty(env["GOV_REAL_CL"]))
    {
        var realCl = FindRealTool("cl.exe", wrapperDir, currentPath);
        if (realCl != null) env["GOV_REAL_CL"] = realCl;
    }

    if (!env.ContainsKey("GOV_REAL_LINK") || string.IsNullOrEmpty(env["GOV_REAL_LINK"]))
    {
        var realLink = FindRealTool("link.exe", wrapperDir, currentPath);
        if (realLink != null) env["GOV_REAL_LINK"] = realLink;
    }

    Console.WriteLine($"[gov] Running: {buildCommand} {string.Join(' ', buildArgs)}");
    Console.WriteLine($"[gov] Wrappers: {wrapperDir}");
    if (env.TryGetValue("GOV_REAL_CL", out var cl))
        Console.WriteLine($"[gov] Real cl.exe: {cl}");
    Console.WriteLine();

    // Run the build command
    // Use cmd /c to execute so PATH is respected
    var psi = new ProcessStartInfo
    {
        FileName = "cmd.exe",
        UseShellExecute = false,
    };

    // Build the full command line for cmd /c
    var cmdLine = buildCommand;
    if (buildArgs.Length > 0)
    {
        cmdLine += " " + string.Join(" ", buildArgs.Select(a => a.Contains(' ') ? $"\"{a}\"" : a));
    }
    psi.ArgumentList.Add("/c");
    psi.ArgumentList.Add(cmdLine);

    foreach (var (key, value) in env)
        psi.Environment[key] = value;

    using var process = Process.Start(psi);
    if (process == null)
    {
        Console.Error.WriteLine($"Error: Failed to start '{buildCommand}'");
        return 1;
    }

    await process.WaitForExitAsync();

    Console.WriteLine();
    Console.WriteLine($"[gov] Build exited with code {process.ExitCode}");

    return process.ExitCode;
}

static async Task<int> StatusCommand()
{
    try
    {
        using var pipe = new NamedPipeClientStream(".", "BuildGovernor", PipeDirection.InOut);
        pipe.Connect(2000);

        using var reader = new StreamReader(pipe);
        await using var writer = new StreamWriter(pipe) { AutoFlush = true };

        await writer.WriteLineAsync(JsonSerializer.Serialize(new { type = "status" }));
        var response = await reader.ReadLineAsync();

        if (response == null)
        {
            Console.WriteLine("Governor: not responding");
            return 1;
        }

        using var doc = JsonDocument.Parse(response);
        var data = doc.RootElement.GetProperty("data");

        var totalTokens = data.GetProperty("totalTokens").GetInt32();
        var availableTokens = data.GetProperty("availableTokens").GetInt32();
        var activeLeases = data.GetProperty("activeLeases").GetInt32();
        var commitRatio = data.GetProperty("commitRatio").GetDouble();
        var commitCharge = data.GetProperty("commitChargeBytes").GetInt64() / (1024.0 * 1024 * 1024);
        var commitLimit = data.GetProperty("commitLimitBytes").GetInt64() / (1024.0 * 1024 * 1024);
        var recommended = data.GetProperty("recommendedParallelism").GetInt32();

        Console.WriteLine("╔══════════════════════════════════════════════════════════════════╗");
        Console.WriteLine("║              Build Governor Status                               ║");
        Console.WriteLine("╠══════════════════════════════════════════════════════════════════╣");
        Console.WriteLine($"║  Tokens:      {availableTokens,3} available / {totalTokens,3} total                          ║");
        Console.WriteLine($"║  Leases:      {activeLeases,3} active                                        ║");
        Console.WriteLine($"║  Commit:      {commitRatio:P0} ({commitCharge:F1} GB / {commitLimit:F1} GB)                      ║");
        Console.WriteLine($"║  Recommended: -j {recommended}                                             ║");
        Console.WriteLine("╚══════════════════════════════════════════════════════════════════╝");

        return 0;
    }
    catch (TimeoutException)
    {
        Console.WriteLine("Governor: not running");
        return 1;
    }
    catch (Exception ex)
    {
        Console.WriteLine($"Governor: error ({ex.Message})");
        return 1;
    }
}

static bool IsGovernorRunning()
{
    try
    {
        using var pipe = new NamedPipeClientStream(".", "BuildGovernor", PipeDirection.InOut);
        pipe.Connect(500);
        return true;
    }
    catch
    {
        return false;
    }
}

static bool TryStartGovernor(string exeDir)
{
    try
    {
        // Find governor service
        var serviceDir = Path.GetFullPath(Path.Combine(exeDir, "..", "..", "..", "..", "src", "Gov.Service"));
        if (!Directory.Exists(serviceDir))
        {
            return false;
        }

        var psi = new ProcessStartInfo
        {
            FileName = "dotnet",
            Arguments = $"run --project \"{serviceDir}\" -c Release",
            UseShellExecute = false,
            CreateNoWindow = true
        };

        Process.Start(psi);
        return true;
    }
    catch
    {
        return false;
    }
}

static string? FindRealTool(string toolName, string wrapperDir, string pathEnv)
{
    // First try vswhere
    var vsWherePath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
        "Microsoft Visual Studio", "Installer", "vswhere.exe");

    if (File.Exists(vsWherePath))
    {
        try
        {
            var psi = new ProcessStartInfo
            {
                FileName = vsWherePath,
                Arguments = "-latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath",
                RedirectStandardOutput = true,
                UseShellExecute = false,
                CreateNoWindow = true
            };

            using var proc = Process.Start(psi);
            var vsPath = proc?.StandardOutput.ReadToEnd().Trim();
            proc?.WaitForExit();

            if (!string.IsNullOrEmpty(vsPath))
            {
                // Find the tools directory
                var vcToolsDir = Path.Combine(vsPath, "VC", "Tools", "MSVC");
                if (Directory.Exists(vcToolsDir))
                {
                    var versions = Directory.GetDirectories(vcToolsDir)
                        .OrderByDescending(d => d)
                        .FirstOrDefault();

                    if (versions != null)
                    {
                        var toolPath = Path.Combine(versions, "bin", "Hostx64", "x64", toolName);
                        if (File.Exists(toolPath))
                            return toolPath;
                    }
                }
            }
        }
        catch { }
    }

    // Fall back to PATH search, skipping wrapper directory
    var wrapperDirNorm = Path.GetFullPath(wrapperDir).TrimEnd(Path.DirectorySeparatorChar).ToLowerInvariant();

    foreach (var dir in pathEnv.Split(Path.PathSeparator))
    {
        if (string.IsNullOrWhiteSpace(dir)) continue;

        var dirNorm = Path.GetFullPath(dir).TrimEnd(Path.DirectorySeparatorChar).ToLowerInvariant();
        if (dirNorm == wrapperDirNorm) continue;

        var candidate = Path.Combine(dir, toolName);
        if (File.Exists(candidate))
            return candidate;
    }

    return null;
}
