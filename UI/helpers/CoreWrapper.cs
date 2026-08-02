//TODO: RESOURCE STRINGS
using System.Diagnostics;
using System.Text;
using System.Text.Json;

namespace sakura.helpers
{


    /// <summary>
    /// Struct to hold arguments for launching a rom
    /// </summary>
    internal struct SearchArgs
    {
        internal string RomPath;       // Search query
    }

    /// <summary>
    /// Handles interaction with the sakura core executable for searches and plugin checks.
    /// </summary>
    internal class CoreWrapper
    {
        // Determine executable name based on OS
        internal static readonly string ExeFileName = OperatingSystem.IsWindows() ? "snesquik_core.exe" : "snesquik_core";


        // JsonSerializerOptions instance
        private static readonly JsonSerializerOptions CachedJsonOptions = new() { PropertyNameCaseInsensitive = true };



        /// <summary>
        /// Launches the sakura emulator core with the given arguements
        /// </summary>
        internal static string Launch(SearchArgs args)
        {
            // Convert arrays to space-separated strings for CLI

            var psi = new ProcessStartInfo
            {
                FileName = ExeFileName,
                Arguments =
                    $"\"{args.RomPath}\"",
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = false,
                StandardOutputEncoding = Encoding.UTF8,
                StandardErrorEncoding = Encoding.UTF8
            };

            using var process = new Process { StartInfo = psi };
            process.Start();

            // Asynchronously read stdout and stderr
            var outputTask = process.StandardOutput.ReadToEndAsync();
            var errorTask = process.StandardError.ReadToEndAsync();


            Task.WaitAll(outputTask, errorTask);

            string output = outputTask.Result;
            string error = errorTask.Result;

            if (process.ExitCode != 0)
            {
                Log.Write($"{Resources.Errorinitializingthesakuracore} {error}");
                return $"{{\"data\":[],\"errors\":[\"{Resources.Errorinitializingthesakuracore}{error}\"]}}";
            }

            return output; // Return JSON search result
        }






    }
}



