using System.Diagnostics;
using System.Runtime.InteropServices;
using System.IO.Pipes;

namespace Gov.Common;

/// <summary>
/// Handles automatic startup of the Build Governor service.
/// Ensures only one instance runs system-wide using a global mutex.
/// </summary>
public static class GovernorAutoStart
{
    private const string MutexName = "Global\\BuildGovernorMutex";
    private const string PipeName = "BuildGovernor";
    private const int StartupWaitMs = 3000;
    private const int CheckIntervalMs = 200;

    /// <summary>
    /// Ensures the governor is running. If not, attempts to start it.
    /// Thread-safe: uses global mutex to prevent multiple simultaneous starts.
    /// </summary>
    /// <returns>True if governor is running (or was successfully started)</returns>
    public static bool EnsureRunning()
    {
        // Fast path: check if already running
        if (IsRunning())
            return true;

        // Slow path: try to start it (with mutex to prevent races)
        return TryStartGovernor();
    }

    /// <summary>
    /// Check if governor is currently running by attempting pipe connection.
    /// </summary>
    public static bool IsRunning()
    {
        try
        {
            using var pipe = new NamedPipeClientStream(".", PipeName, PipeDirection.InOut);
            pipe.Connect(100); // Very short timeout for check
            return true;
        }
        catch
        {
            return false;
        }
    }

    /// <summary>
    /// Attempts to start the governor service. Uses mutex to ensure single starter.
    /// </summary>
    private static bool TryStartGovernor()
    {
        // Try to acquire mutex - if another process is already starting, wait for it
        using var mutex = new Mutex(false, MutexName, out bool createdNew);

        try
        {
            // Wait up to 5 seconds for mutex (another process may be starting governor)
            if (!mutex.WaitOne(5000))
            {
                // Timeout - check if governor is now running (started by other process)
                return IsRunning();
            }

            // Double-check after acquiring mutex (another process may have just started it)
            if (IsRunning())
                return true;

            // We have the mutex and governor isn't running - start it
            var servicePath = FindGovernorService();
            if (servicePath == null)
            {
                PrintDebug("Governor service not found");
                return false;
            }

            PrintDebug($"Starting governor: {servicePath}");

            var started = LaunchGovernor(servicePath);
            if (!started)
            {
                PrintDebug("Failed to launch governor process");
                return false;
            }

            // Wait for governor to become ready
            var deadline = DateTime.UtcNow.AddMilliseconds(StartupWaitMs);
            while (DateTime.UtcNow < deadline)
            {
                Thread.Sleep(CheckIntervalMs);
                if (IsRunning())
                {
                    PrintDebug("Governor started successfully");
                    return true;
                }
            }

            PrintDebug("Governor failed to start within timeout");
            return false;
        }
        finally
        {
            try { mutex.ReleaseMutex(); } catch { }
        }
    }

    /// <summary>
    /// Find the governor service executable or project.
    /// </summary>
    private static string? FindGovernorService()
    {
        var candidates = new List<string>();

        // 1. Published service next to current exe or in sibling directory
        var myDir = Path.GetDirectoryName(Environment.ProcessPath) ?? "";
        candidates.Add(Path.Combine(myDir, "Gov.Service.exe"));           // Same directory
        candidates.Add(Path.Combine(myDir, "..", "service", "Gov.Service.exe"));  // bin/wrappers -> bin/service
        candidates.Add(Path.Combine(myDir, "..", "..", "service", "Gov.Service.exe")); // Published deeper

        // 2. Check GOV_SERVICE_PATH environment variable
        var envPath = Environment.GetEnvironmentVariable("GOV_SERVICE_PATH");
        if (!string.IsNullOrEmpty(envPath))
            candidates.Insert(0, envPath);

        // 3. Well-known installation locations
        var programFiles = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles);
        candidates.Add(Path.Combine(programFiles, "BuildGovernor", "Gov.Service.exe"));

        var localAppData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        candidates.Add(Path.Combine(localAppData, "BuildGovernor", "Gov.Service.exe"));

        // 4. Development mode: project directory
        candidates.Add(Path.GetFullPath(Path.Combine(myDir, "..", "..", "..", "..", "src", "Gov.Service")));
        candidates.Add(Path.GetFullPath(Path.Combine(myDir, "..", "..", "..", "..", "..", "..", "src", "Gov.Service")));

        foreach (var candidate in candidates)
        {
            if (File.Exists(candidate))
                return candidate;

            // Check for project directory (development mode)
            if (Directory.Exists(candidate) && File.Exists(Path.Combine(candidate, "Gov.Service.csproj")))
                return candidate;
        }

        return null;
    }

    /// <summary>
    /// Launch the governor process.
    /// </summary>
    private static bool LaunchGovernor(string path)
    {
        try
        {
            ProcessStartInfo psi;

            if (path.EndsWith(".exe", StringComparison.OrdinalIgnoreCase))
            {
                // Published executable
                psi = new ProcessStartInfo
                {
                    FileName = path,
                    Arguments = "--background",
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    WindowStyle = ProcessWindowStyle.Hidden
                };
            }
            else if (Directory.Exists(path))
            {
                // Development mode: run via dotnet
                psi = new ProcessStartInfo
                {
                    FileName = "dotnet",
                    Arguments = $"run --project \"{path}\" -c Release -- --background",
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    WindowStyle = ProcessWindowStyle.Hidden
                };
            }
            else
            {
                return false;
            }

            var process = Process.Start(psi);
            return process != null;
        }
        catch (Exception ex)
        {
            PrintDebug($"Launch failed: {ex.Message}");
            return false;
        }
    }

    /// <summary>
    /// Print debug message if GOV_DEBUG is set.
    /// </summary>
    private static void PrintDebug(string message)
    {
        if (Environment.GetEnvironmentVariable("GOV_DEBUG") == "1")
        {
            Console.Error.WriteLine($"[gov-auto] {message}");
        }
    }
}
