//TODO: FIX LOG DISPLAY
using sakura;
using sakura.helpers;
using Terminal.Gui;

/// <summary>
/// Read-only view used to display application log output.
///
/// This view listens for log updates and keeps the ListView in sync
/// while attempting to preserve user context (scroll position and
/// selected row) across refreshes.
/// </summary>
internal class LogView : FrameView
{
    /// <summary>
    /// ListView displaying the current log entries.
    ///
    /// Exposed to allow external configuration if needed, but is
    /// fully initialized by the view itself.
    /// </summary>
    public ListView ListView { get; set; } = new();

    public LogView(List<string> log)
        : base(Resources.Log)
    {
        // Position the frame consistently with other views.
        X = 20;
        Y = SettingsData.HeaderHeight;
        Width = Dim.Fill();
        Height = Dim.Fill();

        // ListView is sized to leave a small visual margin
        // around the edges of the frame.
        ListView = new ListView(log)
        {
            X = 1,
            Y = 1,
            Width = Dim.Fill() - 2,
            Height = Dim.Fill() - 2
        };

        Add(ListView);

        // Subscribe to global log updates so the view stays live.
        Log.OnLogAdded += RefreshLog;
    }

    /// <summary>
    /// Refreshes the log display while preserving the user's
    /// current scroll position and selection when possible.
    ///
    /// UI updates are marshalled onto the main loop to remain
    /// thread-safe when logs are written from background tasks.
    /// </summary>
    internal void RefreshLog()
    {
        Application.MainLoop.Invoke(() =>
        {
            try
            {
                // Preserve current UI state before modifying the data source.
                int selected = ListView.SelectedItem;
                int top = ListView.TopItem;

                // Replace the ListView source with the latest log snapshot.
                ListView.SetSource(Log.LogList);

                // Restore scroll position if still within bounds.
                if (top < ListView.Source.Count)
                    ListView.TopItem = top;

                // Restore selection if possible, otherwise clamp to last item.
                if (selected < ListView.Source.Count)
                    ListView.SelectedItem = selected;
                else
                    ListView.SelectedItem = ListView.Source.Count - 1;

                // Force a redraw to reflect updated content.
                ListView.SetNeedsDisplay();
                SetNeedsDisplay();
            }
            catch
            {
                // Intentionally swallow exceptions to prevent
                // logging failures from crashing the UI.
            }
        });
    }
}
