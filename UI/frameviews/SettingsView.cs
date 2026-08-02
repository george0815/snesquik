//TODO: RESOURCE STRINGS
using Terminal.Gui;
using sakura.helpers;

namespace sakura.frameviews
{
    /// <summary>
    /// SettingsView renders and manages the full application settings UI.
    ///
    /// This view is intentionally self-contained and explicit:
    ///  - UI construction
    ///  - input validation
    ///  - application of settings
    ///  - persistence to disk
    ///
    /// Abstraction is deliberately kept minimal to favor debuggability
    /// and predictable behavior in a terminal UI environment.
    /// </summary>
    internal class SettingsView : FrameView
    {
        /// <summary>
        /// Human-readable color names mapped to Terminal.Gui colors.
        ///
        /// Keys come from localized resource strings and are used
        /// as button labels in the color picker UI.
        /// </summary>
        internal static Dictionary<string, Terminal.Gui.Color> colors =
            new()
            {
                { Resources.Black,          Terminal.Gui.Color.Black },
                { Resources.Blue,           Terminal.Gui.Color.Blue },
                { Resources.Green,          Terminal.Gui.Color.Green },
                { Resources.Cyan,           Terminal.Gui.Color.Cyan },
                { Resources.Red,            Terminal.Gui.Color.Red },
                { Resources.Magenta,        Terminal.Gui.Color.Magenta },
                { Resources.Brown,          Terminal.Gui.Color.Brown },
                { Resources.Gray,           Terminal.Gui.Color.Gray },
                { Resources.DarkGray,       Terminal.Gui.Color.DarkGray },
                { Resources.BrightBlue,     Terminal.Gui.Color.BrightBlue },
                { Resources.BrightGreen,    Terminal.Gui.Color.BrightGreen },
                { Resources.BrightCyan,     Terminal.Gui.Color.BrightCyan },
                { Resources.BrightRed,      Terminal.Gui.Color.BrightRed },
                { Resources.BrightMagenta,  Terminal.Gui.Color.BrightMagenta },
                { Resources.BrightYellow,   Terminal.Gui.Color.BrightYellow },
                { Resources.White,          Terminal.Gui.Color.White }
            };

