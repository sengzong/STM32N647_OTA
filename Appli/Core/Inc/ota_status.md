# Shared OTA display status

Name and version have 128/64-byte bounded UTF-8 buffers. Model state distinguishes
included, absent and not supplied. The profile describes the target application,
not the independent receiver's current CPU clock. An empty version means the
user did not provide one. The UI must not fabricate a semantic version.
