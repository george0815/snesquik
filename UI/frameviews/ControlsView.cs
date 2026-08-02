//TODO: UPDATE FOR SNES CONTROLLER
using Terminal.Gui;
using sakura.helpers;

namespace sakura.frameviews
{
    /// <summary>
    /// ControlsView contains buttons that set the controls for the emulator.
    /// </summary>
    internal class ControlsView : FrameView
    {

        /// <summary>
        /// Initializes the controls UI and wires all controls
        /// directly to <see cref="Settings.Current"/>.
        ///
        /// Layout is built procedurally to keep ordering explicit
        /// and easy to modify without hidden layout logic.
        /// </summary>
        public ControlsView()
            : base(Resources.Controls)
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


            #region CONTROLS SETTINGS

            // START ROM
            scroll.Add(new Label(Resources.Startrom) { X = 1, Y = y });
            var myKey = Settings.Current.Controls.StartRom;
            var startRomButton = new Button()
            {
                X = 30,
                Y = y,
                Width = 3,
                Height = 1,
                Text = myKey.ToString()
            };

            startRomButton.Clicked += () =>
            {
                // Pick color from grid dialog
                int tmp = DialogHelpers.PickKey();
                Settings.Current.Controls.StartRom = (Key)tmp;

                // Update button label to reflect selection
                char tmpChr = (char)tmp;
                string tmpString = tmpChr.ToString();
                startRomButton.Text = ((Key)tmp).ToString();

                // Force redraw
                startRomButton.SetNeedsDisplay();
                scroll.SetNeedsDisplay();
            };

            scroll.Add(startRomButton);
            y += 2;

            // STOP ROM 
            scroll.Add(new Label(Resources.Stoprom) { X = 1, Y = y });
            myKey = Settings.Current.Controls.StopRom;
            var stopRomButton = new Button()
            {
                X = 30,
                Y = y,
                Width = 3,
                Height = 1,
                Text = myKey.ToString()
            };

            stopRomButton.Clicked += () =>
            {
                // Pick color from grid dialog
                int tmp = DialogHelpers.PickKey();
                Settings.Current.Controls.StopRom = (Key)tmp;

                // Update button label to reflect selection
                char tmpChr = (char)tmp;
                string tmpString = tmpChr.ToString();
                stopRomButton.Text = ((Key)tmp).ToString();

                // Force redraw
                stopRomButton.SetNeedsDisplay();
                scroll.SetNeedsDisplay();
            };

            scroll.Add(stopRomButton);
            y += 2;


            // OPEN ROM FOLDER 
            scroll.Add(new Label(Resources.Openromfolder) { X = 1, Y = y });
            myKey = Settings.Current.Controls.OpenRomPath;
            var openRomPathButton = new Button()
            {
                X = 30,
                Y = y,
                Width = 3,
                Height = 1,
                Text = myKey.ToString()
            };

            openRomPathButton.Clicked += () =>
            {
                // Pick color from grid dialog
                int tmp = DialogHelpers.PickKey();
                Settings.Current.Controls.OpenRomPath = (Key)tmp;

                // Update button label to reflect selection
                char tmpChr = (char)tmp;
                string tmpString = tmpChr.ToString();
                openRomPathButton.Text = ((Key)tmp).ToString();

                // Force redraw
                openRomPathButton.SetNeedsDisplay();
                scroll.SetNeedsDisplay();
            };

            scroll.Add(openRomPathButton);
            y += 2;


            // OPEN SRAM PATH 
            scroll.Add(new Label(Resources.Opensramfolder) { X = 1, Y = y });
            myKey = Settings.Current.Controls.OpenSramPath;
            var openSramButton = new Button()
            {
                X = 30,
                Y = y,
                Width = 3,
                Height = 1,
                Text = myKey.ToString()
            };

            openSramButton.Clicked += () =>
            {
                // Pick color from grid dialog
                int tmp = DialogHelpers.PickKey();
                Settings.Current.Controls.OpenSramPath = (Key)tmp;

                // Update button label to reflect selection
                char tmpChr = (char)tmp;
                string tmpString = tmpChr.ToString();
                openSramButton.Text = ((Key)tmp).ToString();

                // Force redraw
                openSramButton.SetNeedsDisplay();
                scroll.SetNeedsDisplay();
            };

            scroll.Add(openSramButton);
            y += 2;


