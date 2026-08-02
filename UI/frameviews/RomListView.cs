//TODO: RESOURCE STRINGS AND ROM KEY HANDLERS
using System.Data;
using System.Diagnostics;
using System.Runtime.InteropServices;
using Terminal.Gui;
using sakura.helpers;

namespace sakura.frameviews
{

    /// <summary>
    /// Displays a list of roms with name, SRAM path, path, and display type
    /// 
    /// This view is backed by a TableView and synchronizes with TorrentOperations events.
    /// </summary>
    public class RomListView : FrameView
    {
        /// <summary>
        /// List of roms backing this view.
        /// </summary>
        private List<Rom> _roms;

        /// <summary>
        /// Terminal.Gui table showing rom info.
        /// </summary>
        private readonly TableView _table;

        /// <summary>
        /// DataTable holding the rom rows displayed in _table.
        /// </summary>
        private readonly DataTable _tableData;

        /// <summary>
        /// Initialize the rom list view with the provided roms.
        /// </summary>
        /// <param name="roms">List of rom objects</param>
        public RomListView(List<Rom>? roms)
            : base(Resources.Roms)
        {
            _roms = roms!;

            X = 20;
            Y = SettingsData.HeaderHeight;
            Width = Dim.Fill();
            Height = Dim.Fill();

            // --- Table Schema ---
            _tableData = new DataTable();

            // Define visible columns in a consistent order
            _tableData.Columns.Add(Resources.Name, typeof(string));
            _tableData.Columns.Add(Resources.Path, typeof(string));
            _tableData.Columns.Add(Resources.Sramdatapath, typeof(string));

            // --- TableView Setup ---
            _table = new TableView()
            {
                X = 0,
                Y = 0,
                Width = Dim.Fill(),
                Height = Dim.Fill(),
                Table = _tableData
            };

            Add(_table);

            foreach (Rom rom in _roms)
            {
                _tableData.Rows.Add(rom.Name, rom.RomPath, rom.SramPath);
            }

            _table.Update();
            _table.SetNeedsDisplay();

            Settings.SettingsUpdated += RefreshRomList;

        }

        /// <summary>
        /// Handles keyboard input for control.
        /// Supports start/stop, open path, and open SRAM path
        /// </summary>
        /// <param name="keyEvent">The key event pressed by the user</param>
        /// <returns>True if the key was handled, otherwise false</returns>
        public override bool ProcessKey(KeyEvent keyEvent)
        {
            // Safety: ignore if no row is selected
            if (_table.SelectedRow < 0 || _table.SelectedRow >= _roms.Count)
                return base.ProcessKey(keyEvent);

            // --- Start rom ---
            if (keyEvent.Key == Settings.Current.Controls.StartRom)
            {
                Task.Run(async () =>
                {
                    SearchArgs args = new SearchArgs()
                    {
                        RomPath = _roms[_table.SelectedRow].RomPath!,
                    };
                    CoreWrapper.Launch(args);
                    Log.Write($"{Resources.Romstarted}{_roms[_table.SelectedRow].RomPath!}");
                });

                return true;
            }


            // --- Open rom folder ---
            else if (keyEvent.Key == Settings.Current.Controls.OpenRomPath)
            {
                try
                {
                    OpenExplorer(Directory.GetParent(_roms[_table.SelectedRow].RomPath!)!.ToString());
                }
                catch (Exception) { }
                return true;
            }

            // --- Open SRAM folder ---
            else if (keyEvent.Key == Settings.Current.Controls.OpenSramPath)
            {
                OpenExplorer(Settings.Current.SramPath!);
                return true;
            }

            return base.ProcessKey(keyEvent);
        }

        /// <summary> 
        /// Opens path in file explorer
        /// </summary> 
        void OpenExplorer(string path)
        {
            if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
            {

                var escapedPath = path.Replace("'", "'\\''");

                Process.Start(
                    new ProcessStartInfo
                    {
                        FileName = "/bin/sh",
                        Arguments = $"-c \"setsid xdg-open '{escapedPath}' >/dev/null 2>&1 </dev/null &\"",
                        UseShellExecute = false,
                        CreateNoWindow = true
                    });
            }
            else if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            {
                Process.Start(
                    new ProcessStartInfo
                    {
                        FileName = "explorer.exe",
                        Arguments = $"\"{path}\"",
                        UseShellExecute = true,
                    });
            }
        }


        /// <summary>
        /// Refreshes rom list when settings are saved
        /// </summary>
        void RefreshRomList(object? sender, EventArgs e)
        {

            _tableData.Clear();

            _roms = Rom.Roms!;

            foreach (Rom rom in _roms!)
            {
                _tableData.Rows.Add(rom.Name, rom.RomPath, rom.SramPath);
            }

            _table.Update();
            _table.SetNeedsDisplay();
        }

    }
}
