// CrossCrease NHL 17 start program (tools\crosscrease-nhl17.exe in the runtime package). The CrossCrease
// launcher runs it for Play and for renaming an online ID:
//
//   crosscrease-nhl17 play -Game <eboot.bin> -DataDirectory <folder> [-ServerAddress <host>] [-OnlineId <id>] [-BaseOnly]
//                          [-Resolution 720p|900p|1080p|1440p] [-FrameRate 60|120]
//   crosscrease-nhl17 rename -OnlineId <id> -NewOnlineId <id> [-ServerAddress <host>]
//
// "play" does what tools\run_nhl17_online.ps1 (with run_nhl17.ps1) does for a player's data folder, and
// "rename" what tools\rename_online_id.ps1 does, without needing Windows PowerShell, which Wine and
// Proton do not provide. Keep the scripts and this program in step. The password comes from the
// CROSSCREASE_SETTING_PASSWORD environment variable (or -Password) and never appears on a command line.
//
// Every Play writes <data>\logs\online-<time>\launch.log; a failed start is also shown in a message box.
using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Net.Security;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Security.Authentication;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Web.Script.Serialization;
using System.Windows.Forms;
using System.Xml;

namespace CrossCrease.Nhl17
{
    sealed class Failure : Exception
    {
        public Failure(string message) : base(message) { }
    }

    static class Program
    {
        static readonly Regex OnlineIdPattern = new Regex("^[a-zA-Z0-9_-]{3,16}$");
        static readonly Regex ServerPattern = new Regex("^[a-zA-Z0-9.:-]+$");
        static readonly string[] Switches = { "BaseOnly", "AllowMultipleInstances" };
        static readonly UTF8Encoding Utf8 = new UTF8Encoding(false);
        static StreamWriter log;

        [STAThread]
        static int Main(string[] args)
        {
            string command = args.Length > 0 ? args[0].ToLowerInvariant() : "";
            if (command == "rename") return Rename(args);
            if (command == "play") return Play(args);
            const string usage = "Usage:\n  crosscrease-nhl17 play -Game <eboot.bin> -DataDirectory <folder> [-ServerAddress <host>] [-OnlineId <id>] [-BaseOnly]\n" +
                "                         [-Resolution 720p|900p|1080p|1440p] [-FrameRate 60|120]\n" +
                "  crosscrease-nhl17 rename -OnlineId <id> -NewOnlineId <id> [-ServerAddress <host>]";
            Console.Error.WriteLine(usage);
            return 2;
        }

        // ----- Shared -------------------------------------------------------------------------

        static Dictionary<string, string> ParseOptions(string[] args)
        {
            var options = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            for (int i = 1; i < args.Length; i++)
            {
                string arg = args[i];
                if (!arg.StartsWith("-") || arg.Length < 2) throw new Failure("Unexpected argument: " + arg);
                string name = arg.TrimStart('-');
                if (Array.Exists(Switches, s => s.Equals(name, StringComparison.OrdinalIgnoreCase))) { options[name] = "true"; continue; }
                if (i + 1 >= args.Length) throw new Failure("Missing value for " + arg);
                options[name] = args[++i];
            }
            return options;
        }

        static string Option(Dictionary<string, string> options, string name)
        {
            string value;
            return options.TryGetValue(name, out value) && !string.IsNullOrEmpty(value) ? value : null;
        }

        static string ToolsDirectory { get { return AppDomain.CurrentDomain.BaseDirectory; } }

        static string PackagePath(Dictionary<string, string> options, string option, string folder)
        {
            return Path.GetFullPath(Option(options, option) ?? Path.Combine(ToolsDirectory, "..", folder));
        }

        static string TakePassword(Dictionary<string, string> options)
        {
            string password = Option(options, "Password") ?? Environment.GetEnvironmentVariable("CROSSCREASE_SETTING_PASSWORD");
            Environment.SetEnvironmentVariable("CROSSCREASE_SETTING_PASSWORD", null);
            return password ?? "";
        }