            // SAVE STATE 
            scroll.Add(new Label(Resources.Savestate) { X = 1, Y = y });
            myKey = Settings.Current.Controls.SaveState;
            var saveStateButton = new Button()
            {
                X = 30,
                Y = y,
                Width = 3,
                Height = 1,
                Text = myKey.ToString()
            };

            saveStateButton.Clicked += () =>
            {
                // Pick color from grid dialog
                int tmp = DialogHelpers.PickKey();
                Settings.Current.Controls.SaveState = (Key)tmp;

                // Update button label to reflect selection
                char tmpChr = (char)tmp;
                string tmpString = tmpChr.ToString();
                saveStateButton.Text = ((Key)tmp).ToString();

                // Force redraw
                saveStateButton.SetNeedsDisplay();
                scroll.SetNeedsDisplay();
            };

            scroll.Add(saveStateButton);
            y += 2;


            // LOAD STATE 
            scroll.Add(new Label(Resources.Loadstate) { X = 1, Y = y });
            myKey = Settings.Current.Controls.LoadState;
            var loadStateButton = new Button()
            {
                X = 30,
                Y = y,
                Width = 3,
                Height = 1,
                Text = myKey.ToString()
            };

            loadStateButton.Clicked += () =>
            {
                // Pick color from grid dialog
                int tmp = DialogHelpers.PickKey();
                Settings.Current.Controls.LoadState = (Key)tmp;

                // Update button label to reflect selection
                char tmpChr = (char)tmp;
                string tmpString = tmpChr.ToString();
                loadStateButton.Text = ((Key)tmp).ToString();

                // Force redraw
                loadStateButton.SetNeedsDisplay();
                scroll.SetNeedsDisplay();
            };

            scroll.Add(loadStateButton);
            y += 2;

            // START 
            scroll.Add(new Label(Resources.START) { X = 1, Y = y });
            myKey = Settings.Current.Controls.START;
            var startButton = new Button()
            {
                X = 30,
                Y = y,
                Width = 3,
                Height = 1,
                Text = myKey.ToString()
            };

            startButton.Clicked += () =>
            {
                // Pick color from grid dialog
                int tmp = DialogHelpers.PickKey();
                Settings.Current.Controls.START = (Key)tmp;

                // Update button label to reflect selection
                char tmpChr = (char)tmp;
                string tmpString = tmpChr.ToString();
                startButton.Text = ((Key)tmp).ToString();

                // Force redraw
                startButton.SetNeedsDisplay();
                scroll.SetNeedsDisplay();
            };

            scroll.Add(startButton);
            y += 2;

            // SELECT
            scroll.Add(new Label(Resources.SELECT) { X = 1, Y = y });
            myKey = Settings.Current.Controls.SELECT;
            var selectButton = new Button()
            {
                X = 30,
                Y = y,
                Width = 3,
                Height = 1,
                Text = myKey.ToString()
            };

            selectButton.Clicked += () =>
            {
                // Pick color from grid dialog
                int tmp = DialogHelpers.PickKey();
                Settings.Current.Controls.SELECT = (Key)tmp;

                // Update button label to reflect selection
                char tmpChr = (char)tmp;
                string tmpString = tmpChr.ToString();
                selectButton.Text = ((Key)tmp).ToString();

                // Force redraw
                selectButton.SetNeedsDisplay();
                scroll.SetNeedsDisplay();
            };

            scroll.Add(selectButton);
            y += 2;

            // A
            scroll.Add(new Label(Resources.A) { X = 1, Y = y });
            myKey = Settings.Current.Controls.A;
            var aButton = new Button()
            {
                X = 30,
                Y = y,
                Width = 3,
                Height = 1,
                Text = myKey.ToString()
            };

            aButton.Clicked += () =>
            {
                // Pick color from grid dialog
                int tmp = DialogHelpers.PickKey();
                Settings.Current.Controls.A = (Key)tmp;

                // Update button label to reflect selection
                char tmpChr = (char)tmp;
                string tmpString = tmpChr.ToString();
                aButton.Text = ((Key)tmp).ToString();

                // Force redraw
                aButton.SetNeedsDisplay();
                scroll.SetNeedsDisplay();
            };

            scroll.Add(aButton);
            y += 2;

            // B
            scroll.Add(new Label(Resources.B) { X = 1, Y = y });
            myKey = Settings.Current.Controls.B;
            var bButton = new Button()
            {
                X = 30,
                Y = y,
                Width = 3,
                Height = 1,
                Text = myKey.ToString()
            };

