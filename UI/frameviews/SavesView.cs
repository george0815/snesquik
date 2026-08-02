//TODO: IMPLEMENT SAVES VIEW AND RESOURCE STRING
using Terminal.Gui;

namespace sakura.frameviews;

/// <summary>
/// View for options for manging SRAM .sav files and save states.
///
/// Logic is similar to the settings/controls view, but is placed here to separate the responsibilites.
/// </summary>
internal class SavesView : FrameView
{

    FrameView saveStateView;
    FrameView sramView;
    List<FileInfo> sramFiles;
    List<FileInfo> stateFiles;

    public SavesView()
        : base(Resources.Saves)
    {
        // Position the frame consistently with other views.
        X = 20;
        Y = SettingsData.HeaderHeight;
        Width = Dim.Fill();
        Height = Dim.Fill();


        // Get all save state files. 
        if (!Directory.Exists(Settings.Current.SramPath)) { Directory.CreateDirectory(Settings.Current.SramPath!); }
        if (!Directory.Exists(Settings.Current.StatePath)) { Directory.CreateDirectory(Settings.Current.StatePath!); }
        DirectoryInfo sramInfo = new DirectoryInfo(Settings.Current.SramPath!);
        DirectoryInfo saveStateInfo = new DirectoryInfo(Settings.Current.StatePath!);
        sramFiles = sramInfo.GetFiles().ToList();
        stateFiles = saveStateInfo.GetFiles().ToList();



        // Sidebar container
        var sidebar = new FrameView()
        {
            X = 0,
            Y = 0,
            Width = 20,
            Height = Dim.Fill(),
            Border = new Border() { BorderStyle = BorderStyle.Rounded }
        };

        // Sidebar menu
        var menu = new ListView(new string[]
        {
                Resources.SRAM,
                Resources.SaveStates,
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
            SwitchPanel(args.Item);
        };

        sidebar.Add(menu);
        Add(sidebar);

        sramView = new SramView(sramFiles);
        saveStateView = new SaveStateView(stateFiles);

        Add(sramView);

    }

    /// <summary>
    /// Switches visible content panel based on menu index.
    /// </summary>
    private void SwitchPanel(int index)
    {
        // Remove all panels first
        Remove(sramView);
        Remove(saveStateView);


        switch (index)
        {
            case 0: Add(sramView); break;
            case 1: Add(saveStateView); break;
        }

        SetNeedsDisplay();
    }

}
