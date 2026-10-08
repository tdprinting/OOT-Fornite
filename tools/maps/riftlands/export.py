"""python tools/maps/riftlands/export.py --name riftlands.

Load Riftlands modules before the shared exporter; its geometry/collision/native
texture packing contracts stay identical to Kingdom. Additional placement data
comes from the same layout used by Blender.
"""
import sys, importlib.util, json
from pathlib import Path
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE))
import world, terrain, layout
spec=importlib.util.spec_from_file_location('authored_export',HERE.parent/'kingdom'/'export.py')
export=importlib.util.module_from_spec(spec);spec.loader.exec_module(export)

if __name__=='__main__':
    if '--name' not in sys.argv:sys.argv+=['--name','riftlands']
    report=export.main()
    import props
    prop_instances=list(props.PLACEMENTS)
    root=HERE.parents[2]
    header=root/'shared'/'riftlands_data.h'
    with header.open('a') as f:
        f.write('namespace royale { namespace riftlands {\nstruct Pool { float x0,x1,z0,z1,y; };\n')
        export.arr(f,'Pool','kPools',layout.POOLS)
        f.write('struct Station { float x,z; };\n')
        export.arr(f,'Station','kAllyStations',layout.ALLY_SPOTS)
        export.arr(f,'Station','kCartStations',layout.CART_SPOTS)
        export.arr(f,'Station','kAccessSites',layout.ACCESS_SPOTS)
        f.write('struct Encounter { float x,z; int boss; };\n')
        export.arr(f,'Encounter','kBossSites',layout.BOSS_SPOTS)
        f.write('struct Door { float x,z,y; const char* house; };\n')
        export.arr(f,'Door','kDoors',[(round(x),round(z),round(y),export.quote(n)) for x,z,y,n in report['world'].doors])
        f.write('} }\n')
    out=root/'assets'/'maps'/'hyrule_riftlands';out.mkdir(parents=True,exist_ok=True)
    (out/'export-report.json').write_text(json.dumps({k:v for k,v in report.items() if k not in ('world','heights')},indent=2))
    # Reproducible manifest consumed by the authoring audit and human review.
    (out/'placement.json').write_text(json.dumps({'regions':layout.POIS,'water_boxes':layout.POOLS,'roads':layout.ROADS,'props':prop_instances,
       'allies':layout.ALLY_SPOTS,'buggies':layout.CART_SPOTS,'spawn':[0,1500],
       'storm_circle':{'center':[0,0],'radius':6500},'run_speed':100,'buggy_max_speed':420},indent=2))
