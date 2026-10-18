# IMX335 middleware mode adaptation

RT60 failed on-board image validation (noise reported on 2026-10-18). This
experimental file is excluded from RT30 R2, which uses the original SDK source.

Accepts 1296x972 as local resolution ID 7 and keeps 2592x1944 as SDK ID 6.
Sensor info uses the actual initialized mode and corresponding exposure limit.
For binning, ISP statistics cover a central 648x486 area starting at (324,242),
which fits the smaller sensor frame. Full-resolution mode retains its tuning
file's original area. The existing tuning coefficients are retained; image
quality and recognition precision in binning mode need board validation.
