
using Terminal.Gui;
using sakura.frameviews;

namespace sakura.helpers
{

    /// <summary>
    /// Provides helper methods to display dialogs for selecting colors, files, folders, and sorting criteria using Terminal.Gui.
    /// </summary>
    public static class DialogHelpers
    {


        /// <summary>
        /// Displays a dialog to pick a color from a grid.
        /// </summary>
        /// <returns>The name of the selected color.</returns>
        public static string PickColorGrid()
        {
            var colors = SettingsView.colors;
            var dlg = new Dialog(Resources.ChooseColor, 50, 8)
            {
                // Set the color scheme for the dialog window
                ColorScheme = new ColorScheme()
                {
                    Normal = Application.Driver.MakeAttribute(Settings.Current.TextColor, Color.Black),
                    Focus = Application.Driver.MakeAttribute(Settings.Current.FocusTextColor, Color.Black),
                    HotNormal = Application.Driver.MakeAttribute(Settings.Current.HotTextColor, Color.Black),
                    HotFocus = Application.Driver.MakeAttribute(Settings.Current.FocusTextColor, Color.Black),
                }
            };

            string result = Resources.Black; // Default color

            int x = 0, y = 0;
            foreach (var c in colors)
            {
                // Adjust text color for visibility on black background
                var textColor = c.Value == Color.Black ? Color.White : c.Value;

                var box = new Button($" {c.Key} ")
                {
                    X = x * 15, // Grid column
                    Y = y,      // Grid row
                    ColorScheme = new ColorScheme()
                    {
                        Normal = new Terminal.Gui.Attribute(textColor, Color.Black),
                        Focus = Application.Driver.MakeAttribute(textColor, Color.Black),
                        HotNormal = Application.Driver.MakeAttribute(textColor, Color.Black),
                        HotFocus = Application.Driver.MakeAttribute(textColor, Color.Black),
                    }
                };

                // Set color selection on click
                box.Clicked += () =>
                {
                    result = c.Key;
                    Application.RequestStop();
                };

                dlg.Add(box);

                x++; // Move to next column
                if (x == 3) { x = 0; y++; } // Wrap to next row after 3 columns
            }

            Application.Run(dlg);
            return result;
        }

        /// <summary>
        /// Shows a save file dialog with validation for allowed extensions and directories.
        /// </summary>
        public static string? ShowSaveFileDialog(string title, string message, string[] allowedExtensions, string defaultFileName = "")
        {
            var dialog = new SaveDialog(title, message)
            {
                CanCreateDirectories = true, // Allow directory creation
                AllowedFileTypes = allowedExtensions,
                FilePath = defaultFileName
            };

            Application.Run(dialog);

            // Check if user canceled
            if (dialog.Canceled || string.IsNullOrWhiteSpace(dialog.FilePath?.ToString()))
                return null;

            string? path = dialog.FilePath!.ToString();

            try
            {
                // Ensure directory exists or create it
                var dir = Path.GetDirectoryName(path);
                if (string.IsNullOrWhiteSpace(dir))
                    throw new Exception(Resources.Invalidfile_folderpath);

                if (!Directory.Exists(dir))
                    Directory.CreateDirectory(dir);

                return path;
            }
            catch (Exception ex)
            {
                // Show error message
                MessageBox.ErrorQuery(40, 10, Resources.Error, ex.Message, Resources.OK);
                return null;
            }
        }

        /// <summary>
        /// Shows an open file dialog with optional validation for existing files.
        /// </summary>
        public static string? ShowFileDialog(string title, string message, string[] allowedExtensions, bool create)
        {
            var dialog = new OpenDialog(title, message)
            {
                CanChooseFiles = true,
                CanChooseDirectories = false,
                AllowedFileTypes = allowedExtensions,
                AllowsMultipleSelection = false
            };

            Application.Run(dialog);

            if (!dialog.Canceled && dialog.FilePaths.Count > 0)
            {
                var path = dialog.FilePath.ToString();

                if (!create)
                {
                    if (File.Exists(path)) { return path; } // Valid file exists
                    else { MessageBox.ErrorQuery(Resources.InvalidFile, Resources.Selectedfiledoesnotexist, Resources.OK); }
                }
                else
                {
                    return path; // Return path even if file doesn't exist yet
                }
            }

            return null;
        }

        /// <summary>
        /// Truncates a string to a maximum length.
        /// </summary>
        internal static string Truncate(this string value, int maxLength)
        {
            if (string.IsNullOrEmpty(value)) return value; // Nothing to truncate
            return value.Length <= maxLength ? value : value[..maxLength]; // Truncate if necessary
        }

        /// <summary>
        /// Shows a folder selection dialog with validation.
        /// </summary>
        public static string? ShowFolderDialog(string title, string message)
        {
            var dialog = new OpenDialog(title, message)
            {
                CanChooseFiles = false,
                CanChooseDirectories = true,
                AllowsMultipleSelection = false
            };

            Application.Run(dialog);

            if (!dialog.Canceled && dialog.FilePaths.Count > 0)
            {
                var path = dialog.FilePath.ToString();

                if (Directory.Exists(path))
                    return path; // Valid folder
                else
                    MessageBox.ErrorQuery(Resources.InvalidFolder, Resources.Selectedfolderdoesnotexist, Resources.OK);
            }

            return null; // Canceled or invalid
        }


        /// <summary>
        /// Displays a dialog to pick a key for the controls
        /// </summary>
        /// <returns>The name of the selected color.</returns>
        public static int PickKey()
        {
            var colors = SettingsView.colors;
            var dlg = new Dialog(Resources.Inputkey, 50, 3)
            {
                // Set the color scheme for the dialog window
                ColorScheme = new ColorScheme()
                {
                    Normal = Application.Driver.MakeAttribute(Settings.Current.TextColor, Color.Black),
                    Focus = Application.Driver.MakeAttribute(Settings.Current.FocusTextColor, Color.Black),
                    HotNormal = Application.Driver.MakeAttribute(Settings.Current.HotTextColor, Color.Black),
                    HotFocus = Application.Driver.MakeAttribute(Settings.Current.FocusTextColor, Color.Black),
                }
            };

            int result = (int)Key.Enter;


            dlg.Add(new Label(Resources.Waitingforinput));

            dlg.KeyDown += e =>
            {
                result = e.KeyEvent.KeyValue;
                Application.RequestStop();
                e.Handled = true;
            };


            Application.Run(dlg);
            return result;
        }
    }
}
