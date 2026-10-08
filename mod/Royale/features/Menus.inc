// ---- the Battle Royale menu --------------------------------------------------------------------------------------------

#define ROYALE_CVAR(name) CVAR_SETTING("Royale." name)

struct UiState {
    char name[24] = "Link";
    char address[64] = "";
    int port = royale::net::kDefaultPort;
    bool waitingRoom = true;
    int botDifficulty = 1; // 0 easy, 1 normal, 2 hard
    int playerLimit = royale::kMaxPlayers; // the host's slider
    bool autoStart = true;                 // the lobby starts the match by itself after two minutes
    int mapId = 0;                         // which place to play (host)
    bool majorBoss = true;                 // the map's major boss arrives halfway through (host)
    int weatherSeason = royale::kSeasonRandom;   // 0-3 or kSeasonRandom (host)
    int weatherIntensity = 60;             // 0 = no weather (host)
    int weatherChange = 50;                // how often the weather changes (host)
    int weatherDensity = 100;              // particles drawn on this screen, per cent (local)
    int foliage = 100;                     // grass and trees scattered around, per cent (local)
    bool clothOn = true;                   // cloth physics on hats and gliders at all (local)
    bool windOn = true;                    // a breeze that moves cloth, grass and trees (local)
    bool windStreaks = true;               // streaks in the air that show the wind's direction (local)
    int windStrength = 100;                // how strong the breeze is, per cent (local)
    int clothPhysics = 100;                // how much cloth and wind physics the cap and glider get, per cent (local)
    int musicMode = 0;                     // match music: 0 the game's, 1 random from the music folder, 2 none (local)
    int skin = 0;          // index into royale::kSkins, or royale::kCustomSkin
    float customTunic[3] = { 0.12f, 0.41f, 0.11f };
    bool loaded = false;
    bool showPosition = false;
    std::string error;
    std::string exportMsg;   // result of the last map export
    std::vector<std::string> localAddresses;
    int addressAge = 1 << 30; // frames since localAddresses was refreshed
};

uint32_t SelectedTunic(const UiState& ui) {
    if (ui.skin == royale::kCustomSkin) {
        auto byte = [](float f) { return static_cast<uint8_t>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };
        return royale::PackRgb(byte(ui.customTunic[0]), byte(ui.customTunic[1]), byte(ui.customTunic[2]));
    }
    return royale::SkinRgb(ui.skin);
}