        /// <summary>
        /// Initializes the settings UI and wires all controls
        /// directly to <see cref="Settings.Current"/>.
        ///
        /// Layout is built procedurally to keep ordering explicit
        /// and easy to modify without hidden layout logic.
        /// </summary>
        public SettingsView()
            : base(Resources.Settings)
        {
            X = 20;
            Y = SettingsData.HeaderHeight;
            Width = Dim.Fill();
            Height = Dim.Fill();

            // ScrollView allows all settings to remain accessible
            // even on small or resized terminal windows.
            var scroll = new ScrollView()
            {
                X = 0,
                Y = 0,
                Width = Dim.Fill(),
                Height = Dim.Fill(),
                ShowVerticalScrollIndicator = true,
                ShowHorizontalScrollIndicator = false,
            };

            Add(scroll);

            // Tracks vertical layout position inside the scroll view.
            // This avoids implicit layout rules and keeps spacing obvious.
            int y = 1;


            #region BOOLEAN OPTIONS


            // Disable colored hotkey hints in the UI
            var disableHotKeyColors = new CheckBox(Resources.Disablecoloredhotkeyinformation)
            {
                X = 1,
                Y = y,
                Checked = Settings.Current.DisableColoredHotkeyInfo
            };
            scroll.Add(disableHotKeyColors);
            y += 2;

            // Enable verbose logging for debugging and diagnostics
            var detailedLogging = new CheckBox(Resources.Enabledetailedlogging)
            {
                X = 1,
                Y = y,
                Checked = Settings.Current.DetailedLogging
            };
            scroll.Add(detailedLogging);
            y += 2;


            // Hide the terminal text cursor
            var hideTextCursor = new CheckBox(Resources.Hidetextcursor)
            {
                X = 1,
                Y = y,
                Checked = Settings.Current.HidetextCursor
            };
            scroll.Add(hideTextCursor);
            y += 2;

            // Use the system console
            var useSystemConsole = new CheckBox(Resources.Usesystemconsole)
            {
                X = 1,
                Y = y,
                Checked = Settings.Current.UseSystemConsole
            };
            scroll.Add(useSystemConsole);
            y += 2;
            #endregion


            #region PATHS

            scroll.Add(new Label(Resources.Rompaths) { X = 1, Y = y });
            var romDirs = new TextView()
            {
                X = 30,
                Y = y,
                Width = 40,
                Height = 5,
                Text = String.Join("\n", Settings.Current.AllRomPaths)
            };
            scroll.Add(romDirs);
            y += 6;


            // Default rom directory
            scroll.Add(new Label(Resources.Defaultrompath) { X = 1, Y = y });
            var romPathField =
                new TextField(Settings.Current.DefaultRomPath ?? "")
                {
                    X = 30,
                    Y = y,
                    Width = 40
                };
            scroll.Add(romPathField);
            var romFolderDialogBtn = new Button("...") { X = 71, Y = y };
            scroll.Add(romFolderDialogBtn);
            y += 2;

            // Log file location
            scroll.Add(new Label(Resources.LogPath) { X = 1, Y = y });
            var logPathField =
                new TextField(Settings.Current.LogPath ?? "")
                {
                    X = 30,
                    Y = y,
                    Width = 40
                };
            scroll.Add(logPathField);
            var logFileDialogBtn = new Button("...") { X = 71, Y = y };
            scroll.Add(logFileDialogBtn);
            y += 2;

            // SRAM saves location
            scroll.Add(new Label(Resources.Srampath) { X = 1, Y = y });
            var sramFolderPathField =
                new TextField(Settings.Current.SramPath ?? "")
                {
                    X = 30,
                    Y = y,
                    Width = 40
                };
            scroll.Add(sramFolderPathField);
            var sramFolderDialogBtn = new Button("...") { X = 71, Y = y };
            scroll.Add(sramFolderDialogBtn);
            y += 2;

            // Save state location
            scroll.Add(new Label(Resources.Savestatepath) { X = 1, Y = y });
            var stateFolderPathField =
                new TextField(Settings.Current.StatePath ?? "")
                {
                    X = 30,
                    Y = y,
                    Width = 40
                };
            scroll.Add(stateFolderPathField);
            var stateFolderDialogBtn = new Button("...") { X = 71, Y = y };
            scroll.Add(stateFolderDialogBtn);
            y += 3;

            #endregion

            #region COLOR SETTINGS

            // Background color
            scroll.Add(new Label(Resources.Backgroundcolor) { X = 1, Y = y });
            var myKey =
                colors.FirstOrDefault(x => x.Value == Settings.Current.BackgroundColor).Key;
            var bgColorCombo = new Button()
            {
                X = 30,
                Y = y,
                Width = 3,
                Height = 1,
                Text = myKey
            };

            bgColorCombo.Clicked += () =>
            {
                // Pick color from grid dialog
                string tmp = DialogHelpers.PickColorGrid();
                Settings.Current.BackgroundColor = colors[tmp];

                // Update button label to reflect selection
                bgColorCombo.Text = tmp;

                // Force redraw
                bgColorCombo.SetNeedsDisplay();
                scroll.SetNeedsDisplay();
            };

            scroll.Add(bgColorCombo);
            y += 2;

            // Text color
            scroll.Add(new Label(Resources.Textcolor) { X = 1, Y = y });
            myKey = colors.FirstOrDefault(x => x.Value == Settings.Current.TextColor).Key;
            var textColorCombo = new Button()
            {
                X = 30,
                Y = y,
                Width = 3,
                Height = 1,
                Text = myKey
            };

            textColorCombo.Clicked += () =>
            {
                string tmp = DialogHelpers.PickColorGrid();
                Settings.Current.TextColor = colors[tmp];
                textColorCombo.Text = tmp;
                textColorCombo.SetNeedsDisplay();
                scroll.SetNeedsDisplay();
            };

            scroll.Add(textColorCombo);
            y += 2;

            // Focus background color
            scroll.Add(new Label(Resources.Focusbackgroundcolor) { X = 1, Y = y });
            myKey =
                colors.FirstOrDefault(
                    x => x.Value == Settings.Current.FocusBackgroundColor).Key;
            var backgroundFocusColorCombo = new Button()
            {
                X = 30,
                Y = y,
                Width = 3,
                Height = 1,
                Text = myKey
            };

            backgroundFocusColorCombo.Clicked += () =>
            {
                string tmp = DialogHelpers.PickColorGrid();
                Settings.Current.FocusBackgroundColor = colors[tmp];
                backgroundFocusColorCombo.Text = tmp;
                backgroundFocusColorCombo.SetNeedsDisplay();
                scroll.SetNeedsDisplay();
            };

            scroll.Add(backgroundFocusColorCombo);
            y += 2;

            // Focus text color
            scroll.Add(new Label(Resources.Focustextcolor) { X = 1, Y = y });
            myKey =
                colors.FirstOrDefault(
                    x => x.Value == Settings.Current.FocusTextColor).Key;
            var textFocusColorCombo = new Button()
            {
                X = 30,
                Y = y,
                Width = 3,
                Height = 1,
                Text = myKey
            };

            textFocusColorCombo.Clicked += () =>
            {
                string tmp = DialogHelpers.PickColorGrid();
                Settings.Current.FocusTextColor = colors[tmp];
                textFocusColorCombo.Text = tmp;
                textFocusColorCombo.SetNeedsDisplay();
                scroll.SetNeedsDisplay();
            };

            scroll.Add(textFocusColorCombo);
            y += 2;

            // Hotkey / accent color
            scroll.Add(new Label(Resources.Hotkeytextcolor) { X = 1, Y = y });
            myKey =
                colors.FirstOrDefault(
                    x => x.Value == Settings.Current.HotTextColor).Key;
            var hotTextColorCombo = new Button()
            {
                X = 30,
                Y = y,
                Width = 3,
                Height = 1,
                Text = myKey
            };

            hotTextColorCombo.Clicked += () =>
            {
                string tmp = DialogHelpers.PickColorGrid();
                Settings.Current.HotTextColor = colors[tmp];
                hotTextColorCombo.Text = tmp;
                hotTextColorCombo.SetNeedsDisplay();
                scroll.SetNeedsDisplay();
            };

            scroll.Add(hotTextColorCombo);
            y += 2;

            // ASCII logo color
            scroll.Add(new Label(Resources.ASCIIcolor) { X = 1, Y = y });
            myKey =
                colors.FirstOrDefault(
                    x => x.Value == Settings.Current.LogoColor).Key;
            var logoColorCombo = new Button()
            {
                X = 30,
                Y = y,
                Width = 3,
                Height = 1,
                Text = myKey
            };

            logoColorCombo.Clicked += () =>
            {
                string tmp = DialogHelpers.PickColorGrid();
                Settings.Current.LogoColor = colors[tmp];
                logoColorCombo.Text = tmp;
                logoColorCombo.SetNeedsDisplay();
                scroll.SetNeedsDisplay();
            };

            scroll.Add(logoColorCombo);
            y += 2;

            // Toggle ASCII logo entirely
            var disableASCII =
                new CheckBox(Resources.DisableASCII)
                {
                    X = 1,
                    Y = y,
                    Checked = Settings.Current.DisableASCII
                };
            scroll.Add(disableASCII);
            y += 2;

            #endregion

            // Persist settings to disk
            var saveBtn = new Button(Resources.Save) { X = 1, Y = y };
            scroll.Add(saveBtn);

            // Required so scroll bars calculate correctly
            scroll.ContentSize =
                new Terminal.Gui.Size(Application.Top.Frame.Width - 2, y + 2);

            #region EVENT HANDLERS

            logFileDialogBtn.Clicked += () =>
            {
                string? path =
                    DialogHelpers.ShowFolderDialog(
                        Resources.Selectlogfilepath,

                        Resources.Selectfolderforlog);

                if (!string.IsNullOrWhiteSpace(path))
                {
                    logPathField.Text = path;
                }
            };

            sramFolderDialogBtn.Clicked += () =>
            {
                string? path =
                    DialogHelpers.ShowFolderDialog(
                        Resources.Selectsramfolder,
                        Resources.Selectfolderforsram);

                if (!string.IsNullOrWhiteSpace(path))
                {
                    sramFolderPathField.Text = path;
                }
            };

            stateFolderDialogBtn.Clicked += () =>
            {
                string? path =
                    DialogHelpers.ShowFolderDialog(
                        Resources.Selectsavestatefolder,
                        Resources.Selectfolderforsavestatefiles);

                if (!string.IsNullOrWhiteSpace(path))
                {
                    stateFolderPathField.Text = path;
                }
            };

            romFolderDialogBtn.Clicked += () =>
            {
                string? path =
                    DialogHelpers.ShowFolderDialog(
                        Resources.Selectdefaultromfolder,
                        Resources.Selectthefolderthatsakura);

                if (!string.IsNullOrWhiteSpace(path))
                {
                    // Extract directory from returned path
                    string dir = Path.GetDirectoryName(path) ?? path;
                    romPathField.Text = path;
                }
            };

            saveBtn.Clicked += () =>
            {
                try
                {


                    // --- path validation ---

                    List<string> romDirsList = [];
                    string romDirString = romDirs.Text.ToString()!.Trim();

                    if (!String.IsNullOrWhiteSpace(romDirString))
                    {

                        foreach (var line in romDirString.Split("\n", StringSplitOptions.RemoveEmptyEntries))
                        {
                            string trimmed = line.Trim();
                            romDirsList.Add(trimmed);
                        }


                    }

                    string romPath = romPathField.Text.ToString()!.Trim();
                    string logPath = logPathField.Text.ToString()!.Trim();
                    string sramPath = sramFolderPathField.Text.ToString()!.Trim();
                    string statePath = stateFolderPathField.Text.ToString()!.Trim();

                    if (string.IsNullOrWhiteSpace(romPath))
                    {
                        MessageBox.ErrorQuery(Resources.Error, Resources.Rompathcannotbeempty, Resources.OK);
                        return;
                    }

                    if (!Directory.Exists(romPath))
                    {
                        if (MessageBox.Query(
                            Resources.MissingDirectory,
                            Resources.Rompathdoesnotexist,
                            Resources.Yes, Resources.No) == 0)
                        {
                            Directory.CreateDirectory(romPath);
                        }
                        else return;
                    }

                    if (string.IsNullOrWhiteSpace(logPath))
                    {
                        MessageBox.ErrorQuery(Resources.Error, Resources.Logpathcannotbeempty, Resources.OK);
                        return;
                    }

                    if (string.IsNullOrWhiteSpace(sramPath))
                    {
                        MessageBox.ErrorQuery(Resources.Error, Resources.Srampathcannotbeempty, Resources.OK);
                        return;
                    }

                    if (string.IsNullOrWhiteSpace(statePath))
                    {
                        MessageBox.ErrorQuery(Resources.Error, Resources.Savestatepathcannotbeempty, Resources.OK);
                        return;
                    }
                    // --- apply settings ---


                    Settings.Current.DetailedLogging = detailedLogging.Checked;
                    Settings.Current.DisableASCII = disableASCII.Checked;
                    Settings.Current.DisableColoredHotkeyInfo = disableHotKeyColors.Checked;
                    Settings.Current.HidetextCursor = hideTextCursor.Checked;
                    Settings.Current.UseSystemConsole = useSystemConsole.Checked;

                    Settings.Current.DefaultRomPath = romPath;
                    Settings.Current.AllRomPaths = romDirsList;
                    Settings.Current.LogPath = logPath;
                    Settings.Current.SramPath = sramPath;
                    Settings.Current.StatePath = statePath;

                    Settings.Current.BackgroundColor = colors[bgColorCombo.Text.ToString()!];
                    Settings.Current.TextColor = colors[textColorCombo.Text.ToString()!];
                    Settings.Current.FocusBackgroundColor = colors[backgroundFocusColorCombo.Text.ToString()!];
                    Settings.Current.FocusTextColor = colors[textFocusColorCombo.Text.ToString()!];
                    Settings.Current.HotTextColor = colors[hotTextColorCombo.Text.ToString()!];
                    Settings.Current.LogoColor = colors[logoColorCombo.Text.ToString()!];

                    Settings.Save();
                    Rom.GetAllRoms();

                    MessageBox.Query(Resources.Settings, Resources.Settingssavedsuccessfully, Resources.OK);
                    Log.Write(Resources.Settingssaved);
                }
                catch (Exception ex)
                {
                    MessageBox.ErrorQuery(Resources.FatalError, ex.Message, Resources.OK);
                }
            };

            #endregion
        }
    }
}