        static object ReadJson(string path)
        {
            return new JavaScriptSerializer { MaxJsonLength = int.MaxValue }.DeserializeObject(File.ReadAllText(path));
        }

        static void WriteJson(string path, object value)
        {
            File.WriteAllText(path, new JavaScriptSerializer { MaxJsonLength = int.MaxValue }.Serialize(value), Utf8);
        }

        static Dictionary<string, object> Section(Dictionary<string, object> parent, string name)
        {
            object value;
            var section = parent.TryGetValue(name, out value) ? value as Dictionary<string, object> : null;
            if (section == null) parent[name] = section = new Dictionary<string, object>();
            return section;
        }

        static string Text(Dictionary<string, object> map, string name)
        {
            object value;
            return map.TryGetValue(name, out value) && value != null ? Convert.ToString(value, CultureInfo.InvariantCulture) : "";
        }

        static IEnumerable<Dictionary<string, object>> Users(object users)
        {
            var root = users as Dictionary<string, object>;
            object section, list;
            if (root == null || !root.TryGetValue("Users", out section) || !(section is Dictionary<string, object>)) yield break;
            if (!((Dictionary<string, object>)section).TryGetValue("user", out list)) yield break;
            if (list is Dictionary<string, object>) { yield return (Dictionary<string, object>)list; yield break; }
            var items = list as IEnumerable;
            if (items == null) yield break;
            foreach (var item in items)
                if (item is Dictionary<string, object>) yield return (Dictionary<string, object>)item;
        }

