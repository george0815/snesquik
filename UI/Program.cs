
using sakura.helpers;
using System.Text;
using Terminal.Gui;

namespace sakura
{
    /// <summary> Entry point 
    /// Initializes settings, plugins, and starts the Terminal.Gui interface.
    /// </summary>
    /// <author>George Hunter S.</author>
    /// <created>May, 2026</created>
    class Program
    {
        public static async Task Main(string[] args)
        {


            // ------------------------------
            // Optional debug: change culture to Japanese
            // ------------------------------
            //CultureInfo ci = new("ja-JP");
            //Thread.CurrentThread.CurrentUICulture = ci;
            //CultureInfo.DefaultThreadCurrentCulture = ci;
            //CultureInfo.DefaultThreadCurrentUICulture = ci;


            // load user settings from file
            Settings.Load();


            // Register encodings for legacy code pages (e.g., Shift-JIS)
            Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);

            // Enable UTF-8
            Application.UseSystemConsole = Settings.Current.UseSystemConsole;
            Console.InputEncoding = Encoding.UTF8;
            Console.OutputEncoding = Encoding.UTF8;


            // Swallow standard error
            Console.SetError(TextWriter.Null);

            // ------------------------------
            // Initialize Terminal.Gui
            // ------------------------------
            Application.Init();


            // Top-level container for windows
            var top = Application.Top;

            // Main window (contains header, sidebar, and content panels)
            var mainWin = new SakuraUI();

            // ------------------------------
            // Setup color scheme for the main window
            // ------------------------------
            ColorScheme myScheme = new()
            {
                Normal = Application.Driver.MakeAttribute(Settings.Current.TextColor, Settings.Current.BackgroundColor), // normal text
                Focus = Application.Driver.MakeAttribute(Settings.Current.FocusTextColor, Settings.Current.FocusBackgroundColor), // focused element
                HotNormal = Application.Driver.MakeAttribute(Settings.Current.HotTextColor, Settings.Current.BackgroundColor), // hotkey text
                HotFocus = Application.Driver.MakeAttribute(Settings.Current.FocusTextColor, Settings.Current.FocusBackgroundColor), // focused hotkey
            };

            mainWin.ColorScheme = myScheme;

            // Add main window to the top-level container
            top.Add(mainWin);

            // ------------------------------
            // Run the application
            // ------------------------------
            try
            {
                Application.Run(); // blocks until user exits
            }
            finally
            {
                // ------------------------------
                // Cleanup / persist data
                // ------------------------------
                Log.Save(); // save logs to file

                // Shutdown Terminal.Gui
                Application.Shutdown();

                // Restore terminal state if on Linux or macOS
                if (OperatingSystem.IsLinux() || OperatingSystem.IsMacOS())
                {
                    Console.Write("\x1b[0m");      // reset attributes
                    Console.Write("\x1b[?25h");   // show cursor
                    Console.Write("\x1b[?1049l"); // leave alt screen
                    Console.Out.Flush();
                }

            }


        }
    }
}
