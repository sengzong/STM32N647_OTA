# Portable initial-install merger

Windows Forms application targeting the Windows x64 .NET Framework 4.x runtime.
Build with `powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_install_merger.ps1`.
It uses the compiler included with Windows; Python, CubeIDE and downloaded build
dependencies are unnecessary for the resulting executable.

The GUI defaults to current Binary/compat files when kept at its shipped project
location. Elsewhere it uses the accompanying firmware folder. Users can select
different component HEX files, a default/600/AI-800 boot profile and optional
network-data HEX. No signing, device access, firmware patching or network changes
occur in the tool.

The parser validates Intel HEX checksums, supported records, EOF, non-overlapping
addresses and component ranges. FSBL checks mirror verify_fsbl_image.py plus the
project Boot Manager markers. Those checks do not prove an arbitrary FSBL has the
compatible partition layout: use this project's compat FSBL and Agent. Application
vectors are checked before adding models. Sparse application/model gaps are filled
with FF so on-device CRC does not depend on old flash content.

Output includes the signed FSBL, Agent, full application image, runtime prefix and
three initialized record sectors, exactly matching build_ota.py's current layout.
The runtime image length includes a selected model. A completed temporary HEX is
parsed again before publication. Existing output is replaced by a same-volume
MoveFileEx rename with WRITE_THROUGH; invalid input never replaces prior output.
GUI overwrite asks once, CLI requires --overwrite. Input-path overwrite is rejected.

`--preview FILE.png` draws the actual form offscreen for layout review. The normal
entry point opens the GUI; --help lists the optional command-line arguments.
Run `python tools/test_install_merger.py` for the executable integration suite.
