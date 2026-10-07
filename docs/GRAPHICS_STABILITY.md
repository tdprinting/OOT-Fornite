# Cloud and rendering stability fix

The supplied IMG_9977.mov shows camera-following clouds and graphics instability during island movement and storm entry.

- Both cloud styles now use wind-driven world positions and fixed world heights. Tiled clouds fade before recycling. A fixed regional matrix keeps intermediate rendered frames from moving camera-relative cloud vertices with the interpolated camera.
- Plants and ground patches have stable interpolation paths based on mesh and position. Camera culling no longer changes which previous object a visible instance interpolates against.
- Sky, fog, light, storm and particle passes have separate interpolation identities. Sky matrix allocation checks the frame arena and records an explicit world matrix rather than applying the dummy actor's transform.
- Water-grid origins have identities based on the snapped grid coordinate. Crossing a cell does not blend the old origin with newly generated vertices.
- Negative waves remain above the opaque water-level terrain mesh, preventing the surface from alternating in front of and behind it.

The graphics regression executable checks world cloud anchoring under horizontal and vertical camera travel, wind motion, invisible wrap transitions and water separation. A full game compilation is also required; server tests alone do not validate engine integration.

Device verification: use the same island, sprint and rotate the camera with both sky styles, approach shores, enter/leave the storm, change weather, and compare 20 FPS with higher interpolation frame rates. Clouds should drift with wind while keeping their world height; foliage/ground must not stretch toward newly culled neighbours; water should remain visible across grid boundaries. Check dawn/night and skydiving too. Source fixes and successful compilation do not establish that every artifact in the recording is resolved on the device.
