using Terminal.Gui;

namespace sakura.helpers
{
    /// <summary>
    /// Represents a set of configurable hotkeys for rom operations.
    /// This struct holds the key mappings for starting/stopping roms and opening path folders.
    /// </summary>
    public class RomHotkeys
    {
        /// <summary>
        /// Initializes a new instance of the <see cref="RomHotkeys"/> struct.
        /// The default keys are assigned automatically via property initializers.
        /// </summary>
        public RomHotkeys() { }

        /// <summary>
        /// The key used to start/resume a rom download.
        /// Default is F3.
        /// </summary>
        public Key StartRom { get; set; } = Key.F3;

        /// <summary>
        /// The key used to stop a rom.
        /// Default is F4.
        /// </summary>
        public Key StopRom { get; set; } = Key.F4;

        /// <summary>
        /// The key used to open the rom's path.
        /// Default is F5.
        /// </summary>
        public Key OpenRomPath { get; set; } = Key.F5;

        /// <summary>
        /// The key used to open a rom's SRAM .sav folder.
        /// Default is F6.
        /// </summary>
        public Key OpenSramPath { get; set; } = Key.F6;


        /// <summary>
        /// The key used to open a rom's SRAM .sav folder.
        /// Default is F6.
        /// </summary>
        public Key SaveState { get; set; } = Key.F7;

        /// <summary>
        /// The key used to open a rom's SRAM .sav folder.
        /// Default is F6.
        /// </summary>
        public Key LoadState { get; set; } = Key.F8;

        /// <summary>
        /// Start button
        /// Default is a.
        /// </summary>
        public Key START { get; set; } = Key.a;


        /// <summary>
        /// Select button
        /// Default is s.
        /// </summary>
        public Key SELECT { get; set; } = Key.s;


        /// <summary>
        /// A button
        /// Default is z.
        /// </summary>
        public Key A { get; set; } = Key.z;


        /// <summary>
        /// B button
        /// Default is x.
        /// </summary>
        public Key B { get; set; } = Key.x;


        /// <summary>
        /// Up button
        /// Default is up arrow.
        /// </summary>
        public Key UP { get; set; } = Key.CursorUp;


        /// <summary>
        /// Up button
        /// Default is down arrow.
        /// </summary>
        public Key DOWN { get; set; } = Key.CursorDown;


        /// <summary>
        /// Up button
        /// Default is left arrow.
        /// </summary>
        public Key LEFT { get; set; } = Key.CursorLeft;


        /// <summary>
        /// Up button
        /// Default is right arrow.
        /// </summary>
        public Key RIGHT { get; set; } = Key.CursorRight;
    }
}
