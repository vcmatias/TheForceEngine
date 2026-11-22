# DFMOD weapon style installer

The port has five weapon styles. Four of them ship inside the APK. The fifth, **DFMOD**, uses the
weapon models of the [Dark Forces Mod Remastered](https://www.moddb.com/mods/dark-forces-mod-remastered)
for Jedi Academy (DFmod 2025 Final). Those models belong to the authors of the mod, so the port does
not distribute them. This installer builds the file the game needs on your own computer, from your
own download of the mod, and copies it to the headset.

## What it does

1. Looks for `DFmod_2025_Final.zip` in your Downloads folder. If it is not there, it opens the mod's
   official download page and waits for the file (about 590 MB).
2. Takes the weapon models out of the archive (`models/weapons2` inside the mod's `.pk3` files).
3. Converts them: the detailed first-person model of each weapon is completed with the mod's own
   world model, and the result is packed as `dfmod_combined.bin` (about 52 MB).
4. Copies the file to the Quest with adb, to
   `/sdcard/Android/data/com.vcmatias.tfevr/files/dfmod_combined.bin`.

Then choose the DFMOD weapon style in the game options. Without the file, that style shows the
high resolution weapons.

## Running it

- **Windows:** double-click `install_dfmod.bat`. It needs Python 3 from python.org; on the first run
  it creates a private environment in `.venv` with numpy, scipy and pillow.
- **Linux, macOS:** `./install_dfmod.sh`
- The Quest must be connected by USB with developer mode on, and the game installed and opened once.
  adb is looked up in the PATH, in a `platform-tools` folder next to the installer, in SideQuest and in
  Meta Quest Developer Hub.

Options: `--zip FILE` (archive somewhere else), `--folder DIR` (unpacked copy of the mod),
`--no-push` (only convert), `--out FILE`, `--adb FILE`, `--recompute`, `--lang en|pt`.

## How the result is checked

`dfmod_2025_final.json` holds the SHA-256 of the 69 weapon files of DFmod 2025 Final that the
converter reads, the fit of each world model onto its first-person model (seven similarity
transforms) and the SHA-256 of the expected output. With the same files the installer reuses the
stored fit and takes a few seconds; on Linux with numpy 2.5 and scipy 1.18 the output is
byte-identical to the file used in the 0.41 build. With another version of the mod it computes the
fit again (several minutes) and the result should be reviewed in the headset.

Tested with an archive assembled from the mod's weapon files. The `.bat` launcher and the adb copy
were not run on a Windows machine with a headset before publication.

## What is in here

| Path | Contents |
|---|---|
| `install_dfmod.py`, `.bat`, `.sh` | The installer and its launchers |
| `dfmod_2025_final.json` | Hashes and model fits (numbers only) |
| `own_parts/` | Parts made for this port in Blender: thermal detonator, mine, fusion cutter handle (`tools/weapons`, `tools/dfmod/blender_finish.py`) |
| `../tools/dfmod/` | The converter: MD3 and GLM readers, `register.py`, `combine.py`, `pack_comb.py` |
| `make_zip.sh` | Builds the standalone zip attached to the releases |

No file of the mod is stored in this repository or in the zip.
