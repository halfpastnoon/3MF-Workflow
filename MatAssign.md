# MatAssign 
This script combines a folder of STL files into a multi-material 3MF Package
## Basic Usage
In Windows Subsystem for Linux, run the following command:
`mat-assign [STL Folder Path] [Output Filename]`


### Parameters
 1. `[STL Folder Path]`: Windows absolute path (starting with drive letter e.g. `C:`) to folder containing individual STL files, to which each will be assigned material properties. This path can be easily obtained by right-clicking the folder of choice and selecting 'Copy As Path' in Windows. Materials are mapped using the [Material Map File](#material-map-file) detailed later, though this file *must* be present in the directory.
 2. `[Output Filename]`: This is the final 3MF package and should be a filename ending in `.3mf`. This file will be written to the STL Folder given in the first argument.

### Material Map File
This is a special excel spreadsheet named `matmap.xlsx` and is located in the folder pointed to by the `[STL Folder Path]`.  Starting at the second row (allowing for headers), the 'A' column stores each STL file name. Each of these files should be formatted as `Segmentation_[Material Name].stl`. Columns B, C and D represent Poisson's Ratio, Material Density and Young's Modulus, respectively. Example material map files can be found under the `Examples/` directory.

### Material Properties (± 0.0001 Unit)
1. Poisson's Ratio
2. Material Density
3. Young's Modulus

## Installation

### 3MF_Scripts Installation
 1. Install Windows Subsystem for Linux (WSL) on your system, preferably Ubuntu but any Linux distro should work.
 2. Download `3MF_Scripts.zip` from the GitHub and extract to a folder within WSL (e.g. /home/PrusaBuild NOT /mnt/c/...). 
 >If using VS Code, stop here and open new folder using VS Code with C-Make extensions
 3. Set the current working directory (cwd) of the WSL shell to the `Source` folder inside the newly extracted folder.
 4. Run: `chmod +x GenerateMake.sh`
 5. Run: `./GenerateMake.sh`
 6. Set the cwd of the shell to the newly created `build` folder inside the `Source` folder.
 7. Run: `make`
 8. Now, the binaries for both MatAssign and PrusaHelper have been generated and are present in the `build` folder and can be ran with `./mat-assign [arguments]` or `./prusa-helper [arguments]`. 
 9. Add these scripts to the PATH variable in your distribution and these scripts can be called anywhere in the filesystem, with syntax like `mat-assign [arguments]` or `prusa-helper [arguments]`.
