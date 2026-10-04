// OOT Royale: milestone 1 stub. Registers hooks and logs them. No gameplay yet.
// Copied or symlinked into third_party/Shipwright/soh/soh/Enhancements/Royale/ by scripts/link_mod.sh (a copy).
// NOTE: not yet compiled against Shipwright (needs a full SoH build); written against tag 9.2.3.
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include <spdlog/spdlog.h>

static void RegisterRoyaleMod() {
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnLoadGame>(
        [](int32_t fileNum) { SPDLOG_INFO("[Royale] OnLoadGame file={}", fileNum); });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>(
        [](int16_t sceneNum) { SPDLOG_INFO("[Royale] OnSceneInit scene={}", sceneNum); });
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnPlayerHealthChange>(
        [](int16_t amount) { SPDLOG_INFO("[Royale] OnPlayerHealthChange amount={}", amount); });
}

static RegisterShipInitFunc royaleInit(RegisterRoyaleMod);
