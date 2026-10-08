"""Local syntax check with the pinned engine and available Windows dependency headers."""
from pathlib import Path
import os, shutil, subprocess, xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[1]
engine=Path(os.environ.get('ROYALE_ENGINE_SOURCE',str(root/'third_party/Shipwright-Android')))
deps=Path(os.environ.get('ROYALE_WINDOWS_DEPS',str(root.parent.parent/'Shipwright/build/x64')))
project=deps/'soh/soh.vcxproj'
ns={'m':'http://schemas.microsoft.com/developer/msbuild/2003'}
tree=ET.parse(project)
includes=max((e.text or '' for e in tree.findall('.//m:AdditionalIncludeDirectories',ns)),key=len).split(';')
definitions=next(e.text for e in tree.findall('.//m:PreprocessorDefinitions',ns) if 'CVAR_PREFIX_SETTING' in (e.text or '') and '\\"' not in e.text).split(';')
own=[root/'shared',root/'client',root/'server',root/'mod/Royale',root/'third_party/enet/include',engine/'soh/include',engine/'soh/src',engine/'soh/assets',engine/'soh',engine/'libultraship/include',engine/'libultraship/src']
own += [engine/'libultraship/src'/p for p in ['utils','utils/binarytools','log','debug','config','resource','resource/type','public','public/libultra','public/bridge','graphic','graphic/Fast3D','graphic/Fast3D/U64/PR']]
includes=[str(p) for p in own]+[p for p in includes if p and not p.startswith('%') and '/Shipwright/soh' not in p.replace('\\','/') and '/Shipwright/libultraship' not in p.replace('\\','/')]
overlay=root/'war-table-compile/pinned-engine'
if overlay.is_dir():
    # libultra.h uses a relative include for libultra/gbi.h. An unpatched isolated
    # checkout must enter the overlay here, before that relative include wins.
    entry=engine/'libultraship/include/libultraship/libultra.h'
    target=overlay/'libultraship/include/libultraship/libultra.h'
    if entry.is_file() and not target.exists():
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(entry,target)
    # Relative includes of a patched gbi.h must see its pinned companions.
    # Adding this directory to /I would shadow CRT time.h and stdio.h.
    for header in (engine/'libultraship/include/libultraship/libultra').glob('*.h'):
        target=overlay/'libultraship/include/libultraship/libultra'/header.name
        if not target.exists(): shutil.copyfile(header,target)
    for header in (engine/'soh/soh/Enhancements/game-interactor').rglob('*.h'):
        target=overlay/header.relative_to(engine)
        if not target.exists():
            target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(header,target)
    includes=[str(overlay/'soh/include'),str(overlay/'soh'),str(overlay/'libultraship/include'),str(overlay/'libultraship/src'),str(overlay/'libultraship/src/graphic/Fast3D')]+includes
build=root/'war-table-compile';build.mkdir(exist_ok=True)
args=['/nologo','/std:c++20','/Zs','/bigobj','/EHsc','/utf-8','/D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS','/D_SILENCE_ALL_CXX20_DEPRECATION_WARNINGS']
for d in definitions:
    if d and not d.startswith('%') and not d.startswith('CMAKE_INTDIR'):
        args.append('/D'+d.replace('"','\\"'))
args += ['/I"'+p+'"' for p in includes]
source=Path(os.environ.get('ROYALE_COMPILE_SOURCE',str(root/'mod/Royale/RoyaleMod.cpp')))
args += ['"'+str(source)+'"']
(build/'check.rsp').write_text('\n'.join(args),encoding='utf-8')
vcvars=Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat')
(build/'check.cmd').write_text('@echo off\ncall "'+str(vcvars)+'" >nul\ncl @"'+str(build/'check.rsp')+'" > "'+str(build/'compile.log')+'" 2>&1\nexit /b %errorlevel%\n')
result=subprocess.run(['cmd.exe','/c',str(build/'check.cmd')],cwd=root)
print((build/'compile.log').read_text(errors='replace')[-24000:])
raise SystemExit(result.returncode)
