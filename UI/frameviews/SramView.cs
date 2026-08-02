//TODO: RESOURCE STRINGS AND ROM KEY HANDLERS
using System.Data;
using Terminal.Gui;

namespace sakura.frameviews
{

    /// <summary>
    /// Displays a list of sram files with name, date, and size.
    /// 
    /// This view is backed by a TableView and synchronizes with TorrentOperations events.
    /// </summary>
    public class SramView : FrameView
    {
        /// <summary>
        /// List of files backing this view.
        /// </summary>
        private List<FileInfo> _stateFiles;

        /// <summary>
        /// Terminal.Gui table showing file info.
        /// </summary>
        private readonly TableView _table;

        /// <summary>
        /// DataTable holding the rows displayed in _table.
        /// </summary>
        private readonly DataTable _tableData;

        /// <summary>
        /// Initialize the sram file list view with the provided roms.
        /// </summary>
        /// <param name="files">List of fileinfo objects</param>
        public SramView(List<FileInfo>? files)
            : base(Resources.SRAMFiles)
        {
            _stateFiles = files!;

            X = 20;
            Y = 0;
            Width = Dim.Fill();
            Height = Dim.Fill();

            // --- Table Schema ---
            _tableData = new DataTable();

            // Define visible columns in a consistent order
            _tableData.Columns.Add(Resources.Name, typeof(string));
            _tableData.Columns.Add(Resources.Created, typeof(string));
            _tableData.Columns.Add(Resources.Size, typeof(string));

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

            foreach (FileInfo file in _stateFiles)
            {
                _tableData.Rows.Add(file.Name, file.CreationTime, file.Length);
            }

            _table.Update();
            _table.SetNeedsDisplay();


        }

    }
}
