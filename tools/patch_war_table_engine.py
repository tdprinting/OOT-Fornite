"""Author the native-menu integration patch against the already-patched engine.
Only used when authoring; release builds apply the resulting .patch normally.
"""
from pathlib import Path
import difflib

root = Path(__file__).resolve().parents[1]
engine = root / 'third_party/Shipwright-Android'
changes = {}

def change(path, before, after):
    p = engine / path
    old = p.read_text(encoding='utf-8')
    if before not in old:
        raise RuntimeError('Missing patch anchor: '+path+' / '+before[:60])
    if path not in changes:
        changes[path] = old
    p.write_text(old.replace(before, after, 1), encoding='utf-8', newline='\n')

change('soh/soh/SaveManager.h', '    void InitFile(bool isDebug);',
'''    void InitFile(bool isDebug);
    // BR uses its own disk file, while retaining the engine's valid slot-0 ID.
    // No adventure file is renamed, deleted or written through this route.
    void UseBattleRoyaleFile() { battleRoyaleFile = true; }
''')
change('soh/soh/SaveManager.h', '    std::filesystem::path GetFileName(int fileNum);',
'''    bool battleRoyaleFile = false;
    std::filesystem::path GetFileName(int fileNum);''')
change('soh/soh/SaveManager.cpp', '    return sSavePath / ("file" + std::to_string(fileNum + 1) + ".sav");',
'''    if (battleRoyaleFile && fileNum == 0) return sSavePath / "battle-royale.sav";
    return sSavePath / ("file" + std::to_string(fileNum + 1) + ".sav");''')
change('soh/soh/SaveManager.cpp', '    return sSavePath / ("file" + std::to_string(fileNum + 1) + ".temp");',
'''    if (battleRoyaleFile && fileNum == 0) return sSavePath / "battle-royale.temp";
    return sSavePath / ("file" + std::to_string(fileNum + 1) + ".temp");''')
change('soh/src/overlays/gamestates/ovl_file_choose/z_file_choose.c', 'void FileChoose_Main(GameState* thisx) {',
'''extern void Royale_NativeMenuBoot(GameState* state);
void FileChoose_Main(GameState* thisx) {
    // Native BR startup owns this state, including save errors and retry.
    Royale_NativeMenuBoot(thisx);
    return;
''')
change('soh/src/overlays/gamestates/ovl_title/z_title.c',
       'SET_NEXT_GAMESTATE(&this->state, Opening_Init, OpeningContext);',
       'SET_NEXT_GAMESTATE(&this->state, FileChoose_Init, FileChooseContext);')
change('soh/src/overlays/gamestates/ovl_title/z_title.c', '    this->exit = false;', '    this->exit = true; // boot directly into the native Battle Royale front end')
change('soh/soh/SohGui/SohGui.cpp', '    gui->SetMenu(mSohMenu);',
'''    // Settings services still exist, but the port menu is never registered or rendered.
    // The native War Table owns Start, Escape, F1, and controller navigation.
    mSohMenu->Hide();
    gui->SetMenu(nullptr);''')
# Prevent both the inventory and debug inventory from being opened by another hook.
change('soh/src/code/z_kaleido_scope_call.c', 'void KaleidoScopeCall_Update(PlayState* play) {',
'''void KaleidoScopeCall_Update(PlayState* play) {
    play->pauseCtx.state = 0;
    play->pauseCtx.debugState = 0;
    return; // Battle Royale uses its own native Start menu.
''')
change('soh/src/code/z_kaleido_scope_call.c', 'void KaleidoScopeCall_Draw(PlayState* play) {',
'''void KaleidoScopeCall_Draw(PlayState* play) {
    return; // No original inventory screen in this edition.
''')
# The character portrait is drawn with the game's actual Link model on its own framebuffer.
change('soh/src/code/z_player_lib.c',
       '(GPACK_RGBA5551(0, 0, 0, 1) << 16) | GPACK_RGBA5551(0, 0, 0, 1)',
       '(GPACK_RGBA5551(0, 0, 0, 0) << 16) | GPACK_RGBA5551(0, 0, 0, 0)')

# Native overlay covers the world HUD while the War Table is open.
change('soh/src/code/z_play.c', 'void Play_DrawOverlayElements(PlayState* play) {',
'''extern int Royale_NativeMenuHideHud(void);
void Play_DrawOverlayElements(PlayState* play) {
    if (Royale_NativeMenuHideHud()) return;
''')
# A malformed dedicated save must remain untouched, with an error on our native screen.
change('soh/soh/SaveManager.cpp',
       '    } catch (const std::exception& e) {\n        input.close();',
'''    } catch (const std::exception& e) {
        input.close();
        if (battleRoyaleFile && fileNum == 0) {
            saveMtx.unlock();
            throw; // Native startup reports this without renaming or deleting the file.
        }''')

patch = []
for name, before in changes.items():
    after = (engine/name).read_text(encoding='utf-8')
    patch.append('diff --git a/'+name+' b/'+name+'\n')
    patch.extend(difflib.unified_diff(before.splitlines(True),after.splitlines(True),'a/'+name,'b/'+name))
(root/'patches/0025-native-war-table.patch').write_text(''.join(patch),encoding='utf-8',newline='\n')
print('Wrote patches/0025-native-war-table.patch')
