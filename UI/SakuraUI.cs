//TODO: RESOURCE STRINGS, HOTKEY INFO, AND
using sakura.frameviews;
using sakura.helpers;
using Terminal.Gui;

namespace sakura
{
    /// <summary>
    /// Main UI window for sakura application using Terminal.Gui.
    /// Contains header, sidebar menu, and various content panels.
    /// </summary>
    public class SakuraUI : Window
    {
        // ------------------------------
        // Content views
        // ------------------------------
        readonly FrameView romListView;
        readonly FrameView savesView;
        readonly FrameView settingsView;
        readonly FrameView controlsView;
        internal FrameView logView;

        // Header components
        readonly private FrameView header;
        readonly private Label romCount;
        readonly private Label startLabel;
        readonly private Label stopLabel;
        readonly private Label openRomPathLabel;
        readonly private Label openSramPathLabel;
        readonly private Label saveLabel;
        readonly private Label loadLabel;

        public SakuraUI()
        {

            //Get rom list
            Rom.GetAllRoms();


            #region HEADER

            // Header container
            header = new FrameView()
            {
                X = 0,
                Y = 0,
                Width = Dim.Fill(),
                Height = Settings.Current.DisableASCII ? 5 : SettingsData.HeaderHeight,
                Border = new Border() { BorderStyle = BorderStyle.None }
            };

            // Scrollable area inside header
            var headerScroll = new ScrollView()
            {
                X = 0,
                Y = 0,
                Width = Dim.Fill(),
                Height = Dim.Fill(),
                ContentSize = new Size(120, SettingsData.HeaderHeight),
                ShowHorizontalScrollIndicator = false,
                ShowVerticalScrollIndicator = false,
                Border = new Border() { BorderStyle = BorderStyle.None }
            };

            header.Add(headerScroll);

            // ASCII logo frame
            var logoFrame = new FrameView()
            {
                X = 0,
                Y = 0,
                Width = Settings.Current.DisableASCII ? 0 : SettingsData.LogoWidth,
                Height = SettingsData.HeaderHeight,
                Border = new Border() { BorderStyle = BorderStyle.Single }
            };

            var logo = new Label()
            {
                X = 0,
                Y = 0,
                Width = Dim.Fill(),
                Height = Dim.Fill(),
                Text = Settings.Current.Icons[0],
                ColorScheme = new ColorScheme()
                {
                    Normal = Application.Driver.MakeAttribute(Settings.Current.LogoColor, Settings.Current.BackgroundColor),
                    Focus = Application.Driver.MakeAttribute(Settings.Current.LogoColor, Settings.Current.BackgroundColor)
                }
            };

            // Add logo only if ASCII art is enabled
            if (!Settings.Current.DisableASCII)
            {
                logoFrame.Add(logo);
                header.Add(logoFrame);
            }

            #region EXTRA HEADER INFO

            // Display current date
            var date = new Label()
            {
                X = (Settings.Current.DisableASCII ? 0 : SettingsData.LogoWidth) + 2,
                Y = 1,
                Text = DateTime.Now.ToString("yyyy-MM-dd")
            };

            // Display number of roms
            romCount = new Label()
            {
                X = (Settings.Current.DisableASCII ? 30 : SettingsData.LogoWidth) + 2,
                Y = (Settings.Current.DisableASCII ? 1 : 3),
                Text = $"{Resources.Roms}: {Rom.Roms?.Count}"
            };

            Settings.SettingsUpdated += RefreshHeader;

            // Add info labels to header scroll
            headerScroll.Add(date, romCount);

            // Add header to main window
            Add(header);

            #endregion


            #region HOTKEY INFO

            // Add hotkey instructions with colored text
            startLabel = new Label($"{Resources.Startrom}{Settings.Current.Controls.StartRom}")
            {
                ColorScheme = (Settings.Current.DisableColoredHotkeyInfo ? this.SuperView?.ColorScheme : new ColorScheme()
                {
                    Normal = Application.Driver.MakeAttribute(Color.Green, Settings.Current.BackgroundColor)
                }),
                X = (Settings.Current.DisableASCII ? 30 : SettingsData.LogoWidth) + 30,
                Y = 1
            };

            headerScroll.Add(startLabel);

            stopLabel = new Label($"{Resources.Stoprom}{Settings.Current.Controls.StopRom}")
            {
                ColorScheme = (Settings.Current.DisableColoredHotkeyInfo ? this.SuperView?.ColorScheme : new ColorScheme()
                {
                    Normal = Application.Driver.MakeAttribute(Color.Blue, Settings.Current.BackgroundColor)
                }),
                X = (Settings.Current.DisableASCII ? 30 : SettingsData.LogoWidth) + 30,
                Y = 3
            };

            headerScroll.Add(stopLabel);



            openRomPathLabel = new Label($"{Resources.Openromfolder}{Settings.Current.Controls.OpenRomPath}")
            {
                ColorScheme = (Settings.Current.DisableColoredHotkeyInfo ? this.SuperView?.ColorScheme : new ColorScheme()
                {
                    Normal = Application.Driver.MakeAttribute(Color.Cyan, Settings.Current.BackgroundColor)
                }),
                X = (Settings.Current.DisableASCII ? 42 : SettingsData.LogoWidth) + 30,
                Y = (Settings.Current.DisableASCII ? 1 : 5)
            };
            headerScroll.Add(openRomPathLabel);

            openSramPathLabel = new Label($"{Resources.Opensramfolder}{Settings.Current.Controls.OpenSramPath}")
            {
                ColorScheme = (Settings.Current.DisableColoredHotkeyInfo ? this.SuperView?.ColorScheme : new ColorScheme()
                {
                    Normal = Application.Driver.MakeAttribute(Color.Magenta, Settings.Current.BackgroundColor)
                }),
                X = (Settings.Current.DisableASCII ? 42 : SettingsData.LogoWidth) + 30,
                Y = (Settings.Current.DisableASCII ? 3 : 7)

            };
            headerScroll.Add(openSramPathLabel);

            saveLabel = new Label($"{Resources.Savestate}{Settings.Current.Controls.SaveState}")
            {
                ColorScheme = (Settings.Current.DisableColoredHotkeyInfo ? this.SuperView?.ColorScheme : new ColorScheme()
                {
                    Normal = Application.Driver.MakeAttribute(Color.Red, Settings.Current.BackgroundColor)
                }),
                X = (Settings.Current.DisableASCII ? 62 : SettingsData.LogoWidth) + 30,
                Y = (Settings.Current.DisableASCII ? 1 : 9)

            };
            headerScroll.Add(saveLabel);

            loadLabel = new Label($"{Resources.Loadstate}{Settings.Current.Controls.LoadState}")
            {
                ColorScheme = (Settings.Current.DisableColoredHotkeyInfo ? this.SuperView?.ColorScheme : new ColorScheme()
                {
                    Normal = Application.Driver.MakeAttribute(Color.BrightCyan, Settings.Current.BackgroundColor)
                }),
                X = (Settings.Current.DisableASCII ? 62 : SettingsData.LogoWidth) + 30,
                Y = (Settings.Current.DisableASCII ? 3 : 11)

            };
            headerScroll.Add(loadLabel);

            #endregion

            #endregion

            #region SIDEBAR AND MENU

            // Sidebar container
            var sidebar = new FrameView()
            {
                X = 0,
                Y = SettingsData.HeaderHeight,
                Width = 20,
                Height = Dim.Fill(),
                Border = new Border() { BorderStyle = BorderStyle.Rounded }
            };

            // Sidebar menu
            var menu = new ListView(new string[]
        {
                Resources.Roms,
                Resources.Saves,
                Resources.Settings,
                Resources.Controls,
                Resources.Log,
        })
            {
                X = 1,
                Y = 0,
                Width = Dim.Fill(),
                Height = Dim.Fill() - 3
            };

            // Switch content panel when menu selection changes
            menu.SelectedItemChanged += (args) =>
            {
                SwitchPanel(args.Item, ref logo);
            };

            sidebar.Add(menu);

            // Exit button
            var exitButton = new Button(Resources.Exit)
            {
                X = 1,
                Y = Pos.Bottom(menu),
                Width = 16
            };

            exitButton.Clicked += () => ShowExitDialog();

            sidebar.Add(exitButton);

            // Add sidebar to window
            Add(sidebar);

            #endregion


            #region CONTENT VIEWS

            romListView = new RomListView(Rom.Roms!);
            savesView = new SavesView();
            settingsView = new SettingsView();
            controlsView = new ControlsView();
            logView = new LogView(Log.LogList);

            // Show default panel
            Add(romListView);

            #endregion
        }

