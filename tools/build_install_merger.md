# Build the portable merger

Compile tools/initial_install_merger/InitialInstallMerger.cs with the Windows
.NET Framework 4.x x64 C# compiler, copy its Chinese README and current compat
component HEX files, and produce Portable/InitialInstallMerger.zip. The ZIP has
the EXE, README.txt and firmware folder at its root. It excludes generated output.
No dependency installation or device access occurs.

Run `powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_install_merger.ps1`.
Re-run after changing the tool or updating compat components to refresh the portable
snapshot. The in-project GUI prioritizes current Binary/compat over that snapshot.
