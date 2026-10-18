# FSBL packaging regression tests

`python tools/test_fsbl_image.py` exercises aligned payload acceptance and rejects
the old unaligned default, mismatched reset entry, modified/truncated payload,
wrong ROM image length and wrong checksum. It uses the actual built FSBL bytes.
Run `python tools/build_ota.py` first to generate the signed FSBL under `Build/ota/LED_2_FSBL/`.
