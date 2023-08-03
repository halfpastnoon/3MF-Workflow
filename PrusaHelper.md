# PrusaHelper
## Basic Usage
In Windows Subsystem for Linux, run the following command:
`prusa-helper [3MF Path] [PrusaSlicer Path]`
### Parameters
1. `[3MF Path]`: Windows absolute path to 3MF file generated with MatAssign script, containing material properties. This can be easily obtained by right-clicking on the 3MF file and choosing 'Copy As Path'.
2. `[PrusaSlicer Path]`: Windows absolute path to PrusaSlicer AppData folder. Usually found at: `C:\Users\[Your Username]\AppData\Roaming\PrusaSlicer`.

If your PrusaSlicer installation does not have the required materials as laid out in the 3MF assigned to extruders, PrusaHelper will not open the 3MF in PrusaSlicer until this has been fixed. The properties of missing materials will be provided at the command line. 
### Material Definition
To define a new material with specific properties, follow these steps:
1. Open PrusaSlicer and locate filament profile to which you would like to assign properties (this can be done under the filament settings tab).
2. Under the notes tab, assign properties using the provided text-box in the following manner: `[Property Name]:[Value]`, pressing enter after each line. `[Property Name]` can be one of either `density`, `youngs_modulus`, or `poisson_ratio`. `Value` is accurate to 0.0001 Units. An example is provided below (not usable with clipboard).
	>density:123.4561
 	>
 	>youngs_modulus:789.1232
	>
	>poisson_ratio:456.7893
	
3. Save the filament profile as a custom profile with a custom name (click the save icon near the profile name near the top of the window). You can verify this worked by checking the 'filament' folder for an INI file with the custom name provided to PrusaSlicer. The 'filament' folder can be found in the folder pointed to by the `[PrusaSlicer Path]` parameter.
4. Example filament profile INI files with appropriate material definitions are provided in the `Examples/` directory. Material properties can be found under the `filament_notes` section of the INI file.
## Installation
### 3MF_Scripts Installation
 1. Install Windows Subsystem for Linux (WSL) on your system, preferably Ubuntu but any Linux distro should work.
 2. Download `3MF_Scripts.zip` from the GitHub and extract to a folder accessible in WSL. (If using VS Code, stop here and open new folder using VS Code with C-Make extensions)
 3. Set the current working directory (cwd) of the WSL shell to the `Source` folder inside the newly extracted folder.
 4. Run: `chmod +x GenerateMake.sh`
 5. Run: `./GenerateMake.sh`
 6. Set the cwd of the shell to the newly created `build` folder inside the `Source` folder.
 7. Run: `make`
 8. Now, the binaries for both MatAssign and PrusaHelper have been generated and are present in the `build` folder and can be ran with `./mat-assign [arguments]` or `./prusa-helper [arguments]`. 
 9. Add these scripts to the PATH variable in your distribution and these scripts can be called anywhere in the filesystem, with syntax like `mat-assign [arguments]` or `prusa-helper [arguments]`.
 
### Custom PrusaSlicer Installation 
1. Download and Install PrusaSlicer v2.6.0 from their [Releases](https://github.com/prusa3d/PrusaSlicer/releases) page on GitHub
2. Download updated .exe and .dll file from `PrusaSlicer/Binaries(Windows)` directory on GitHub.
3. Locate PrusaSlicer installation folder (usually `C:\Program Files\Prusa3D\PrusaSlicer`).
4. Replace `prusa-slicer-console.exe` and `PrusaSlicer.dll` with the provided files with matching extensions from GitHub.
5. For more information regarding how to build and modify the PrusaSlicer source code, visit [the PrusaSlicer Windows build instructions page](https://github.com/prusa3d/PrusaSlicer/blob/master/doc/How%20to%20build%20-%20Windows.md). Source code including modified files can be found in the `PrusaSlicer/src` directory on GitHub. This directory can freely replace the one provided by PrusaSlicer named `src` during the build process.

### Modified PrusaSlicer Source Files
- `PrusaSlicer/src/libslic3r/Model.cpp`
