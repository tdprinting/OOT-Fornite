"""Build a labeled QA contact sheet from actual Blender render outputs."""
from pathlib import Path
from PIL import Image, ImageDraw
OUT=Path(__file__).resolve().parents[3]/'assets/maps/hyrule_riftlands'
NAMES=['overview','top','castle','courtyard','crown_props','kakariko','deku','lake','bazaar','ranch','lodge','quarry','interior']
W,H=480,350
sheet=Image.new('RGB',(W*4,H*4),(228,232,230));draw=ImageDraw.Draw(sheet)
for index,name in enumerate(NAMES):
    im=Image.open(OUT/(name+'.png')).convert('RGB');im.thumbnail((W,H-28))
    x=(index%4)*W;y=(index//4)*H
    sheet.paste(im,(x+(W-im.width)//2,y+28))
    draw.text((x+12,y+8),name.replace('_',' ').title(),fill=(20,32,35))
sheet.save(OUT/'gallery.jpg',quality=92)
print(OUT/'gallery.jpg')
