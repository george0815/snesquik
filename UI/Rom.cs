//TODO: DOC STRINGS, COMMENTS, AND PARSING ROM DATA

using sakura.helpers;

namespace sakura;

//Class that represents SNES rom
public class Rom
{

    public string? Name { get; set; }

    public string? RomPath { get; set; }

    public string? SramPath { get; set; }

    public static List<Rom>? Roms { get; set; } = new List<Rom>();




    //For all rom directories, get all roms in each directoy and return list of all roms
    internal static void GetAllRoms()
    {

        List<Rom> roms = new List<Rom>();
        if (!Settings.Current.AllRomPaths.Contains(Settings.Current.DefaultRomPath!))
        {
            Settings.Current.AllRomPaths.Add(Settings.Current.DefaultRomPath!);
        }
        foreach (string path in Settings.Current.AllRomPaths)
        {
            if (Directory.Exists(path))
            {
                roms.AddRange(GetRoms(path));
            }
        }

        Roms = roms;

    }

    private static List<Rom> GetRoms(string path)
    {


        List<Rom> tempRoms = new List<Rom>();
        DirectoryInfo DirInfo = new DirectoryInfo(path);

        foreach (FileInfo file in DirInfo.EnumerateFiles())
        {
            string ext = Path.GetExtension(file.Name);

            if (ext.ToLower() == ".sfc")
            {

                Log.Write($"{Resources.Romadded}{file.Name}");

                //create temp rom
                string name = Path.GetFileNameWithoutExtension(file.Name);
                Rom tempRom = new Rom()
                {
                    Name = name,
                    RomPath = Path.Combine(file.DirectoryName!, file.Name),
                    SramPath = Path.Combine(Settings.Current.SramPath!, name),
                };
                tempRoms.Add(tempRom);

            }
        }


        return tempRoms;
    }

}