UiState& Ui() {
    static UiState ui;
    if (!ui.loaded) {
        ui.loaded = true;
        std::snprintf(ui.name, sizeof(ui.name), "%s", CVarGetString(ROYALE_CVAR("Name"), "Link"));
        std::snprintf(ui.address, sizeof(ui.address), "%s", CVarGetString(ROYALE_CVAR("Address"), ""));
        ui.port = CVarGetInteger(ROYALE_CVAR("Port"), royale::net::kDefaultPort);
        ui.waitingRoom = CVarGetInteger(ROYALE_CVAR("WaitingRoom"), 1) != 0;
        ui.botDifficulty = std::clamp(CVarGetInteger(ROYALE_CVAR("BotDifficulty"), 1), 0, 2);
        gSession.SetBotDifficulty(static_cast<royale::BotDifficulty>(ui.botDifficulty));
        ui.playerLimit = std::clamp(CVarGetInteger(ROYALE_CVAR("PlayerLimit"), royale::kMaxPlayers), royale::kMinPlayers, royale::kMaxPlayers);
        ui.autoStart = CVarGetInteger(ROYALE_CVAR("AutoStart"), 1) != 0;
        ui.mapId = CVarGetInteger(ROYALE_CVAR("Map"), royale::kKingdomMapIndex);   // Hyrule Kingdom is the main map
        if (!royale::IsPlayableMap(ui.mapId)) ui.mapId = 0;
        ui.majorBoss = CVarGetInteger(ROYALE_CVAR("MajorBoss"), 1) != 0;
        ui.weatherSeason = std::clamp(CVarGetInteger(ROYALE_CVAR("WeatherSeason"), royale::kSeasonRandom), 0, static_cast<int>(royale::kSeasonRandom));
        ui.weatherIntensity = std::clamp(CVarGetInteger(ROYALE_CVAR("WeatherIntensity"), 60), 0, 100);
        ui.weatherChange = std::clamp(CVarGetInteger(ROYALE_CVAR("WeatherChange"), 50), 0, 100);
        ui.weatherDensity = std::clamp(CVarGetInteger(ROYALE_CVAR("WeatherDensity"), 100), 0, 200);
        gWeatherDensity = ui.weatherDensity / 100.0f;
        ui.foliage = std::clamp(CVarGetInteger(ROYALE_CVAR("Foliage"), 100), 0, 200);
        gFoliage = ui.foliage / 100.0f;
        ui.clothPhysics = std::clamp(CVarGetInteger(ROYALE_CVAR("ClothPhysics"), 100), 0, 200);
        ui.clothOn = CVarGetInteger(ROYALE_CVAR("ClothOn"), 1) != 0;
        ui.windOn = CVarGetInteger(ROYALE_CVAR("WindOn"), 1) != 0;
        ui.windStrength = std::clamp(CVarGetInteger(ROYALE_CVAR("WindStrength"), 100), 0, 200);
        ui.windStreaks = CVarGetInteger(ROYALE_CVAR("WindStreaks"), 1) != 0;
        gWindStreaks = ui.windStreaks;
        gWindOn = ui.windOn;
        gWindScale = ui.windStrength / 100.0f;
        gClothScale = ui.clothOn ? ui.clothPhysics / 100.0f : 0.0f;
        gSkyOot = CVarGetInteger(ROYALE_CVAR("SkyOot"), 1) != 0;
        gSkyStars = CVarGetInteger(ROYALE_CVAR("SkyStars"), 1) != 0;
        gSkyBodies = CVarGetInteger(ROYALE_CVAR("SkyBodies"), 1) != 0;
        gSkyClouds = std::clamp(CVarGetInteger(ROYALE_CVAR("SkyClouds"), 100), 0, 200) / 100.0f;
        gFogAmount = std::clamp(CVarGetInteger(ROYALE_CVAR("FogAmount"), 100), 0, 200) / 100.0f;
        gGroundPuddles = CVarGetInteger(ROYALE_CVAR("GroundPuddles"), 1) != 0;
        gGroundSnow = CVarGetInteger(ROYALE_CVAR("GroundSnow"), 1) != 0;
        gGroundFrost = CVarGetInteger(ROYALE_CVAR("GroundFrost"), 1) != 0;
        gGroundLeaves = CVarGetInteger(ROYALE_CVAR("GroundLeaves"), 1) != 0;
        gGroundMerge = CVarGetInteger(ROYALE_CVAR("GroundMerge"), 1) != 0;
        gGroundTrails = CVarGetInteger(ROYALE_CVAR("GroundTrails"), 1) != 0;
        gGroundDeform = std::clamp(CVarGetInteger(ROYALE_CVAR("GroundDeform"), 100), 0, 200) / 100.0f;
        gGroundAmount = std::clamp(CVarGetInteger(ROYALE_CVAR("GroundAmount"), 100), 0, 200) / 100.0f;
        gHitFlash = CVarGetInteger(ROYALE_CVAR("HitFlash"), 1) != 0;
        gHitParticles = CVarGetInteger(ROYALE_CVAR("HitParticles"), 1) != 0;
        gHitBlood = CVarGetInteger(ROYALE_CVAR("HitBlood"), 1) != 0;
        gHitScaled = CVarGetInteger(ROYALE_CVAR("HitScaled"), 1) != 0;
        gHitOnSelf = CVarGetInteger(ROYALE_CVAR("HitOnSelf"), 1) != 0;
        gHitReact = CVarGetInteger(ROYALE_CVAR("HitReact"), 1) != 0;
        gHitAmount = std::clamp(CVarGetInteger(ROYALE_CVAR("HitAmount"), 100), 0, 200) / 100.0f;
        gWaterDetail = std::clamp(CVarGetInteger(ROYALE_CVAR("WaterDetail"), 1), 0, 2);
        gWaterWaves = std::clamp(CVarGetInteger(ROYALE_CVAR("WaterWaves"), 100), 0, 200) / 100.0f;
        gWaterFxOn = CVarGetInteger(ROYALE_CVAR("WaterFx"), 1) != 0;
        gWaterLife = CVarGetInteger(ROYALE_CVAR("WaterLife"), 1) != 0;
        gWaterWakes = CVarGetInteger(ROYALE_CVAR("WaterWakes"), 1) != 0;
        gWaterRain = CVarGetInteger(ROYALE_CVAR("WaterRain"), 1) != 0;
        gWaterSkyOn = CVarGetInteger(ROYALE_CVAR("WaterSky"), 1) != 0;
        gWaterBodiesRefl = CVarGetInteger(ROYALE_CVAR("WaterBodies"), 1) != 0;
        gWaterUnder = CVarGetInteger(ROYALE_CVAR("WaterUnder"), 1) != 0;
        gWaterCurrent = CVarGetInteger(ROYALE_CVAR("WaterCurrent"), 1) != 0;
        gWaterChop = std::clamp(CVarGetInteger(ROYALE_CVAR("WaterChop"), 60), 0, 200) / 100.0f;
        gWaterClarity = std::clamp(CVarGetInteger(ROYALE_CVAR("WaterClarity"), 100), 0, 200) / 100.0f;
        gWaterGlints = CVarGetInteger(ROYALE_CVAR("WaterGlints"), 1) != 0;
        gWaterGlintAmt = std::clamp(CVarGetInteger(ROYALE_CVAR("WaterGlintAmt"), 100), 0, 200) / 100.0f;
        gWaterCaustics = CVarGetInteger(ROYALE_CVAR("WaterCaustics"), 1) != 0;
        gWaterCausticAmt = std::clamp(CVarGetInteger(ROYALE_CVAR("WaterCausticAmt"), 100), 0, 200) / 100.0f;
        gWaterFoamTex = CVarGetInteger(ROYALE_CVAR("WaterFoamTex"), 1) != 0;
        gWaterFoamAmt = std::clamp(CVarGetInteger(ROYALE_CVAR("WaterFoamAmt"), 100), 0, 200) / 100.0f;
        gWaterRipples = CVarGetInteger(ROYALE_CVAR("WaterRipples"), 1) != 0;
        LoadShadowSettings();
        gWaterUnderAmt = std::clamp(CVarGetInteger(ROYALE_CVAR("WaterUnderAmt"), 100), 0, 200) / 100.0f;
        ui.musicMode = std::clamp(CVarGetInteger(ROYALE_CVAR("MusicMode"), 0), 0, 2);
        gMusicMode = ui.musicMode;
        gSession.SetWeatherOptions({ static_cast<uint8_t>(ui.weatherSeason), static_cast<uint8_t>(ui.weatherIntensity), static_cast<uint8_t>(ui.weatherChange) });
        gSession.SelectMap(ui.mapId);
        gSession.SetMajorBoss(ui.majorBoss);
        gSession.SetPlayerLimit(ui.playerLimit);
        gSession.SetAutoStart(ui.autoStart ? royale::kLobbyAutoStartSec : 0.0f);
        ui.skin = std::clamp(CVarGetInteger(ROYALE_CVAR("Skin"), 0), 0, royale::kCustomSkin);
        const uint32_t saved = static_cast<uint32_t>(CVarGetInteger(ROYALE_CVAR("SkinColor"), static_cast<int>(royale::SkinRgb(0))));
        ui.customTunic[0] = royale::RgbR(saved) / 255.0f; ui.customTunic[1] = royale::RgbG(saved) / 255.0f; ui.customTunic[2] = royale::RgbB(saved) / 255.0f;
        gLocalTunic = SelectedTunic(ui);
        gSession.SetTunic(gLocalTunic);
    }
    return ui;
}