        static void Log(string message)
        {
            if (log == null) return;
            try { log.WriteLine(DateTime.Now.ToString("HH:mm:ss.fff", CultureInfo.InvariantCulture) + "  " + message); } catch { }
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern IntPtr GetModuleHandle(string name);
        [DllImport("kernel32.dll", CharSet = CharSet.Ansi)] static extern IntPtr GetProcAddress(IntPtr module, string name);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate IntPtr WineGetVersion();

        // Wine and Proton name their version in ntdll; on Windows this returns null.
        static string WineVersion()
        {
            try
            {
                IntPtr function = GetProcAddress(GetModuleHandle("ntdll.dll"), "wine_get_version");
                if (function == IntPtr.Zero) return null;
                var call = (WineGetVersion)Marshal.GetDelegateForFunctionPointer(function, typeof(WineGetVersion));
                return Marshal.PtrToStringAnsi(call());
            }
            catch { return null; }
        }

        // ----- Play ---------------------------------------------------------------------------

        static int Play(string[] args)
        {
            string session = null;
            try
            {
                var options = ParseOptions(args);
                string game = Option(options, "Game");
                string dataDirectory = Option(options, "DataDirectory");
                if (game == null || dataDirectory == null) throw new Failure("Play needs -Game and -DataDirectory.");
                string dataPath = Path.GetFullPath(dataDirectory);
                string userPath = Path.Combine(dataPath, "user");
                string artifacts = Path.Combine(dataPath, "logs");
                session = Path.Combine(artifacts, "online-" + DateTime.Now.ToString("yyyyMMdd-HHmmss", CultureInfo.InvariantCulture));
                Directory.CreateDirectory(session);
                log = new StreamWriter(Path.Combine(session, "launch.log"), false, Utf8) { AutoFlush = true };
                string wine = WineVersion();
                Log("CrossCrease NHL 17 start program; " + Environment.OSVersion + (wine != null ? "; Wine " + wine : "") + "; .NET " + Environment.Version);
                return PlaySession(options, game, dataPath, userPath, artifacts, session);
            }
            catch (Exception error)
            {
                string message = error is Failure ? error.Message : error.GetType().Name + ": " + error.Message;
                Log("Failed: " + error);
                if (log != null) log.Dispose();
                string details = session != null ? "\n\nDetails: " + Path.Combine(session, "launch.log") : "";
                try { MessageBox.Show(message + details, "Could not start NHL 17", MessageBoxButtons.OK, MessageBoxIcon.Error); }
                catch { Console.Error.WriteLine(message + details); }
                return 1;
            }
        }

        static int PlaySession(Dictionary<string, string> options, string game, string dataPath, string userPath, string artifacts, string session)
        {
            bool baseOnly = options.ContainsKey("BaseOnly");
            bool allowMultipleInstances = options.ContainsKey("AllowMultipleInstances");
            // Graphics options, as in run_nhl17.ps1: 900p is the game's own render resolution.
            string resolution = (Option(options, "Resolution") ?? "900p").ToLowerInvariant();
            if (Array.IndexOf(new[] { "720p", "900p", "1080p", "1440p" }, resolution) < 0) throw new Failure("Invalid resolution: " + resolution + " (use 720p, 900p, 1080p or 1440p).");
            string frameRateText = Option(options, "FrameRate") ?? "60";
            if (frameRateText != "60" && frameRateText != "120") throw new Failure("Invalid frame rate: " + frameRateText + " (use 60 or 120).");
            int frameRate = int.Parse(frameRateText, CultureInfo.InvariantCulture);
            string password = TakePassword(options);

            string connectionPath = PackagePath(options, "ConnectionDirectory", "online");
            string connectionFile = Path.Combine(connectionPath, "server_connection.json");
            if (!File.Exists(connectionFile)) throw new Failure("The runtime's online connection files are missing (" + connectionPath + "). Reinstall the NHL 17 runtime from the launcher.");
            var connection = (Dictionary<string, object>)ReadJson(connectionFile);
            string server = Option(options, "ServerAddress");
            if (server == null || server == "default") server = Text(connection, "server_address");
            if (!ServerPattern.IsMatch(server)) throw new Failure("Invalid server address: " + server);
            string onlineId = Option(options, "OnlineId") ?? "NHL17Player";
            if (!OnlineIdPattern.IsMatch(onlineId)) throw new Failure("Invalid online ID: " + onlineId);
            string trustPatch = Path.Combine(connectionPath, Text(connection, "trust_patch_file"));
            if (!File.Exists(trustPatch)) throw new Failure("The server trust patch is missing: " + trustPatch);

            string runtimePath = PackagePath(options, "RuntimeDirectory", "runtime");
            string exe = Path.Combine(runtimePath, "shadPS4.exe");
            if (!File.Exists(exe)) throw new Failure("The emulator is missing: " + exe + ". Reinstall the NHL 17 runtime from the launcher.");
            string gamePath = Path.GetFullPath(game);
            if (!File.Exists(gamePath) && !Directory.Exists(gamePath)) throw new Failure("The game was not found: " + gamePath);
            Log("Game " + gamePath + (baseOnly ? " (base game)" : "") + "; data " + dataPath + "; server " + server + "; online ID " + onlineId +
                "; resolution " + resolution + "; frame rate " + frameRate);

            foreach (var running in Process.GetProcessesByName("shadPS4"))
            {
                string path = null;
                try { path = running.MainModule.FileName; } catch { }
                if (path != null && string.Equals(Path.GetFullPath(path), exe, StringComparison.OrdinalIgnoreCase))
                    throw new Failure("NHL 17 is already running from this runtime.");
                if (!allowMultipleInstances) throw new Failure("A shadPS4 game is already running. Close it and press Play again.");
            }

            string hostOverrides = Path.Combine(session, "host_overrides.json");
            var overrides = new Dictionary<string, object>();
            overrides["_pack"] = Text(connection, "pack");
            overrides["_server"] = server;
            overrides[Text(connection, "redirector_hostname")] = server;
            WriteJson(hostOverrides, overrides);

            // Missing defaults are seeded first so there is a profile to back up.
            Directory.CreateDirectory(userPath);
            foreach (var name in new[] { "config.json", "users.json" })
            {
                string target = Path.Combine(userPath, name);
                if (!File.Exists(target)) File.Copy(Path.Combine(runtimePath, "defaults", "user", name), target);
            }
            string configPath = Path.Combine(userPath, "config.json");
            string usersPath = Path.Combine(userPath, "users.json");

            // shadPS4 asks a "Save Migration" question whenever it creates a player's home folder. Nobody
            // can answer it in a hidden launch, so create the (empty) folders it would create.
            foreach (var player in Users(ReadJson(usersPath)))
            {
                string playerHome = Path.Combine(userPath, "home", Text(player, "user_id"));
                if (!Directory.Exists(playerHome))
                    foreach (var folder in new[] { "savedata", "trophy", "inputs" }) Directory.CreateDirectory(Path.Combine(playerHome, folder));
            }
            if (baseOnly) BackUpSavesForBaseGame(gamePath, dataPath, userPath);

            string configBackup = Path.Combine(session, "config.before.json");
            string usersBackup = Path.Combine(session, "users.before.json");
            File.Copy(configPath, configBackup, true);
            File.Copy(usersPath, usersBackup, true);
            try
            {
                var config = (Dictionary<string, object>)ReadJson(configPath);
                var network = Section(config, "Network");
                network["connected_to_network"] = true;
                network["shad_net_enabled"] = true;
                // Sign-in is encrypted when the connection files name a TLS port and the CA certificate.
                string caCertificate = "";
                string identityPort = Text(connection, "identity_tcp_port");
                if (Text(connection, "identity_tls_port") != "")
                {
                    identityPort = Text(connection, "identity_tls_port");
                    caCertificate = Path.GetFullPath(Path.Combine(connectionPath, Text(connection, "identity_ca_certificate_file")));
                    if (!File.Exists(caCertificate)) throw new Failure("The server certificate is missing: " + caCertificate);
                }
                network["shadnet_server"] = server + ":" + identityPort;
                network["shadnet_ca_certificate"] = caCertificate;
                network["shadnet_webapi_server"] = caCertificate != "" && Text(connection, "psn_webapi_tls_port") != ""
                    ? "https://" + server + ":" + Text(connection, "psn_webapi_tls_port")
                    : "http://" + server + ":" + Text(connection, "psn_webapi_http_port");
                Section(config, "GPU")["readback_linear_images_enabled"] = true;
                Section(config, "GPU")["vblank_frequency"] = frameRate;
                Section(config, "Vulkan")["vkvalidation_enabled"] = false;
                Section(config, "Vulkan")["vkvalidation_sync_enabled"] = false;
                Section(config, "Log")["filter"] = "*:warning";
                WriteJson(configPath, config);

                var users = ReadJson(usersPath);
                Dictionary<string, object> user = null;
                foreach (var candidate in Users(users))
                    if (Text(candidate, "player_index") == "1") { user = candidate; break; }
                if (user == null) throw new Failure("No primary emulator user exists in " + usersPath + ".");
                user["shadnet_npid"] = onlineId;
                // The emulator takes the password from SHADPS4_SHADNET_PASSWORD, so it is never written to users.json.
                user["shadnet_password"] = "";
                user["shadnet_token"] = "";
                user["shadnet_email"] = "";
                user["shadnet_enabled"] = true;
                WriteJson(usersPath, users);

                string arguments = "--show-fps -g \"" + gamePath + "\"";
                if (baseOnly) arguments += " --ignore-game-patch";
                string patchFile = CombinePatches(baseOnly, resolution, trustPatch, artifacts, "online");
                if (patchFile != null) arguments += " -p \"" + patchFile + "\"";

                var start = new ProcessStartInfo(exe, arguments)
                {
                    WorkingDirectory = dataPath,
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true
                };
                var environment = start.EnvironmentVariables;
                environment["SDL_WINDOW_ACTIVATE_WHEN_SHOWN"] = "0";
                environment["SDL_WINDOW_ACTIVATE_WHEN_RAISED"] = "0";
                environment["SHADPS4_ENABLE_IPC"] = "false";
                environment["SHADPS4_PAD_PIPE"] = "true";
                environment["SHADPS4_BUFFER_READBACK_KIB"] = "128";
                environment["SHADPS4_NET_HOST_OVERRIDES_JSON"] = hostOverrides;
                environment["VK_LOADER_LAYERS_DISABLE"] = "~implicit~";
                environment["SHADPS4_SHADNET_PASSWORD"] = password != "" ? password : "local-only";
                foreach (var name in new[] { "SHADPS4_LOG_FRAME_STATS", "SHADPS4_PROFILE_SCOPES", "SHADPS4_NET_CAPTURE_DIR", "CROSSCREASE_SETTING_PASSWORD" })
                    environment.Remove(name);
                password = "";

                Log("Starting " + exe + " " + arguments);
                DateTime started = DateTime.Now;
                using (var process = Process.Start(start))
                {
                    var pumps = new[] {
                        Pump(process.StandardOutput.BaseStream, Path.Combine(artifacts, "online.stdout.log")),
                        Pump(process.StandardError.BaseStream, Path.Combine(artifacts, "online.stderr.log"))
                    };
                    File.WriteAllText(Path.Combine(artifacts, "online.pid"), process.Id.ToString(CultureInfo.InvariantCulture) + "\r\n");
                    WriteRunInfo(Path.Combine(artifacts, "online.run.json"), process.Id, started, exe, gamePath, arguments, config, hostOverrides, patchFile, resolution);
                    Log("Game process " + process.Id + " started; waiting for it to close.");
                    process.WaitForExit();
                    foreach (var pump in pumps) pump.Join(5000);
                    int exitCode = process.ExitCode;
                    Log("Game closed with exit code 0x" + exitCode.ToString("x8", CultureInfo.InvariantCulture) + ".");
                    // The emulator writes user\log\crash.txt itself when it catches a crash. For a crash it
                    // cannot catch, write one from the exit code so the launcher can still offer to report it.
                    string crashFile = Path.Combine(userPath, "log", "crash.txt");
                    bool caught = File.Exists(crashFile) && File.GetLastWriteTime(crashFile) >= started;
                    if (exitCode != 0 && !caught)
                    {
                        Directory.CreateDirectory(Path.GetDirectoryName(crashFile));
                        File.WriteAllText(crashFile, string.Join("\n", new[] {
                            "format=1", "kind=exit", "code=0x" + exitCode.ToString("x8", CultureInfo.InvariantCulture), "address=",
                            "time=" + DateTime.UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ", CultureInfo.InvariantCulture), "log=shad_log.txt" }) + "\n");
                    }
                }
            }
            finally
            {
                File.Copy(configBackup, configPath, true);
                File.Copy(usersBackup, usersPath, true);
                Log("Restored the player's config.json and users.json.");
            }
            log.Dispose();
            return 0;
        }

        // The base game cannot load a profile saved by the update, and its "Create New Profile" replaces
        // it. When an update exists, keep a copy of the saves before starting the base game (newest 5).
        static void BackUpSavesForBaseGame(string gamePath, string dataPath, string userPath)
        {
            string gameFolder = File.Exists(gamePath) ? Path.GetDirectoryName(gamePath) : gamePath;
            if (!Directory.Exists(gameFolder + "-UPDATE") && !Directory.Exists(gameFolder + "-patch")) return;
            string home = Path.Combine(userPath, "home");
            var saves = new List<DirectoryInfo>();
            if (Directory.Exists(home))
                foreach (var player in new DirectoryInfo(home).GetDirectories())
                {
                    var save = new DirectoryInfo(Path.Combine(player.FullName, "savedata"));
                    if (save.Exists && save.GetFiles("*", SearchOption.AllDirectories).Length > 0) saves.Add(save);
                }
            if (saves.Count == 0) return;
            string backupRoot = Path.Combine(dataPath, "save-backups");
            string backup = Path.Combine(backupRoot, DateTime.Now.ToString("yyyyMMdd-HHmmss", CultureInfo.InvariantCulture) + "-before-base-game");
            foreach (var save in saves) CopyDirectory(save.FullName, Path.Combine(backup, save.Parent.Name, "savedata"));
            Log("Backed up the saves to " + backup + ".");
            var old = new DirectoryInfo(backupRoot).GetDirectories();
            Array.Sort(old, (a, b) => string.CompareOrdinal(b.Name, a.Name));
            for (int i = 5; i < old.Length; i++) old[i].Delete(true);
        }

        static void CopyDirectory(string source, string target)
        {
            Directory.CreateDirectory(target);
            foreach (var file in Directory.GetFiles(source)) File.Copy(file, Path.Combine(target, Path.GetFileName(file)));
            foreach (var folder in Directory.GetDirectories(source)) CopyDirectory(folder, Path.Combine(target, Path.GetFileName(folder)));
        }

        // shadPS4 takes one patch file. The CrossCrease compatibility patch (update 1.08 only) and the
        // resolution patch (1.00 and 1.08) are combined with the server trust patch.
        static string CombinePatches(bool baseOnly, string resolution, string trustPatch, string artifacts, string label)
        {
            var files = new List<string>();
            string patches = Path.Combine(ToolsDirectory, "..", "patches");
            string compatibility = Path.GetFullPath(Path.Combine(patches, "nhl17-compat.xml"));
            if (!baseOnly && File.Exists(compatibility)) files.Add(compatibility);
            if (resolution != "900p")
            {
                string resolutionPatch = Path.GetFullPath(Path.Combine(patches, "nhl17-resolution-" + resolution + ".xml"));
                if (!File.Exists(resolutionPatch)) throw new Failure("The resolution patch is missing: " + resolutionPatch + ". Reinstall the NHL 17 runtime from the launcher.");
                files.Add(resolutionPatch);
            }
            files.Add(Path.GetFullPath(trustPatch));
            if (files.Count == 1) return files[0];
            var combined = new XmlDocument();
            combined.Load(files[0]);
            for (int i = 1; i < files.Count; i++)
            {
                var document = new XmlDocument();
                document.Load(files[i]);
                foreach (XmlNode metadata in document.SelectNodes("/Patch/Metadata"))
                    combined.DocumentElement.AppendChild(combined.ImportNode(metadata, true));
            }
            string output = Path.Combine(artifacts, label + ".patch.xml");
            combined.Save(output);
            return output;
        }

        static Thread Pump(Stream from, string path)
        {
            var thread = new Thread(() =>
            {
                try { using (var to = new FileStream(path, FileMode.Create, FileAccess.Write, FileShare.ReadWrite)) { var buffer = new byte[16384]; int read; while ((read = from.Read(buffer, 0, buffer.Length)) > 0) { to.Write(buffer, 0, read); to.Flush(); } } }
                catch (Exception error) { Log("Could not record " + path + ": " + error.Message); }
            }) { IsBackground = true };
            thread.Start();
            return thread;
        }

        static void WriteRunInfo(string path, int processId, DateTime started, string exe, string gamePath, string arguments,
            Dictionary<string, object> config, string hostOverrides, string patchFile, string resolution)
        {
            string hash;
            using (var sha = SHA256.Create())
            using (var stream = File.OpenRead(exe))
                hash = BitConverter.ToString(sha.ComputeHash(stream)).Replace("-", "");
            var gpu = Section(config, "GPU");
            var info = new Dictionary<string, object>();
            info["ProcessId"] = processId;
            info["StartedUtc"] = started.ToUniversalTime().ToString("o", CultureInfo.InvariantCulture);
            info["Executable"] = exe;
            info["ExecutableSha256"] = hash;
            info["Game"] = gamePath;
            info["Arguments"] = arguments;
            info["Validation"] = false;
            info["FrameStats"] = false;
            info["ProfileScopes"] = false;
            info["ImplicitOverlaysDisabled"] = true;
            info["BufferReadbackWindowKiB"] = 128;
            foreach (var pair in new[] { new[] { "GpuReadbacksMode", "readbacks_mode" }, new[] { "LinearImageReadbacks", "readback_linear_images_enabled" },
                new[] { "FastShaderMath", "fast_shader_math" }, new[] { "VblankFrequency", "vblank_frequency" }, new[] { "InternalWidth", "internal_screen_width" },
                new[] { "InternalHeight", "internal_screen_height" }, new[] { "PresentMode", "present_mode" } })
            {
                object value;
                info[pair[0]] = gpu.TryGetValue(pair[1], out value) ? value : null;
            }
            info["NetworkCaptureDirectory"] = null;
            info["NetworkHostOverridesJson"] = hostOverrides;
            info["Resolution"] = resolution;
            info["PatchFile"] = patchFile ?? "";
            info["StartProgram"] = "crosscrease-nhl17.exe";
            info["Wine"] = WineVersion();
            WriteJson(path, info);
        }

        // ----- Rename -------------------------------------------------------------------------
        // Renames a player's online ID on a CrossCrease server; the account keeps HUT, stats, friends and
        // the password. Prints one line for the player and exits with 0 when the server renamed the account.

        static int Rename(string[] args)
        {
            try
            {
                Console.Out.WriteLine(RenameOnServer(ParseOptions(args)));
                return 0;
            }
            catch (Failure failure)
            {
                Console.Out.WriteLine(failure.Message);
                return 1;
            }
            catch (Exception error)
            {
                Console.Out.WriteLine("The rename failed: " + error.Message);
                return 1;
            }
        }

        static string RenameOnServer(Dictionary<string, string> options)
        {
            string onlineId = Option(options, "OnlineId") ?? "";
            string newOnlineId = Option(options, "NewOnlineId") ?? "";
            if (!OnlineIdPattern.IsMatch(onlineId)) throw new Failure("Invalid online ID: " + onlineId);
            string password = TakePassword(options);
            if (password == "") throw new Failure("Enter your password in the launcher first.");
            string connectionPath = PackagePath(options, "ConnectionDirectory", "online");
            Dictionary<string, object> connection;
            try { connection = (Dictionary<string, object>)ReadJson(Path.Combine(connectionPath, "server_connection.json")); }
            catch { throw new Failure("The runtime's online connection files are missing (" + connectionPath + ")."); }
            string server = Option(options, "ServerAddress");
            if (server == null || server == "default") server = Text(connection, "server_address");
            if (!ServerPattern.IsMatch(server)) throw new Failure("Invalid server address: " + server);
            int port = int.Parse(Text(connection, "identity_tcp_port"), CultureInfo.InvariantCulture);
            X509Certificate2 ca = null;
            if (Text(connection, "identity_tls_port") != "")
            {
                port = int.Parse(Text(connection, "identity_tls_port"), CultureInfo.InvariantCulture);
                try { ca = new X509Certificate2(Path.Combine(connectionPath, Text(connection, "identity_ca_certificate_file"))); }
                catch { throw new Failure("The runtime's server certificate is missing (" + connectionPath + ")."); }
            }

            // ShadNet packet: u8 type, u16 command, u32 total size, u64 packet id, then the payload.
            const ushort changeOnlineId = 50;
            var message = new List<byte>();
            message.AddRange(Field(1, onlineId));
            message.AddRange(Field(2, password));
            message.AddRange(Field(3, newOnlineId));
            password = "";
            var packet = new List<byte> { 0 };
            packet.AddRange(BitConverter.GetBytes(changeOnlineId));
            packet.AddRange(BitConverter.GetBytes((uint)(15 + 4 + message.Count)));
            packet.AddRange(BitConverter.GetBytes((ulong)1));
            packet.AddRange(BitConverter.GetBytes((uint)message.Count));
            packet.AddRange(message);

            using (var client = new TcpClient())
            {
                try
                {
                    var connect = client.BeginConnect(server, port, null, null);
                    if (!connect.AsyncWaitHandle.WaitOne(10000) || !client.Connected) throw new TimeoutException();
                    client.EndConnect(connect);
                }
                catch { throw new Failure("Could not reach the server at " + server + ":" + port + ". Check the Server setting and your connection."); }
                try
                {
                    Stream stream = client.GetStream();
                    stream.ReadTimeout = 15000;
                    if (ca != null)
                    {
                        var ssl = new SslStream(stream, false, (sender, certificate, chain, errors) => IssuedBy(certificate, ca));
                        ssl.ReadTimeout = 15000;
                        ssl.AuthenticateAsClient(server, null, SslProtocols.Tls12, false);
                        stream = ssl;
                    }
                    var bytes = packet.ToArray();
                    stream.Write(bytes, 0, bytes.Length);
                    while (true)
                    {
                        var header = ReadExact(stream, 15);
                        ushort command = BitConverter.ToUInt16(header, 1);
                        uint size = BitConverter.ToUInt32(header, 3);
                        var body = size > 15 ? ReadExact(stream, (int)(size - 15)) : new byte[0];
                        if (header[0] != 1 || command != changeOnlineId) continue;
                        int code = body.Length > 0 ? body[0] : 33;
                        // Reply: error byte, u32 length, protobuf with the player message as string 1.
                        string text = "";
                        if (body.Length > 6 && body[5] == 0x0A)
                        {
                            int length = 0, shift = 0, offset = 6;
                            byte value;
                            do { value = body[offset]; length |= (value & 0x7F) << shift; shift += 7; offset++; } while (value >= 0x80 && offset < body.Length);
                            text = Encoding.UTF8.GetString(body, offset, Math.Min(length, body.Length - offset));
                        }
                        if (code == 0) return text != "" ? text : "Renamed to " + newOnlineId + ".";
                        if (code == 33 && text == "") throw new Failure("This server cannot rename accounts yet. Ask the server operator.");
                        throw new Failure(text != "" ? "The server did not rename the account: " + text + "." : "The server did not rename the account (error " + code + ").");
                    }
                }
                catch (Failure) { throw; }
                catch (Exception error) { throw new Failure("The server did not answer the rename request: " + error.Message); }
            }
        }

        static byte[] Field(int number, string value)
        {
            var bytes = Encoding.UTF8.GetBytes(value);
            if (bytes.Length > 127) throw new Failure("A value is too long.");
            var field = new byte[bytes.Length + 2];
            field[0] = (byte)((number << 3) | 2);
            field[1] = (byte)bytes.Length;
            Buffer.BlockCopy(bytes, 0, field, 2, bytes.Length);
            return field;
        }

        static byte[] ReadExact(Stream stream, int count)
        {
            var buffer = new byte[count];
            int offset = 0;
            while (offset < count)
            {
                int read = stream.Read(buffer, offset, count - offset);
                if (read <= 0) throw new IOException("The server closed the connection.");
                offset += read;
            }
            return buffer;
        }

        // Accepts only a server certificate issued by the CrossCrease CA from the connection files.
        static bool IssuedBy(X509Certificate certificate, X509Certificate2 ca)
        {
            if (certificate == null) return false;
            using (var chain = new X509Chain())
            {
                chain.ChainPolicy.RevocationMode = X509RevocationMode.NoCheck;
                chain.ChainPolicy.VerificationFlags = X509VerificationFlags.AllowUnknownCertificateAuthority;
                chain.ChainPolicy.ExtraStore.Add(ca);
                if (!chain.Build(new X509Certificate2(certificate))) return false;
                var root = chain.ChainElements[chain.ChainElements.Count - 1].Certificate;
                return chain.ChainElements.Count >= 2 && root.Thumbprint == ca.Thumbprint;
            }
        }
    }
}
