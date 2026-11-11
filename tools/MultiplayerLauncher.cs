using System;
using System.Diagnostics;
using System.IO;
using System.Windows.Forms;
class MultiplayerLauncher {
    [STAThread]
    static int Main(string[] args) {
#if PLAYER2
        const string player = "2";
#else
        const string player = "1";
#endif
        string root = AppDomain.CurrentDomain.BaseDirectory;
        string script = Path.Combine(root, "tools", "launch_multiplayer_player.ps1");
        string shell = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "PowerShell", "7", "pwsh.exe");
        try {
            if (!File.Exists(shell)) shell = File.ReadAllText(Path.Combine(root, "tools", "multiplayer-powershell-path.txt")).Trim();
            if (!File.Exists(shell) || !File.Exists(script)) throw new FileNotFoundException("Keep this launcher in the NHL17 folder. PowerShell 7 and tools/launch_multiplayer_player.ps1 are required.");
            if (args.Length == 1 && args[0] == "--check") return 0;
            var start = new ProcessStartInfo(shell, "-NoProfile -ExecutionPolicy Bypass -File \"" + script + "\" -Player " + player) {
                WorkingDirectory = root, UseShellExecute = false, CreateNoWindow = true,
                WindowStyle = ProcessWindowStyle.Hidden, RedirectStandardError = true
            };
            using (var process = Process.Start(start)) {
                string error = process.StandardError.ReadToEnd();
                process.WaitForExit();
                if (process.ExitCode != 0) throw new Exception(string.IsNullOrWhiteSpace(error) ? "Launch failed. Check the NHL17 runtime logs." : error);
            }
            return 0;
        } catch (Exception error) {
            MessageBox.Show(error.Message, "NHL17 Player " + player, MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        }
    }
}

