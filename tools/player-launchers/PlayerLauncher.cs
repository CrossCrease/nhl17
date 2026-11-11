using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Web.Script.Serialization;
using System.Windows.Forms;

internal static class PlayerLauncher
{
    [STAThread]
    private static int Main()
    {
        try
        {
            string executableName = Path.GetFileNameWithoutExtension(
                Process.GetCurrentProcess().MainModule.FileName);
            bool playerTwo = string.Equals(executableName, "Player2", StringComparison.OrdinalIgnoreCase);
            string root = FindProjectRoot();
            EnsureServerConnection(root);
            string runtime = playerTwo
                ? EnsurePlayerTwoRuntime(root)
                : Path.Combine(root, "runtime");
            string config = playerTwo
                ? EnsurePlayerTwoConfig(root)
                : Path.Combine(root, "SERVER-CONNECTION.json");

            string script = Path.Combine(root, "tools", "run_nhl17_server.ps1");
            if (!File.Exists(script))
            {
                throw new FileNotFoundException("The NHL 17 server launcher was not found.", script);
            }

            string player = playerTwo ? "player2" : "player1";
            string label = player + "-" + DateTime.Now.ToString("yyyyMMdd-HHmmss-fff");
            string arguments = string.Join(" ", new[]
            {
                "-NoProfile",
                "-ExecutionPolicy Bypass",
                "-File " + Quote(script),
                "-ConfigPath " + Quote(config),
                "-Label " + Quote(label),
                "-RuntimeDirectory " + Quote(runtime),
                "-AllowMultipleInstances"
            });

            ProcessStartInfo startInfo = new ProcessStartInfo
            {
                FileName = "powershell.exe",
                Arguments = arguments,
                WorkingDirectory = root,
                UseShellExecute = true,
                WindowStyle = ProcessWindowStyle.Normal
            };

            using (Process process = Process.Start(startInfo))
            {
                if (process == null)
                {
                    throw new InvalidOperationException("PowerShell could not be started.");
                }
                process.WaitForExit();
                return process.ExitCode;
            }
        }
        catch (Exception exception)
        {
            MessageBox.Show(
                exception.Message,
                "CrossCrease NHL 17 launcher",
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
            return 1;
        }
    }

    private static string FindProjectRoot()
    {
        DirectoryInfo current = new DirectoryInfo(AppDomain.CurrentDomain.BaseDirectory);
        while (current != null)
        {
            if ((File.Exists(Path.Combine(current.FullName, "SERVER-CONNECTION.json")) ||
                 File.Exists(Path.Combine(current.FullName, "SERVER-CONNECTION.example.json"))) &&
                File.Exists(Path.Combine(current.FullName, "tools", "run_nhl17_server.ps1")))
            {
                return current.FullName;
            }
            current = current.Parent;
        }
        throw new DirectoryNotFoundException("Could not locate the NHL 17 project root.");
    }

    // SERVER-CONNECTION.json is personal and not in Git; start from the shared template.
    private static void EnsureServerConnection(string root)
    {
        string personal = Path.Combine(root, "SERVER-CONNECTION.json");
        string template = Path.Combine(root, "SERVER-CONNECTION.example.json");
        if (!File.Exists(personal) && File.Exists(template)) File.Copy(template, personal);
    }

    private static string EnsurePlayerTwoRuntime(string root)
    {
        string source = Path.Combine(root, "runtime");
        string destination = Path.Combine(root, "runtime-player2");
        if (!Directory.Exists(source))
        {
            throw new DirectoryNotFoundException("The primary runtime folder was not found: " + source);
        }
        if (!Directory.Exists(destination))
        {
            CopyDirectory(source, destination);
        }
        if (!File.Exists(Path.Combine(destination, "shadPS4.exe")) ||
            !File.Exists(Path.Combine(destination, "user", "config.json")) ||
            !File.Exists(Path.Combine(destination, "user", "users.json")))
        {
            throw new InvalidDataException("The Player 2 runtime is incomplete: " + destination);
        }
        return destination;
    }

    private static string EnsurePlayerTwoConfig(string root)
    {
        string source = Path.Combine(root, "SERVER-CONNECTION.json");
        string outputDirectory = Path.Combine(root, "artifacts", "player-launchers");
        string destination = Path.Combine(outputDirectory, "SERVER-CONNECTION-player2.json");
        if (!File.Exists(source))
        {
            throw new FileNotFoundException("The server connection configuration was not found.", source);
        }

        Directory.CreateDirectory(outputDirectory);
        JavaScriptSerializer serializer = new JavaScriptSerializer();
        Dictionary<string, object> configuration = serializer.DeserializeObject(File.ReadAllText(source))
            as Dictionary<string, object>;
        if (configuration == null)
        {
            throw new InvalidDataException("The server connection configuration is not a JSON object.");
        }
        configuration["online_id"] = "NHL17Probe2";
        File.WriteAllText(destination, serializer.Serialize(configuration) + Environment.NewLine);
        return destination;
    }

    private static void CopyDirectory(string source, string destination)
    {
        Directory.CreateDirectory(destination);
        foreach (string file in Directory.GetFiles(source))
        {
            File.Copy(file, Path.Combine(destination, Path.GetFileName(file)));
        }
        foreach (string directory in Directory.GetDirectories(source))
        {
            CopyDirectory(directory, Path.Combine(destination, Path.GetFileName(directory)));
        }
    }

    private static string Quote(string value)
    {
        return "\"" + value.Replace("\"", "\\\"") + "\"";
    }
}
