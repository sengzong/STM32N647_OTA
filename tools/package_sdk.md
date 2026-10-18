# SDK packaging tool

`python tools/package_sdk.py` scans the configured SDK Projects directory and
creates one .ota file plus a source/hash manifest per prebuilt example under
Binary/sdk_packages. It never edits SDK sources. The inventory reports failures
instead of silently omitting incompatible addresses or missing AI model files.

`--project` selects one example with Binary/appli.hex; `--sdk` changes the SDK
root; `--out` changes the output folder. Generated packages require the paired
Binary/compat FSBL/Agent. Existing board layouts are never modified by this tool.