void SaveUi(const UiState& ui) {
    CVarSetString(ROYALE_CVAR("Name"), ui.name);
    CVarSetString(ROYALE_CVAR("Address"), ui.address);
    CVarSetInteger(ROYALE_CVAR("Port"), ui.port);
    CVarSetInteger(ROYALE_CVAR("WaitingRoom"), ui.waitingRoom ? 1 : 0);
    CVarSetInteger(ROYALE_CVAR("BotDifficulty"), ui.botDifficulty);
    CVarSetInteger(ROYALE_CVAR("Skin"), ui.skin);
    CVarSetInteger(ROYALE_CVAR("PlayerLimit"), ui.playerLimit);
    CVarSetInteger(ROYALE_CVAR("AutoStart"), ui.autoStart ? 1 : 0);
    CVarSetInteger(ROYALE_CVAR("Map"), ui.mapId);
    CVarSetInteger(ROYALE_CVAR("MajorBoss"), ui.majorBoss ? 1 : 0);
    CVarSetInteger(ROYALE_CVAR("WeatherSeason"), ui.weatherSeason);
    CVarSetInteger(ROYALE_CVAR("WeatherIntensity"), ui.weatherIntensity);
    CVarSetInteger(ROYALE_CVAR("WeatherChange"), ui.weatherChange);
    CVarSetInteger(ROYALE_CVAR("WeatherDensity"), ui.weatherDensity);
    CVarSetInteger(ROYALE_CVAR("Foliage"), ui.foliage);
    CVarSetInteger(ROYALE_CVAR("ClothPhysics"), ui.clothPhysics);
    CVarSetInteger(ROYALE_CVAR("ClothOn"), ui.clothOn ? 1 : 0);
    CVarSetInteger(ROYALE_CVAR("WindOn"), ui.windOn ? 1 : 0);
    CVarSetInteger(ROYALE_CVAR("WindStrength"), ui.windStrength);
    CVarSetInteger(ROYALE_CVAR("WindStreaks"), ui.windStreaks ? 1 : 0);
    CVarSetInteger(ROYALE_CVAR("MusicMode"), ui.musicMode);
    CVarSetInteger(ROYALE_CVAR("SkinColor"), static_cast<int>(SelectedTunic(ui)));
    Ship::Context::GetInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}

void RefreshLocalAddresses(UiState& ui, bool force) {
    if (!force && ui.addressAge < 600) { ui.addressAge++; return; }
    ui.localAddresses = royale::net::LocalIPv4Addresses();
    ui.addressAge = 0;
}

const char* CleanName(const char* name) { return name[0] ? name : "Link"; }