            bButton.Clicked += () =>
            {
                // Pick color from grid dialog
                int tmp = DialogHelpers.PickKey();
                Settings.Current.Controls.B = (Key)tmp;

                // Update button label to reflect selection
                char tmpChr = (char)tmp;
                string tmpString = tmpChr.ToString();
                bButton.Text = ((Key)tmp).ToString();

                // Force redraw
                bButton.SetNeedsDisplay();
                scroll.SetNeedsDisplay();
            };

            scroll.Add(bButton);
            y += 2;

            // X
            scroll.Add(new Label(Resources.X) { X = 1, Y = y });
            myKey = Settings.Current.Controls.X;
            var xButton = new Button()
            {
                X = 30,
                Y = y,
                Width = 3,
                Height = 1,
                Text = myKey.ToString()
            };

            xButton.Clicked += () =>
            {
                // Pick color from grid dialog
                int tmp = DialogHelpers.PickKey();
                Settings.Current.Controls.X = (Key)tmp;

                // Update button label to reflect selection
                char tmpChr = (char)tmp;
                string tmpString = tmpChr.ToString();
                xButton.Text = ((Key)tmp).ToString();

                // Force redraw
                xButton.SetNeedsDisplay();
                scroll.SetNeedsDisplay();
            };

            scroll.Add(xButton);
            y += 2;


            // Y
            scroll.Add(new Label(Resources.Y) { X = 1, Y = y });
            myKey = Settings.Current.Controls.Y;
            var yButton = new Button()
            {
                X = 30,
                Y = y,
                Width = 3,
                Height = 1,
                Text = myKey.ToString()
            };

            yButton.Clicked += () =>
            {
                // Pick color from grid dialog
                int tmp = DialogHelpers.PickKey();
                Settings.Current.Controls.Y = (Key)tmp;

                // Update button label to reflect selection
                char tmpChr = (char)tmp;
                string tmpString = tmpChr.ToString();
                yButton.Text = ((Key)tmp).ToString();

                // Force redraw
                yButton.SetNeedsDisplay();
                scroll.SetNeedsDisplay();
            };

            scroll.Add(yButton);
            y += 2;


            // L
            scroll.Add(new Label(Resources.L) { X = 1, Y = y });
            myKey = Settings.Current.Controls.L;
            var lButton = new Button()
            {
                X = 30,
                Y = y,
                Width = 3,
                Height = 1,
                Text = myKey.ToString()
            };

            lButton.Clicked += () =>
            {
                // Pick color from grid dialog
                int tmp = DialogHelpers.PickKey();
                Settings.Current.Controls.L = (Key)tmp;

                // Update button label to reflect selection
                char tmpChr = (char)tmp;
                string tmpString = tmpChr.ToString();
                lButton.Text = ((Key)tmp).ToString();

                // Force redraw
                lButton.SetNeedsDisplay();
                scroll.SetNeedsDisplay();
            };

            scroll.Add(lButton);
            y += 2;


            // R
            scroll.Add(new Label(Resources.R) { X = 1, Y = y });
            myKey = Settings.Current.Controls.R;
            var rButton = new Button()
            {
                X = 30,
                Y = y,
                Width = 3,
                Height = 1,
                Text = myKey.ToString()
            };

            rButton.Clicked += () =>
            {
                // Pick color from grid dialog
                int tmp = DialogHelpers.PickKey();
                Settings.Current.Controls.R = (Key)tmp;

                // Update button label to reflect selection
                char tmpChr = (char)tmp;
                string tmpString = tmpChr.ToString();
                rButton.Text = ((Key)tmp).ToString();

                // Force redraw
                rButton.SetNeedsDisplay();
                scroll.SetNeedsDisplay();
            };

            scroll.Add(rButton);
            y += 2;

            // UP
            scroll.Add(new Label(Resources.UP) { X = 1, Y = y });
            myKey = Settings.Current.Controls.UP;
            var upButton = new Button()
            {
                X = 30,
                Y = y,
                Width = 3,
                Height = 1,
                Text = myKey.ToString()
            };

            upButton.Clicked += () =>
            {
                // Pick color from grid dialog
                int tmp = DialogHelpers.PickKey();
                Settings.Current.Controls.UP = (Key)tmp;

                // Update button label to reflect selection
                char tmpChr = (char)tmp;
                string tmpString = tmpChr.ToString();
                upButton.Text = ((Key)tmp).ToString();

                // Force redraw
                upButton.SetNeedsDisplay();
                scroll.SetNeedsDisplay();
            };

            scroll.Add(upButton);
            y += 2;

