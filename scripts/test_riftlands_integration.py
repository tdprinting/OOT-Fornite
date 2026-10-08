"""Guard first-load collision, map switches and export packaging contracts."""
from pathlib import Path
import json,struct
root=Path(__file__).resolve().parents[1]
bridge=(root/'mod/Royale/features/EngineBridge.inc').read_text()
header=bridge[bridge.index('CollisionHeader* FortniteHeader()'):bridge.index('} // namespace')]
assert 'OnRiftlandsTerrain()' not in header,'scene loading must use loaded terrain ID, never previous gPlayState'
assert header.count('fn::gTerrainMapId == royale::kRiftlandsMapIndex')==2
assert 'gFortniteWater[count++]=ocean' in header and 'numWaterBoxes=count' in header
assert 'gFortniteHeader.numWaterBoxes = 1' in header,'switching to an older map resets river boxes'
source=(root/'shared/fortnite_map.h').read_text()
assert 'if(gTerrainMapId==11) for(const auto& b:riftlands::kPools)' in source
assets=root/'assets/maps/hyrule_riftlands'
placement=json.loads((assets/'placement.json').read_text())
assert len(placement['regions'])==24 and len(placement['allies'])==4 and len(placement['buggies'])==4
assert len({p['family'] for p in placement['props']})==30
report=json.loads((assets/'export-report.json').read_text())
assert report['collision_vertices']<65536 and report['collision_triangles']<65536
assert (assets/'hyrule_riftlands.blend').is_file()
data=(assets/'hyrule_riftlands.glb').read_bytes();magic,version,length=struct.unpack_from('<4sII',data)
assert magic==b'glTF' and version==2 and length==len(data)
print('Riftlands integration: first-load predicates, water reset, prop manifest and GLB packaging passed')
