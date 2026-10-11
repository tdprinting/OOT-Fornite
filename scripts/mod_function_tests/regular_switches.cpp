// Layer 2b (scripts/test_mod_functions.py): the regular game's settings table (Core.inc): one key per switch, unique, non-empty, sane defaults.
// EXTRACT_SPAN mod/Royale/features/Core.inc  enum RegularFeature | };
#include <cassert>
#include <cstring>
// @EXTRACTED
int main() {
    static_assert(sizeof(kRegularSettings) / sizeof(kRegularSettings[0]) == kRegCount, "one setting per switch");
    for (int i = 0; i < kRegCount; i++) {
        assert(kRegularSettings[i].key && kRegularSettings[i].key[0]);
        assert(kRegularSettings[i].fallback >= 0 && kRegularSettings[i].fallback <= 2);
        for (int j = 0; j < i; j++) assert(std::strcmp(kRegularSettings[i].key, kRegularSettings[j].key) != 0);
    }
    return 0;
}
