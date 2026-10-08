"""Local syntax check with the pinned engine and available Windows dependency headers."""
from pathlib import Path
import os, subprocess, xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[1]
engine=root/'third_party/Shipwright-Android'
deps=Path(os.environ.get('ROYALE_WINDOWS_DEPS',str(root.parent.parent/'Shipwright/build/x64')))
project=deps/'soh/soh.vcxproj'
ns={'m':'http://schemas.microsoft.com/developer/msbuild/2003'}
tree=ET.parse(project)
includes=max((e.text or '' for e in tree.findall('.//m:AdditionalIncludeDirectories',ns)),key=len).split(';')
definitions=next(e.text for e in tree.findall('.//m:PreprocessorDefinitions',ns) if 'CVAR_PREFIX_SETTING' in (e.text or '') and '\\"' not in e.text).split(';')
own=[root/'shared',root/'client',root/'server',root/'mod/Royale',root/'third_party/enet/include',engine/'soh/include',engine/'soh/src',engine/'soh/assets',engine/'soh',engine/'libultraship/include',engine/'libultraship/src']
own += [engine/'libultraship/src'/p for p in ['utils','utils/binarytools','log','debug','config','resource','resource/type','public','public/libultra','public/bridge','graphic','graphic/Fast3D/U64/PR']]
includes=[str(p) for p in own]+[p for p in includes if p and not p.startswith('%') and '/Shipwright/soh' not in p.replace('\\','/') and '/Shipwright/libultraship' not in p.replace('\\','/')]
build=root/'war-table-compile';build.mkdir(exist_ok=True)
args=['/nologo','/std:c++20','/Zs','/bigobj','/EHsc','/utf-8','/D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS','/D_SILENCE_ALL_CXX20_DEPRECATION_WARNINGS']
for d in definitions:
    if d and not d.startswith('%') and not d.startswith('CMAKE_INTDIR'):
        args.append('/D'+d.replace('"','\\"'))
args += ['/I"'+p+'"' for p in includes]
args += ['"'+str(root/'mod/Royale/RoyaleMod.cpp')+'"']
(build/'check.rsp').write_text('\n'.join(args),encoding='utf-8')
vcvars=Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat')
(build/'check.cmd').write_text('@echo off\ncall "'+str(vcvars)+'" >nul\ncl @"'+str(build/'check.rsp')+'" > "'+str(build/'compile.log')+'" 2>&1\nexit /b %errorlevel%\n')
result=subprocess.run(['cmd.exe','/c',str(build/'check.cmd')],cwd=root)
print((build/'compile.log').read_text(errors='replace')[-24000:])
raise SystemExit(result.returncode)
