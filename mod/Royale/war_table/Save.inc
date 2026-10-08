bool WarPrepareSave() {
    try {
        SaveManager* saves=SaveManager::Instance;
        if(!saves){gWar.bootError="The save service is not ready. Press A to retry.";return false;}
        saves->ThreadPoolWait();
        const auto folder=std::filesystem::path(Ship::Context::GetPathRelativeToAppDirectory("Save"));
        std::filesystem::create_directories(folder);
        const auto path=folder/"battle-royale.sav";
        if(std::filesystem::exists(path)) {
            std::ifstream input(path);
            const auto json=nlohmann::json::parse(input);
            if(json.value("version",0)!=1 || !json.contains("sections") || !json["sections"].contains("base") ||
               !json["sections"]["base"].contains("data") || !json["sections"]["base"]["data"].is_object()) {
                gWar.bootError="The Battle Royale save is not a supported save file. It has been preserved. Restore a backup of Save/battle-royale.sav, then retry.";
                return false;
            }
            const int version=json["sections"]["base"].value("version",0);
            if(version<1||version>4){gWar.bootError="This Battle Royale save needs a different game version. The file has been preserved.";return false;}
        }
        saves->UseBattleRoyaleFile();
        if(!std::filesystem::exists(path)) {
            saves->InitFile(false);
            gSaveContext.fileNum=0;
            gSaveContext.ship.quest.id=QUEST_BATTLEROYALE;
            gSaveContext.linkAge=LINK_AGE_ADULT;
            gSaveContext.entranceIndex=ENTR_TEMPLE_OF_TIME_ENTRANCE;
            gSaveContext.savedSceneNum=SCENE_TEMPLE_OF_TIME;
            gSaveContext.cutsceneIndex=0;
            gSaveContext.healthCapacity=gSaveContext.health=7*16;
            gSaveContext.equips.buttonItems[0]=ITEM_SWORD_MASTER;
            gSaveContext.inventory.equipment|=OWNED_EQUIP_FLAG(EQUIP_TYPE_SWORD,EQUIP_INV_SWORD_MASTER);
            gSaveContext.equips.equipment=(gSaveContext.equips.equipment&~0xf)|EQUIP_VALUE_SWORD_MASTER;
            saves->SaveFile(0);saves->ThreadPoolWait();
            if(!std::filesystem::exists(path)){gWar.bootError="Could not write Save/battle-royale.sav. Check free space and folder access, then retry.";return false;}
        }
        // Exercise the engine section loaders before committing to the play-state transition.
        // The native-save patch propagates errors and preserves the original file.
        saves->LoadFile(0);
        CVarSetInteger(CVAR_DEVELOPER_TOOLS("DebugEnabled"),0);
        CVarSetInteger(ROYALE_CVAR("BRFile0"),1);
        gWar.bootError.clear();return true;
    } catch(const std::exception& e) {
        gWar.bootError=std::string("The Battle Royale save could not be opened. Existing saves were preserved. ")+e.what();
        return false;
    }
}
