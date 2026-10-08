# Native menu index

Read the relevant fragment, then immediate callers only. These files are included
once by RoyaleWarTable.h in this order; do not compile them separately.

| Task | File |
| --- | --- |
| State, page controls, settings and actions | Controls.inc |
| Matrices, graphics state, tiles/fonts and map/portrait caching | Painter.inc |
| Visible pages and panels | Draw.inc |
| Music, lifecycle, controller/keyboard/touch navigation | Input.inc |
| Native save preparation and recovery | Save.inc |

New standalone gameplay UI: features/OotVitals.inc and features/VictoryCrown.inc.
Model authoring: tools/scenery/build_playtest_models.py. Generated arrays belong
in shared/playtest_models.h; avoid reading them for behavioral tasks.