        #region HELPER METHODS


        /// <summary>
        /// Shows exit confirmation dialog and handles application stop.
        /// </summary>
        private static void ShowExitDialog()
        {
            var dialog = new Dialog(Resources.Exit, 50, 10) { Height = 3 };
            var ok = new Button(Resources.OK);
            var cancel = new Button(Resources.Cancel);

            bool exitRequested = false;

            ok.Clicked += () =>
            {
                exitRequested = true;
                Application.RequestStop(dialog);
            };

            cancel.Clicked += () =>
            {
                Application.RequestStop(dialog);
            };

            dialog.AddButton(ok);
            dialog.AddButton(cancel);

            // Run dialog modally
            Application.Run(dialog);

            // If confirmed, close main UI
            if (exitRequested)
            {
                Application.Top.RemoveAll();
                Application.RequestStop();

            }
        }

        /// <summary>
        /// Switches visible content panel based on menu index.
        /// </summary>
        private void SwitchPanel(int index, ref Label logo)
        {
            // Remove all panels first
            Remove(romListView);
            Remove(savesView);
            Remove(settingsView);
            Remove(controlsView);
            Remove(logView);

            // Update ASCII logo
            logo.Text = ASCII.icons[index];

            switch (index)
            {
                case 0: Add(romListView); break;
                case 1: Add(savesView); break;
                case 2: Add(settingsView); break;
                case 3: Add(controlsView); break;
                case 4: Add(logView); break;
            }

            SetNeedsDisplay();
        }

        /// <summary>
        /// Controls cursor visibility based on settings.
        /// </summary>
        public override void PositionCursor()
        {
            if (Settings.Current.HidetextCursor)
            {
                Application.Driver.SetCursorVisibility(CursorVisibility.Invisible);
                return;
            }

            base.PositionCursor();
        }


        /// <summary>
        /// Refreshes header when controls or rom count changes.
        /// </summary>
        public void RefreshHeader(object? sender, EventArgs e)
        {

            romCount.Text = $"{Resources.Roms}: {Rom.Roms?.Count}";
            startLabel.Text = $"{Resources.Startrom}{Settings.Current.Controls.StartRom}";
            stopLabel.Text = $"{Resources.Stoprom}{Settings.Current.Controls.StopRom}";
            openRomPathLabel.Text = $"{Resources.Openromfolder}{Settings.Current.Controls.OpenRomPath}";
            openSramPathLabel.Text = $"{Resources.Opensramfolder}{Settings.Current.Controls.OpenSramPath}";
            saveLabel.Text = $"{Resources.Savestate}{Settings.Current.Controls.SaveState}";
            loadLabel.Text = $"{Resources.Loadstate}{Settings.Current.Controls.LoadState}";

            SetNeedsDisplay();
        }

        #endregion
    }
}
