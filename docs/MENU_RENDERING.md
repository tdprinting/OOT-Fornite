# Native menu rendering

The shared War Table painter now converts its floating-point identity through
`Matrix_MtxFToMtx`, just like the engine's orthographic projection. The port's
`guMtxIdent` writes raw float bytes, while Fast3D decodes packed fixed-point
matrices. Loading those bytes as the menu modelview gives a zero homogeneous-w
column and clips the menu geometry, leaving the frame clear visible while input
and sounds continue.

Changed source: `mod/Royale/war_table/Painter.inc`. Regression:
`python scripts/test_war_table_matrix.py` (accepts a C++ compiler argument).
The regression checks packed matrix decoding and representative menu vertices;
`python scripts/check_mod_layout.py` checks feature layout.

Windows syntax checks use a read-only overlay of the complete pinned patch stack
and sibling dependency headers. Android compilation is verified by game-build CI. No device
visual verification has been performed. Next: build the Android game and check
cold startup, Start pause/reopen, and page navigation on Odin at native and
interpolated frame rates.
