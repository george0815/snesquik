using System.Text;

namespace sakura.helpers
{
    /// <summary>
    /// Simple logging helper that stores logs in memory and optionally writes them to disk.
    /// Fires an event whenever a new log entry is added.
    /// </summary>
    internal class Log
    {
        /// <summary>
        /// Event fired whenever a new log entry is added.
        /// Can be used by the UI to update log views in real-time.
        /// </summary>
        public static event Action? OnLogAdded;

        /// <summary>
        /// Internal in-memory storage for log entries.
        /// </summary>
        static internal List<string> LogList { get; set; } = [];


        /// <summary>
        /// Used for only printing new log entries in CLI/headless mode.
        /// </summary>
        static internal string _lastPrintedLog = "";


        /// <summary>
        /// Adds a new log entry with timestamp.
        /// </summary>
        /// <param name="msg">The message to log.</param>
        static internal void Write(string msg)
        {
            // Append timestamp to message and store in log
            LogList.Add($"[{DateTime.Now}] {msg}");

            // Notify any listeners that a new log entry was added
            OnLogAdded?.Invoke();
        }

        /// <summary>
        /// Saves all log entries to persistent storage.
        /// </summary>
        static internal void Save()
        {
            // Open file for writing, overwrite existing content
            using StreamWriter writer = new("log.txt", false, Encoding.UTF8);

            // Join all log entries with newline and write to file
            writer.Write(string.Join("\n", LogList));
        }
    }
}