            // DOWN
            scroll.Add(new Label(Resources.DOWN) { X = 1, Y = y });
            myKey = Settings.Current.Controls.DOWN;
            var downButton = new Button()
            {
                X = 30,
                Y = y,
                Width = 3,
                Height = 1,
                Text = myKey.ToString()
            };

            downButton.Clicked += () =>
            {
                // Pick color from grid dialog
                int tmp = DialogHelpers.PickKey();
                Settings.Current.Controls.DOWN = (Key)tmp;

                // Update button label to reflect selection
                char tmpChr = (char)tmp;
                string tmpString = tmpChr.ToString();
                downButton.Text = ((Key)tmp).ToString();

                // Force redraw
                downButton.SetNeedsDisplay();
                scroll.SetNeedsDisplay();
            };

            scroll.Add(downButton);
            y += 2;

            // LEFT
            scroll.Add(new Label(Resources.LEFT) { X = 1, Y = y });
            myKey = Settings.Current.Controls.LEFT;
            var leftButton = new Button()
            {
                X = 30,
                Y = y,
                Width = 3,
                Height = 1,
                Text = myKey.ToString()
            };

            leftButton.Clicked += () =>
            {
                // Pick color from grid dialog
                int tmp = DialogHelpers.PickKey();
                Settings.Current.Controls.LEFT = (Key)tmp;

                // Update button label to reflect selection
                char tmpChr = (char)tmp;
                string tmpString = tmpChr.ToString();
                leftButton.Text = ((Key)tmp).ToString();

                // Force redraw
                leftButton.SetNeedsDisplay();
                scroll.SetNeedsDisplay();
            };

            scroll.Add(leftButton);
            y += 2;


            // RIGHT
            scroll.Add(new Label(Resources.RIGHT) { X = 1, Y = y });
            myKey = Settings.Current.Controls.RIGHT;
            var rightButton = new Button()
            {
                X = 30,
                Y = y,
                Width = 3,
                Height = 1,
                Text = myKey.ToString()
            };

            rightButton.Clicked += () =>
            {
                // Pick color from grid dialog
                int tmp = DialogHelpers.PickKey();
                Settings.Current.Controls.RIGHT = (Key)tmp;

                // Update button label to reflect selection
                char tmpChr = (char)tmp;
                string tmpString = tmpChr.ToString();
                rightButton.Text = ((Key)tmp).ToString();

                // Force redraw
                rightButton.SetNeedsDisplay();
                scroll.SetNeedsDisplay();
            };

            scroll.Add(rightButton);
            y += 2;


            #endregion

            // Persist settings to disk
            var saveBtn = new Button(Resources.Save) { X = 1, Y = y };
            scroll.Add(saveBtn);

            // Required so scroll bars calculate correctly
            scroll.ContentSize =
                new Terminal.Gui.Size(Application.Top.Frame.Width - 2, y + 2);

            saveBtn.Clicked += () =>
              {
                  try
                  {
                      // --- controls validation ---
                      Key[] tmpKeys = [
                          Settings.Current.Controls.OpenRomPath,
                          Settings.Current.Controls.OpenSramPath,
                          Settings.Current.Controls.SaveState,
                          Settings.Current.Controls.LoadState,
                          Settings.Current.Controls.StartRom,
                          Settings.Current.Controls.StopRom,
                          Settings.Current.Controls.START,
                          Settings.Current.Controls.SELECT,
                          Settings.Current.Controls.X,
                          Settings.Current.Controls.Y,
                          Settings.Current.Controls.L,
                          Settings.Current.Controls.R,
                          Settings.Current.Controls.A,
                          Settings.Current.Controls.B,
                          Settings.Current.Controls.UP,
                          Settings.Current.Controls.DOWN,
                          Settings.Current.Controls.LEFT,
                          Settings.Current.Controls.RIGHT,
                      ];

                      if (tmpKeys.Distinct().Count() != tmpKeys.Count())
                      {
                          MessageBox.ErrorQuery(Resources.Error, Resources.Cannothavethesamekey, Resources.OK);
                          return;
                      }

                      // --- apply settings ---
                      Settings.Save();

                      MessageBox.Query(Resources.Settings, Resources.Controlssavedsuccessfully, Resources.OK);
                      Log.Write(Resources.Controlssaved);
                  }
                  catch (Exception ex)
                  {
                      MessageBox.ErrorQuery(Resources.FatalError, ex.Message, Resources.OK);
                  }
              };


        }
    }
}
