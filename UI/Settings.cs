using System.Text.Json;
using sakura.helpers;
using Terminal.Gui;

namespace sakura
{

    /// <summary>
    /// Holds all configurable settings and UI variables for the application.
    /// </summary>
    internal class SettingsData
    {
        // ------------------------------
        // UI Variables
        // ------------------------------
        public static ushort HeaderHeight { get; set; } = 15;  // Default header height
        public static ushort LogoWidth { get; set; } = 55;     // Width of ASCII logo display


        // ------------------------------
        // Client behavior flags
        // ------------------------------
        public bool DetailedLogging { get; set; } = true;
        public bool HidetextCursor { get; set; } = true;
        public bool DisableColoredHotkeyInfo { get; set; } = false;
        public bool DisableASCII { get; set; } = false;

        // ------------------------------
        // Paths
        // ------------------------------
        public string? DefaultRomPath { get; set; } = "./";  // Default download folder
        public string? SramPath { get; set; } = "./saves/sram/";  // Default sram folder
        public string? StatePath { get; set; } = "./saves/state/";  // Default save states folder
        public List<string> AllRomPaths { get; set; } = new List<string>() { "./" };
        public string? LogPath { get; set; } = "./";       // Log file path
        public string SettingsPath { get; set; } = "./cfg.json";    // Settings file

        // ------------------------------
        // UI Terminal.Gui.Colors
        // ------------------------------
        public Terminal.Gui.Color BackgroundColor { get; set; } = Terminal.Gui.Color.Black;
        public Terminal.Gui.Color TextColor { get; set; } = Terminal.Gui.Color.White;
        public Terminal.Gui.Color FocusBackgroundColor { get; set; } = Terminal.Gui.Color.White;
        public Terminal.Gui.Color FocusTextColor { get; set; } = Terminal.Gui.Color.Black;
        public Terminal.Gui.Color HotTextColor { get; set; } = Terminal.Gui.Color.BrightYellow;
        public Terminal.Gui.Color LogoColor { get; set; } = Terminal.Gui.Color.BrightCyan;


        // ------------------------------
        // UseSystemConsole 
        // ------------------------------

        public bool UseSystemConsole { get; set; } = false;


        // ------------------------------
        // Hotkey controls
        // ------------------------------
        public RomHotkeys Controls { get; set; } = new RomHotkeys
        {
            StartRom = Key.F3,
            StopRom = Key.F4,
            OpenRomPath = Key.F5,
            OpenSramPath = Key.F6,
            SaveState = Key.F7,
            LoadState = Key.F8,
            START = Key.a,
            SELECT = Key.s,
            A = Key.z,
            B = Key.x,
            UP = Key.CursorUp,
            DOWN = Key.CursorDown,
            LEFT = Key.CursorLeft,
            RIGHT = Key.CursorRight

        };

        // ------------------------------
        // ASCII / Icons
        // ------------------------------
        public List<string> Icons { get; set; } = ASCII.icons;

    }


    /// <summary>
    /// Provides global access to the current settings instance, 
    /// and handles saving/loading from JSON.
    /// </summary>
    internal static class Settings
    {
        /// <summary>
        /// The currently loaded settings.
        /// </summary>
        internal static SettingsData Current { get; private set; } = new();

        internal static event EventHandler? SettingsUpdated;

        /// <summary>
        /// Options for JSON serialization.
        /// </summary>
        private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };


        #region SAVE/LOAD

        /// <summary>
        /// Saves current settings to disk in JSON format.
        /// </summary>
        internal static void Save()
        {
            try
            {
                var json = JsonSerializer.Serialize(Current, JsonOptions);
                File.WriteAllText(Current.SettingsPath ?? "cfg.json", json);
                Rom.GetAllRoms();
                SettingsUpdated?.Invoke(null, EventArgs.Empty);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"{Resources.ErrorsavingsettingsexMessage} {ex.Message}");
            }
        }

        /// <summary>
        /// Loads settings from disk or creates default if missing or corrupted.
        /// </summary>
        internal static void Load()
        {
            try
            {
                if (!File.Exists(Current.SettingsPath))
                {
                    Settings.Save();
                    return;
                }

                string json = File.ReadAllText(Current.SettingsPath);
                var loaded = JsonSerializer.Deserialize<SettingsData>(json, JsonOptions);

                if (loaded != null)
                {
                    Current = loaded;

                    // Reduce header height if ASCII art disabled
                    if (Current.DisableASCII)
                        SettingsData.HeaderHeight = 5;
                }
                else
                {
                    Settings.Save();
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"{Resources.ErrorloadingsettingsexMessage} {ex.Message}");
                Settings.Save();
            }
        }

        #endregion
    }
}
