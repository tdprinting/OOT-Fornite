"""Measure authored furniture against engine Adult Link clearance, not camera perspective.

The figure is an engine clearance reference, not an extracted ROM mesh or a
claim about the maximum extent of Link's animated hat/weapon.
"""
import json,sys
from pathlib import Path
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE))
import world,kit,geom
ROOT=HERE.parents[2]
old_h=kit.H;kit.H=lambda *args:0
measurements={}
for name,framed in [('table',True),('stool',True),('bed',True),('shelf',True),('barrel',False)]:
    w=geom.World();args=(w,kit.Frame(0,0),0,0,0) if framed else (w,0,0,0)
    world.scaled_furniture(getattr(kit,name),framed)(*args)
    points=[p for t in w.tris for p in t.p]
    dimensions=[max(p[i] for p in points)-min(p[i] for p in points) for i in range(3)]
    measurements[name]={'width':round(dimensions[0],2),'height':round(dimensions[1],2),'depth':round(dimensions[2],2)}
    for a,b,c,d,e,f,_ in w.props:
        assert e-b<=dimensions[1]+.001,'collision proxy must scale with its visible furniture'
assert 28<=measurements['table']['height']<=34
assert 15<=measurements['stool']['height']<=23
assert 65<=measurements['bed']['depth']<=85
assert kit.STOREY==270 and kit.DOOR_H==215,'shared map defaults must remain intact'
w=geom.World();world.rift_house(w,0,0,620,620,storeys=2,name='Scale audit house')
assert any(abs(y-w.buildings[-1][4]-170)<.001 for x,y,z,n in w.loot if 'upstairs' in n)
assert kit.STOREY==270 and kit.DOOR_H==215,'scoped house dimensions must be restored'
kit.H=old_h
report={'normal_actor_scale':.01,'adult_ceiling_clearance':56,'adult_wall_radius':18,
        'adult_power_scale':1.35,'ordinary_storey':170,'ordinary_door_height':140,
        'door_width_preserved_for_navigation':240,'furniture':measurements,
        'note':'56 is the engine Adult Link ceiling-check height, not a measured animated mesh bounding box. No ROM model is bundled.'}
out=ROOT/'assets/maps/hyrule_riftlands';out.mkdir(parents=True,exist_ok=True)
(out/'scale-audit.json').write_text(json.dumps(report,indent=2))
items=[('Adult clearance',56),('Table',measurements['table']['height']),('Stool',measurements['stool']['height']),
       ('Bed top',measurements['bed']['height']),('Shelf',measurements['shelf']['height']),('Door',140)]
svg=['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1100 490">',
 '<rect width="1100" height="490" fill="#eef1e6"/>',
 '<g font-family="sans-serif" fill="#203d32"><text x="40" y="45" font-size="27">Riftlands / Adult Link scale check</text>',
 '<text x="40" y="77" font-size="16">Engine units; normal actor scale 0.01. The 1.35 multiplier is temporary Adult Power.</text>',
 '<text x="40" y="102" font-size="15">Green figure represents the 56-unit engine clearance, not the animated ROM mesh.</text>',
 '<path d="M40 405H1060" stroke="#809388"/>']
for i,(label,h) in enumerate(items):
    x=70+i*175;y=405-h*1.9
    svg.append(f'<rect x="{x}" y="{y}" width="65" height="{h*1.9}" rx="5" fill="{"#398564" if i==0 else "#b29164"}"/>')
    svg.append(f'<text x="{x}" y="{y-12}" font-size="17">{h:g} units</text><text x="{x-10}" y="440" font-size="17">{label}</text>')
svg.append('</g></svg>')
(out/'scale-comparison.svg').write_text('\n'.join(svg))
print(json.dumps(report,indent=2))
