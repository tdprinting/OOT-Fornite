// ---- lobby music from a folder ----------------------------------------------------------------------------------------------
// Put .wav files in the "music" folder inside the game's data folder and they play in a shuffled loop while you wait in the lobby, then fade out when
// the countdown starts. (Only WAV is read: the game has no MP3/OGG decoder to hook into.) The folder is created on first use.
void Say(const std::string& text);
bool HeldGlowOn(bool self) {
    return self ? MapOption("HeldGlowSelf", false) : MapOption("HeldGlow", true);
}

struct LobbyMusic {
    std::vector<std::filesystem::path> tracks;
    std::vector<int16_t> pcm;   // a track just read, converted to the game's output format (stereo, 44.1 kHz), until it is handed to the mixer
    size_t next = 0;
    bool scanned = false;
    bool failed = false;
    bool playing = false;
    std::string nowPlaying;
    std::string status;         // shown in the menu: where the folder is, how many songs, and why one could not be read
};
LobbyMusic gLobbyMusic;


std::filesystem::path MusicFolder() { return std::filesystem::path(Ship::Context::GetPathRelativeToAppDirectory("music")); }

// A breadcrumb trail for crashes the phone gives no log for: each step is written to royale-trace.txt (next to the music folder) and closed
// again straight away, so the last line says how far the game got. Each run starts a new file; the one from the run before is kept as
// royale-trace-prev.txt so it is still there to read after the game is reopened.
char gTraceLast[160] = "(nothing yet)";   // the newest step, for the crash report

// Free memory in MB as Linux and Android report it, or -1. A phone that runs low closes the game with no message at all.
int FreeMemoryMb() {
    std::ifstream in("/proc/meminfo");
    std::string key;
    long kb = 0;
    std::string unit;
    while (in >> key >> kb >> unit) {
        if (key == "MemAvailable:") return static_cast<int>(kb / 1024);
    }
    return -1;
}

void Trace(const char* step) {
    static bool fresh = true;
    static std::string last;
    if (last == step) return;   // a step drawn every frame is written once
    last = step;
    std::snprintf(gTraceLast, sizeof(gTraceLast), "%s", step);
    const std::filesystem::path file(Ship::Context::GetPathRelativeToAppDirectory("royale-trace.txt"));
    if (fresh) {
        std::error_code ec;
        std::filesystem::rename(file, Ship::Context::GetPathRelativeToAppDirectory("royale-trace-prev.txt"), ec);
    }
    std::ofstream out(file, fresh ? std::ios::trunc : std::ios::app);
    fresh = false;
    if (out) {
        out << step;
        if (std::strncmp(step, "start:", 6) == 0) out << " (free memory " << FreeMemoryMb() << " MB)";
        out << "\n";
    }
}

// ---- crash report ---------------------------------------------------------------------------------------------------------------
// When the game dies from a native crash (a bad pointer, an abort), Android gives the player nothing to send. So a handler writes
// royale-crash.txt next to the music folder: which signal, where it faulted (library, offset and name for each step of the call stack) and the
// last step of the breadcrumb trail. The next time the Battle Royale menu opens it shows that text so it can be photographed, and the file stays
// until dismissed. The offsets are looked up against the symbols file that the game build publishes.
#ifdef __ANDROID__
char gCrashPath[600] = "";
struct sigaction gOldAction[6];
const int kCrashSignals[6] = { SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGTRAP };

struct CrashStack { void* pc[40]; int n; };
_Unwind_Reason_Code CrashUnwind(struct _Unwind_Context* ctx, void* arg) {
    CrashStack* st = static_cast<CrashStack*>(arg);
    const uintptr_t pc = _Unwind_GetIP(ctx);
    if (pc == 0) return _URC_NO_REASON;
    if (st->n >= 40) return _URC_END_OF_STACK;
    st->pc[st->n++] = reinterpret_cast<void*>(pc);
    return _URC_NO_REASON;
}
void CrashLine(int fd, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void CrashLine(int fd, const char* fmt, ...) {
    char buf[400];
    va_list args;
    va_start(args, fmt);
    const int n = std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n > 0) { const ssize_t ignored = write(fd, buf, std::min<int>(n, sizeof(buf) - 1)); (void)ignored; }
}
void CrashHandler(int sig, siginfo_t* info, void*) {
    static volatile sig_atomic_t busy = 0;
    if (!busy) {
        busy = 1;
        const int fd = open(gCrashPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            CrashLine(fd, "Version %s\nSignal %d code %d fault address %p thread %ld\nFeature running: %s\nLast step: %s\n", ROYALE_BUILD_VERSION, sig, info ? info->si_code : 0,
                      info ? info->si_addr : nullptr, static_cast<long>(syscall(SYS_gettid)), gFeature, gTraceLast);
            CrashStack st;
            st.n = 0;
            _Unwind_Backtrace(CrashUnwind, &st);
            for (int i = 0; i < st.n; i++) {
                Dl_info di;
                if (dladdr(st.pc[i], &di) && di.dli_fname != nullptr) {
                    const char* base = std::strrchr(di.dli_fname, '/');
                    CrashLine(fd, "#%d %s+0x%lx %s\n", i, base ? base + 1 : di.dli_fname,
                              static_cast<unsigned long>(reinterpret_cast<uintptr_t>(st.pc[i]) - reinterpret_cast<uintptr_t>(di.dli_fbase)), di.dli_sname ? di.dli_sname : "");
                } else {
                    CrashLine(fd, "#%d %p\n", i, st.pc[i]);
                }
            }
            close(fd);
        }
    }
    // Hand over to whoever was there before (Android's own crash reporter), so the game still ends the usual way.
    for (int i = 0; i < 6; i++) if (kCrashSignals[i] == sig) sigaction(sig, &gOldAction[i], nullptr);
    if (info != nullptr && info->si_code <= 0) raise(sig);   // sent by abort() and the like: send it again; a real fault just happens again on return
}
void InstallCrashReporter() {
    const std::string path = Ship::Context::GetPathRelativeToAppDirectory("royale-crash.txt");
    std::snprintf(gCrashPath, sizeof(gCrashPath), "%s", path.c_str());
    for (int i = 0; i < 6; i++) {
        struct sigaction sa;
        std::memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = CrashHandler;
        sa.sa_flags = SA_SIGINFO;
        sigemptyset(&sa.sa_mask);
        sigaction(kCrashSignals[i], &sa, &gOldAction[i]);
    }
}
#else
void InstallCrashReporter() {}
#endif

// A crash report from the last run, shown at the top of the Battle Royale menu until dismissed.
// Android's own record of why the last run ended (written by ExitReport.java at start), shown until dismissed.
void DrawExitReason() {
    static int state = 0;   // 0: not looked yet, 1: showing, 2: none or dismissed
    static std::string text;
    const std::filesystem::path file(Ship::Context::GetPathRelativeToAppDirectory("royale-exit-reason.txt"));
    if (state == 0) {
        std::ifstream in(file);
        std::stringstream ss;
        if (in) ss << in.rdbuf();
        text = ss.str();
        state = text.empty() ? 2 : 1;
    }
    if (state != 1) return;
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Android's note on why the game ended last time. Please send a photo of this to Claude:");
    ImGui::TextWrapped("%s", text.c_str());
    if (ImGui::Button("Dismiss Android note")) {
        std::error_code ec;
        std::filesystem::remove(file, ec);
        state = 2;
    }
    ImGui::Separator();
}

void DrawCrashReport() {
    static int state = 0;   // 0: not looked yet, 1: showing a crash, 2: none or dismissed, 3: showing a silent stop
    static std::string text;
    const std::filesystem::path file(Ship::Context::GetPathRelativeToAppDirectory("royale-crash.txt"));
    const std::filesystem::path prev(Ship::Context::GetPathRelativeToAppDirectory("royale-trace-prev.txt"));
    if (state == 0) {
        std::ifstream in(file);
        std::stringstream ss;
        if (in) ss << in.rdbuf();
        text = ss.str();
        state = text.empty() ? 2 : 1;
        if (state == 2) {
            // No crash report, but the last run's trail may end in the middle of a match start: the phone closed the game without a signal
            std::ifstream trail(prev);
            std::string line, lastLine;
            while (std::getline(trail, line)) if (!line.empty()) lastLine = line;
            if (std::strncmp(lastLine.c_str(), "start:", 6) == 0 && lastLine.find("match started") == std::string::npos) {
                text = lastLine;
                state = 3;
            }
        }
    }
    if (state == 1) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "The game closed unexpectedly last time. Please send a photo of this to Claude:");
        ImGui::TextWrapped("%s", text.c_str());
        if (ImGui::Button("Dismiss crash report")) {
            std::error_code ec;
            std::filesystem::remove(file, ec);
            state = 2;
        }
        ImGui::Separator();
    } else if (state == 3) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Last time the game stopped while starting a match, with no crash report (Android may have closed it, for example for memory). Please send a photo of this to Claude:");
        ImGui::TextWrapped("%s", text.c_str());
        if (ImGui::Button("Dismiss")) {
            std::error_code ec;
            std::filesystem::remove(prev, ec);
            state = 2;
        }
        ImGui::Separator();
    }
}

void QueueOotSongs();
// The menu is drawn on the render thread, but a scan also copies the game's soundfonts out of its audio data (CaptureOotFonts), which only the
// game thread may touch. So the menu asks for a scan with this flag and the game thread's frame update does it.
bool gScanRequested = false;
void ScanMusicFolder() {
    Trace("music scan: start");
    gLobbyMusic.tracks.clear();
    std::error_code ec;
    std::filesystem::create_directories(MusicFolder(), ec);
    for (const auto& e : std::filesystem::directory_iterator(MusicFolder(), ec)) {
        std::string ext = e.path().extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (e.is_regular_file(ec) && ext == ".wav") gLobbyMusic.tracks.push_back(e.path());
    }
    std::sort(gLobbyMusic.tracks.begin(), gLobbyMusic.tracks.end());
    std::mt19937 rng(std::random_device{}());
    std::shuffle(gLobbyMusic.tracks.begin(), gLobbyMusic.tracks.end(), rng);
    gLobbyMusic.next = 0;
    gLobbyMusic.scanned = true;
    gLobbyMusic.failed = false;
    gLobbyMusic.status = std::to_string(gLobbyMusic.tracks.size()) + " .wav song(s) found in " + MusicFolder().string();
    Trace("music scan: listed");
    QueueOotSongs();
    Trace("music scan: done");
}

// A WAV reader of our own. The SDL on the phone aborts the whole game on some perfectly good .wav files (a format chunk bigger than it
// expects, as in WAVE_FORMAT_EXTENSIBLE files, makes its fortified read stop the process), and a game that dies while loading a song is no use.
// This reads the chunks with every size checked against the file, understands 8, 16, 24 and 32-bit PCM and 32-bit float in mono or stereo (more
// channels: the first two), and gives back 16-bit samples the way SDL_LoadWAV does (free the result with SDL_FreeWAV). Returns nullptr and sets
// the SDL error text when the file is not usable.
Uint8* SafeLoadWav(const std::string& path, SDL_AudioSpec* spec, Uint8** audioBuf, Uint32* audioLen) {
    constexpr uintmax_t kMaxWavBytes = 400u * 1024u * 1024u;
    std::error_code ec;
    const uintmax_t fileSize = std::filesystem::file_size(path, ec);
    if (ec) { SDL_SetError("cannot open the file"); return nullptr; }
    if (fileSize < 44 || fileSize > kMaxWavBytes) { SDL_SetError("the file is empty or too big"); return nullptr; }
    std::vector<uint8_t> file(static_cast<size_t>(fileSize));
    {
        std::ifstream in(path, std::ios::binary);
        if (!in || !in.read(reinterpret_cast<char*>(file.data()), static_cast<std::streamsize>(file.size()))) { SDL_SetError("cannot read the file"); return nullptr; }
    }
    auto read16 = [&](size_t at) -> uint32_t { return at + 2 <= file.size() ? static_cast<uint32_t>(file[at] | (file[at + 1] << 8)) : 0u; };
    auto read32 = [&](size_t at) -> uint32_t { return at + 4 <= file.size() ? read16(at) | (read16(at + 2) << 16) : 0u; };
    if (std::memcmp(file.data(), "RIFF", 4) != 0 || std::memcmp(file.data() + 8, "WAVE", 4) != 0) { SDL_SetError("not a RIFF WAVE file"); return nullptr; }
    uint32_t tag = 0, channels = 0, rate = 0, bits = 0;
    size_t dataAt = 0, dataLen = 0;
    bool haveFmt = false, haveData = false;
    size_t pos = 12;
    while (pos + 8 <= file.size() && !haveData) {
        const size_t size = read32(pos + 4);
        const size_t body = pos + 8;
        const size_t avail = file.size() - body;
        if (std::memcmp(file.data() + pos, "fmt ", 4) == 0 && size >= 16 && avail >= 16) {
            tag = read16(body); channels = read16(body + 2); rate = read32(body + 4); bits = read16(body + 14);
            if (tag == 0xFFFE && size >= 26 && avail >= 26) tag = read16(body + 24);   // WAVE_FORMAT_EXTENSIBLE: the real format is in the sub-format
            haveFmt = true;
        } else if (std::memcmp(file.data() + pos, "data", 4) == 0) {
            dataAt = body; dataLen = std::min(size, avail); haveData = true;
        }
        const size_t next = body + size + (size & 1);
        if (next <= pos || next > file.size()) break;   // a chunk that claims to run past the end of the file ends the walk
        pos = next;
    }
    if (!haveFmt || !haveData) { SDL_SetError("no format or no sound data in the file"); return nullptr; }
    const bool pcm = tag == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
    const bool flt = tag == 3 && bits == 32;
    if (!pcm && !flt) { SDL_SetError("unsupported sample format"); return nullptr; }
    if (channels < 1 || channels > 8 || rate < 4000 || rate > 192000) { SDL_SetError("unsupported channel count or sample rate"); return nullptr; }
    const size_t bytesPer = bits / 8;
    const size_t frames = dataLen / (bytesPer * channels);
    if (frames == 0) { SDL_SetError("no sound data in the file"); return nullptr; }
    const uint32_t outChannels = channels >= 2 ? 2 : 1;
    const size_t outBytes = frames * outChannels * sizeof(int16_t);
    Uint8* out = static_cast<Uint8*>(SDL_malloc(outBytes));
    if (out == nullptr) { SDL_SetError("out of memory"); return nullptr; }
    int16_t* dst = reinterpret_cast<int16_t*>(out);
    for (size_t f = 0; f < frames; f++) {
        for (uint32_t c = 0; c < outChannels; c++) {
            const uint8_t* src = file.data() + dataAt + (f * channels + c) * bytesPer;
            int16_t v = 0;
            if (flt) {
                float x;
                std::memcpy(&x, src, 4);
                v = static_cast<int16_t>(std::clamp(x, -1.0f, 1.0f) * 32767.0f);
            } else if (bits == 8) v = static_cast<int16_t>((static_cast<int>(src[0]) - 128) << 8);
            else if (bits == 16) v = static_cast<int16_t>(src[0] | (src[1] << 8));
            else v = static_cast<int16_t>(src[bytesPer - 2] | (src[bytesPer - 1] << 8));   // 24 and 32-bit: the top 16 bits
            *dst++ = v;
        }
    }
    *spec = SDL_AudioSpec();
    spec->freq = static_cast<int>(rate);
    spec->format = AUDIO_S16SYS;
    spec->channels = static_cast<Uint8>(outChannels);
    spec->samples = 4096;
    *audioBuf = out;
    *audioLen = static_cast<Uint32>(outBytes);
    return out;
}

bool LoadNextTrack() {
    Trace("song load: start");
    LobbyMusic& m = gLobbyMusic;
    for (size_t tries = 0; tries < m.tracks.size(); tries++) {
        const std::filesystem::path& file = m.tracks[m.next++ % m.tracks.size()];
        SDL_AudioSpec spec = {};
        Uint8* buf = nullptr;
        Uint32 len = 0;
        if (SafeLoadWav(file.string(), &spec, &buf, &len) == nullptr) {
            m.status = "Could not read " + file.filename().string() + ": " + SDL_GetError() + " (use a 16-bit PCM .wav)";
            continue;
        }
        SDL_AudioCVT cvt;
        if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS, 2, 44100) < 0) { SDL_FreeWAV(buf); continue; }
        if (!cvt.needed) {   // already 16-bit stereo at 44100 Hz: take it as it is, with no second and third copy of a big song in memory
            m.pcm.assign(reinterpret_cast<const int16_t*>(buf), reinterpret_cast<const int16_t*>(buf) + len / 2);
            SDL_FreeWAV(buf);
        } else {
            cvt.len = static_cast<int>(len);
            std::vector<Uint8> work(static_cast<size_t>(len) * (cvt.len_mult > 0 ? cvt.len_mult : 1));
            std::memcpy(work.data(), buf, len);
            SDL_FreeWAV(buf);
            cvt.buf = work.data();
            if (SDL_ConvertAudio(&cvt) < 0) continue;
            m.pcm.assign(reinterpret_cast<const int16_t*>(work.data()), reinterpret_cast<const int16_t*>(work.data()) + static_cast<size_t>(cvt.len_cvt) / 2);
        }
        m.nowPlaying = file.stem().string();
        Trace("song load: done");
        return !m.pcm.empty();
    }
    return false;
}

// ---- the music folder's songs, played with Ocarina of Time's own instruments ---------------------------------------------------------
// With the "OoT instruments" option on, each .wav in the music folder is turned into real OoT music in the background: Basic Pitch
// finds its notes, the drum finder its drums, and the game's own soundfonts are searched for the instruments that sound most like each
// part (the same steps as tools/song-to-oot.html). The result is a sequence in the game's own format, which the game's sound engine
// plays on its second music player, exactly like its own songs. Each song is converted once and kept in music/.oot (it holds only notes
// and the numbers of the game's soundfonts and instruments, nothing from the game's data). Until a song is ready, the original plays.
struct OotRawSample {
    std::vector<uint8_t> data;
    int codec = 0, order = 0, npred = 0, loopStart = -1, loopEnd = -1;
    std::vector<int16_t> book;
};
struct OotSong {
    enum State { Waiting, Working, Ready, Failed } state = Waiting;
    std::filesystem::path wav;
    royale::seq::Sequence seq;
    std::string why;                    // for Failed
};
struct OotMusic {
    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    bool quit = false;
    bool captured = false;              // the soundfonts were copied out of the game (on the game thread)
    royale::music::Bank bank;           // owned by the worker once captured
    std::vector<OotRawSample> raw;
    std::vector<std::shared_ptr<OotSong>> songs;
    std::string status;                 // shown in the menu
    std::shared_ptr<OotSong> playing;   // on the game's second music player now
    float lastVolume = -1;
};
OotMusic gOot;
constexpr int kOotSeqPlayer = SEQ_PLAYER_BGM_SUB;
constexpr uint32_t kOotCacheVersion = 1;   // bump when the conversion changes, so old conversions are made again

// Off unless the player turns it on: the conversion copies every soundfont out of the game and runs a background worker, which is heavy for a phone.
bool OotInstrumentsOn() { return MapOption("OotConvert", false); }

// The game's music soundfonts (the ones its songs use, not the sound effects), copied out for the worker: every instrument and drum
// kit, with its samples still packed as the game keeps them. Runs once, on the game thread.
void CaptureOotFonts() {
    if (gOot.captured) return;
    std::set<int> fonts;
    for (size_t id = 2; id <= 108 && id < sequenceMapSize; id++) {
        if (!sequenceMap[id]) continue;
        const SequenceData sd = ResourceMgr_LoadSeqByName(sequenceMap[id]);
        for (int i = 0; i < sd.numFonts && i < 16; i++) fonts.insert(sd.fonts[i]);
    }
    royale::music::Bank bank;
    std::vector<OotRawSample> raw;
    std::unordered_map<const SoundFontSample*, int> seen;
    auto sampleOf = [&](const SoundFontSample* s) -> int {
        if (!s || !s->sampleAddr || s->size == 0) return -1;
        auto it = seen.find(s);
        if (it != seen.end()) return it->second;
        OotRawSample r;
        r.codec = s->codec;
        if (r.codec != CODEC_ADPCM && r.codec != CODEC_SMALL_ADPCM && r.codec != CODEC_S16 && r.codec != CODEC_S16_INMEMORY) return seen[s] = -1;
        if (r.codec == CODEC_ADPCM || r.codec == CODEC_SMALL_ADPCM) {
            if (!s->book || !s->book->book || s->book->order < 1 || s->book->order > 8 || s->book->npredictors < 1 || s->book->npredictors > 16) return seen[s] = -1;
            r.order = s->book->order; r.npred = s->book->npredictors;
            r.book.assign(s->book->book, s->book->book + 8 * r.order * r.npred);
        }
        r.data.assign(s->sampleAddr, s->sampleAddr + s->size);
        if (s->loop && s->loop->count != 0 && s->loop->end > s->loop->start + 8) { r.loopStart = static_cast<int>(s->loop->start); r.loopEnd = static_cast<int>(s->loop->end); }
        raw.push_back(std::move(r));
        return seen[s] = static_cast<int>(raw.size()) - 1;
    };
    auto envelope = [](royale::music::Zone& z, const AdsrEnvelope* e) {
        int16_t pairs[64] = {};
        int n = 0;
        if (e) for (; n < 32; n++) { pairs[2 * n] = e[n].delay; pairs[2 * n + 1] = e[n].arg; if (e[n].delay <= 0) { n++; break; } }
        royale::music::SetEnvelope(z, e ? pairs : nullptr, n);
    };
    for (int f : fonts) {
        if (f < 0 || f >= 256 || !fontMap[f]) continue;
        const SoundFont* sf = ResourceMgr_LoadAudioSoundFont(fontMap[f]);
        if (!sf) continue;
        for (int i = 0; i < sf->numInstruments && sf->instruments; i++) {
            const Instrument* ins = sf->instruments[i];
            if (!ins) continue;
            royale::music::Preset p;
            p.font = f; p.program = i;
            const int lo = ins->normalRangeLo, hi = ins->normalRangeHi, mo = royale::music::kMidiOffset;
            const int ranges[3][2] = {{lo > 0 ? 0 : -1, lo + mo - 1}, {lo + mo, hi + mo}, {hi < 127 ? hi + mo + 1 : -1, 127}};
            const SoundFontSound* sounds[3] = {&ins->lowNotesSound, &ins->normalNotesSound, &ins->highNotesSound};
            for (int slot = 0; slot < 3; slot++) {
                if (ranges[slot][0] < 0 || !sounds[slot]->sample) continue;
                royale::music::Zone z;
                z.lo = std::max(0, ranges[slot][0]); z.hi = std::min(127, ranges[slot][1]);
                if (z.lo > z.hi) continue;
                z.sample = sampleOf(sounds[slot]->sample);
                if (z.sample < 0) continue;
                z.tuning = sounds[slot]->tuning;
                envelope(z, ins->envelope);
                z.release = royale::music::ReleaseSeconds(ins->releaseRate);
                p.zones.push_back(z);
            }
            if (!p.zones.empty()) bank.presets.push_back(std::move(p));
        }
        royale::music::Preset kit;
        kit.font = f; kit.drums = true;
        for (int d = 0; d < sf->numDrums && sf->drums; d++) {
            const Drum* dr = sf->drums[d];
            const int key = d + royale::music::kMidiOffset;
            if (key > 127) break;
            if (!dr) continue;
            royale::music::Zone z;
            z.lo = z.hi = z.fixedKey = key;
            z.sample = sampleOf(dr->sound.sample);
            if (z.sample < 0) continue;
            z.tuning = dr->sound.tuning;
            envelope(z, dr->envelope);
            z.release = royale::music::ReleaseSeconds(dr->releaseRate);
            z.pan = (dr->pan - 64) / 64.0;
            kit.zones.push_back(z);
        }
        if (!kit.zones.empty()) bank.presets.push_back(std::move(kit));
    }
    std::lock_guard<std::mutex> lock(gOot.mutex);
    gOot.bank = std::move(bank);
    gOot.raw = std::move(raw);
    gOot.captured = true;
    gOot.wake.notify_all();
}

std::filesystem::path OotCachePath(const std::filesystem::path& wav) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(wav, ec);
    return MusicFolder() / ".oot" / (wav.stem().string() + "-" + std::to_string(ec ? 0 : size) + ".ootseq");
}
bool ReadOotCache(const std::filesystem::path& file, royale::seq::Sequence& s) {
    std::ifstream in(file, std::ios::binary);
    char magic[8] = {};
    uint32_t version = 0, nFonts = 0, size = 0;
    if (!in.read(magic, 8) || std::memcmp(magic, "OOTSEQ\0\0", 8) != 0) return false;
    if (!in.read(reinterpret_cast<char*>(&version), 4) || version != kOotCacheVersion) return false;
    if (!in.read(reinterpret_cast<char*>(&nFonts), 4) || nFonts > 16) return false;
    s.fonts.resize(nFonts);
    if (!in.read(reinterpret_cast<char*>(s.fonts.data()), nFonts) || !in.read(reinterpret_cast<char*>(&s.seconds), 8)) return false;
    if (!in.read(reinterpret_cast<char*>(&size), 4) || size == 0 || size > royale::seq::kMaxBytes) return false;
    s.data.resize(size);
    return static_cast<bool>(in.read(reinterpret_cast<char*>(s.data.data()), size)) && royale::seq::ParseSequence(s.data).ok;
}
void WriteOotCache(const std::filesystem::path& file, const royale::seq::Sequence& s) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary);
    const uint32_t version = kOotCacheVersion, nFonts = static_cast<uint32_t>(s.fonts.size()), size = static_cast<uint32_t>(s.data.size());
    out.write("OOTSEQ\0\0", 8);
    out.write(reinterpret_cast<const char*>(&version), 4);
    out.write(reinterpret_cast<const char*>(&nFonts), 4);
    out.write(reinterpret_cast<const char*>(s.fonts.data()), nFonts);
    out.write(reinterpret_cast<const char*>(&s.seconds), 8);
    out.write(reinterpret_cast<const char*>(&size), 4);
    out.write(reinterpret_cast<const char*>(s.data.data()), size);
}

// A .wav as mono 22050 Hz floats (what the note finder hears).
bool LoadWavMono22k(const std::filesystem::path& file, std::vector<float>& out, std::string& why) {
    SDL_AudioSpec spec = {};
    Uint8* buf = nullptr;
    Uint32 len = 0;
    if (SafeLoadWav(file.string(), &spec, &buf, &len) == nullptr) { why = SDL_GetError(); return false; }
    SDL_AudioCVT cvt;
    if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_F32SYS, 1, 22050) < 0) { SDL_FreeWAV(buf); why = SDL_GetError(); return false; }
    std::vector<Uint8> work(static_cast<size_t>(len) * static_cast<size_t>(cvt.len_mult > 0 ? cvt.len_mult : 1) + 16);
    std::memcpy(work.data(), buf, len);
    SDL_FreeWAV(buf);
    cvt.len = static_cast<int>(len);
    cvt.buf = work.data();
    if (cvt.needed && SDL_ConvertAudio(&cvt) < 0) { why = SDL_GetError(); return false; }
    const size_t bytes = cvt.needed ? static_cast<size_t>(cvt.len_cvt) : len;
    out.assign(bytes / sizeof(float), 0.0f);
    std::memcpy(out.data(), work.data(), out.size() * sizeof(float));
    return !out.empty();
}

// The worker: unpacks the samples once, then converts the songs one at a time, newest wishes first.
void OotWorker() {
    bool bankReady = false;
    for (;;) {
        std::shared_ptr<OotSong> song;
        {
            std::unique_lock<std::mutex> lock(gOot.mutex);
            gOot.wake.wait(lock, [] {
                if (gOot.quit) return true;
                if (!gOot.captured) return false;
                for (auto& s : gOot.songs) if (s->state == OotSong::Waiting) return true;
                return false;
            });
            if (gOot.quit) return;
            for (auto& s : gOot.songs) if (s->state == OotSong::Waiting) { song = s; break; }
            song->state = OotSong::Working;
        }
        auto say = [&](const std::string& text) { std::lock_guard<std::mutex> lock(gOot.mutex); gOot.status = text; };
        if (!bankReady) {
            say("Reading the game's instruments...");
            gOot.bank.samples.resize(gOot.raw.size());
            for (size_t i = 0; i < gOot.raw.size(); i++) {
                const OotRawSample& r = gOot.raw[i];
                royale::music::Sample& s = gOot.bank.samples[i];
                if (r.codec == CODEC_ADPCM || r.codec == CODEC_SMALL_ADPCM)
                    s.pcm = royale::music::DecodeVadpcm(r.data.data(), r.data.size(), r.book.data(), r.order, r.npred, r.codec == CODEC_SMALL_ADPCM);
                else { s.pcm.resize(r.data.size() / 2); std::memcpy(s.pcm.data(), r.data.data(), s.pcm.size() * 2); }
                if (r.loopStart >= 0 && r.loopStart < static_cast<int>(s.pcm.size())) { s.loopStart = r.loopStart; s.loopEnd = std::min(r.loopEnd, static_cast<int>(s.pcm.size())); }
                if (s.loopEnd <= s.loopStart + 8) s.loopStart = s.loopEnd = -1;
            }
            gOot.raw.clear();
            for (royale::music::Preset& p : gOot.bank.presets) royale::music::Analyze(gOot.bank, p);
            bankReady = true;
        }
        const std::string name = song->wav.stem().string();
        royale::seq::Sequence seq;
        std::string why;
        const std::filesystem::path cache = OotCachePath(song->wav);
        bool ok = ReadOotCache(cache, seq);
        if (!ok) {
            std::vector<float> y;
            if (!LoadWavMono22k(song->wav, y, why)) why = "could not read it (" + why + ")";
            else {
                bool quit = false;
                const royale::bp::Output o = royale::bp::Run(y.data(), y.size(), [&](float f) {
                    say("Turning " + name + " into OoT music: finding the notes " + std::to_string(static_cast<int>(f * 100)) + "%");
                    std::lock_guard<std::mutex> lock(gOot.mutex);
                    quit = gOot.quit;
                    return !quit;
                });
                if (quit) return;
                say("Turning " + name + " into OoT music: picking instruments");
                const std::vector<royale::music::DrumHit> hits = royale::music::TranscribeDrums(y.data(), y.size(), 22050);
                const std::vector<royale::music::Note> notes = royale::music::CleanNotes(royale::music::ToNotes(o.frames, o.onsets, 0.5, 0.3, 11, 11), hits);
                if (notes.empty()) why = "no notes were found in it";
                else {
                    const royale::music::Heard heard = royale::music::HearSong(y.data(), y.size(), 22050, notes, hits);
                    const royale::music::Arrangement a = royale::music::Arrange(gOot.bank, notes, hits, &heard, y.size() / 22050.0);
                    if (a.channels.empty()) why = "no instrument fits it";
                    else {
                        seq = royale::music::WriteArrangement(a);
                        ok = !seq.tooBig;
                        if (ok) WriteOotCache(cache, seq);
                        else why = "it is too long for the game";
                    }
                }
            }
        }
        std::lock_guard<std::mutex> lock(gOot.mutex);
        if (ok) { song->seq = std::move(seq); song->state = OotSong::Ready; gOot.status = name + " is ready to play with OoT instruments."; }
        else { song->state = OotSong::Failed; song->why = why; gOot.status = name + ": " + why + "; the original plays instead."; }
    }
}

// Queue every song in the folder (called after a scan). Starts the worker the first time.
void QueueOotSongs() {
    if (!OotInstrumentsOn()) return;
    CaptureOotFonts();
    std::lock_guard<std::mutex> lock(gOot.mutex);
    for (const auto& t : gLobbyMusic.tracks) {
        bool have = false;
        for (auto& s : gOot.songs) if (s->wav == t) { have = true; break; }
        if (!have) { auto s = std::make_shared<OotSong>(); s->wav = t; gOot.songs.push_back(s); }
    }
    if (!gOot.worker.joinable()) {
        gOot.worker = std::thread(OotWorker);
        std::atexit([] {
            { std::lock_guard<std::mutex> lock(gOot.mutex); gOot.quit = true; }
            gOot.wake.notify_all();
            if (gOot.worker.joinable()) gOot.worker.join();
        });
    }
    gOot.wake.notify_all();
}
std::shared_ptr<OotSong> ReadyOotSong(const std::filesystem::path& wav) {
    std::lock_guard<std::mutex> lock(gOot.mutex);
    for (auto& s : gOot.songs) if (s->wav == wav && s->state == OotSong::Ready) return s;
    return nullptr;
}

// Is our song still on the second music player? (The game may have stopped it, or put its own music there.)
bool OotSongPlaying() {
    if (!gOot.playing) return false;
    const SequencePlayer& sp = gAudioContext.seqPlayers[kOotSeqPlayer];
    return sp.enabled && sp.seqData == gOot.playing->seq.data.data();
}
void StopOotSong() {
    if (OotSongPlaying()) AudioSeq_SequencePlayerDisable(&gAudioContext.seqPlayers[kOotSeqPlayer]);
    gOot.playing = nullptr;
}
// Start a converted song on the game's second music player, the way the game starts its own (audio_load.c,
// AudioLoad_SyncInitSeqPlayerInternal), with no font list: the song names each soundfont itself. Runs on the game thread,
// between the sound engine's updates.
bool StartOotSong(const std::shared_ptr<OotSong>& song) {
    StopOotSong();
    for (uint8_t f : song->seq.fonts) if (!fontMap[f] || !AudioLoad_SyncLoadFont(f)) return false;
    SequencePlayer* sp = &gAudioContext.seqPlayers[kOotSeqPlayer];
    AudioSeq_SequencePlayerDisable(sp);
    AudioSeq_ResetSequencePlayer(sp);
    sp->seqId = NA_BGM_STAFF_4;
    sp->defaultFont = 0xFF;
    sp->seqData = song->seq.data.data();
    sp->scriptState.pc = sp->seqData;
    sp->scriptState.depth = 0;
    sp->delay = 0;
    sp->finished = 0;
    sp->playerIdx = kOotSeqPlayer;
    sp->enabled = 1;
    gOot.playing = song;
    gOot.lastVolume = -1;
    return true;
}
// The song follows the player's main music volume.
void KeepOotSongVolume() {
    if (!OotSongPlaying()) return;
    const float v = static_cast<float>(CVarGetInteger(CVAR_SETTING("Volume.MainMusic"), 100)) / 100.0f;
    if (v != gOot.lastVolume) { Audio_SetGameVolume(kOotSeqPlayer, v); gOot.lastVolume = v; }
}
std::string OotStatus() {
    std::lock_guard<std::mutex> lock(gOot.mutex);
    int ready = 0;
    for (auto& s : gOot.songs) ready += s->state == OotSong::Ready;
    return std::to_string(ready) + " of " + std::to_string(gOot.songs.size()) + " songs ready with OoT instruments. " + gOot.status;
}

// The music folder's songs play in the lobby (if that option is on) and, when "Match music" is set to Random, through the match too.
void UpdateLobbyMusic(bool inLobby, bool inMatchRandom = false) {
    LobbyMusic& m = gLobbyMusic;
    const bool want = ((inLobby && MapOption("LobbyMusic", true)) || inMatchRandom) && !m.failed;
    if (!want) {
        if (m.playing) StopVoice(kVoiceSong);
        m.playing = false;
        StopOotSong();
        if (!inLobby && !inMatchRandom) m.scanned = false; // pick up newly added songs next time
        return;
    }
    if (!m.scanned) ScanMusicFolder();
    if (m.tracks.empty()) return;
    // A song already turned into OoT music plays on the game's own sound engine; the others play as they are until they are ready.
    if (OotSongPlaying()) { if (OotInstrumentsOn()) { KeepOotSongVolume(); return; } StopOotSong(); }
    gOot.playing = nullptr;
    const bool done = VoiceDone(kVoiceSong);
    if (OotInstrumentsOn() && done) {
        for (size_t i = 0; i < m.tracks.size(); i++) {
            const size_t k = (m.next + i) % m.tracks.size();
            std::shared_ptr<OotSong> song = ReadyOotSong(m.tracks[k]);
            if (!song || !StartOotSong(song)) continue;
            m.next = k + 1;
            StopVoice(kVoiceSong);
            m.nowPlaying = m.tracks[k].stem().string();
            m.playing = true;
            KeepOotSongVolume();
            Say((inLobby ? "Lobby music: " : "Now playing: ") + m.nowPlaying + " (OoT instruments)");
            return;
        }
    }
    if (done) {
        if (!LoadNextTrack()) {
            m.failed = true;   // the game's own music comes back (DriveMatchMusic), and the menu says why
            if (m.status.rfind("Could not read", 0) != 0) m.status = "None of the songs in " + MusicFolder().string() + " could be played.";
            Say("Music folder: " + m.status);
            return;
        }
        StartVoice(kVoiceSong, std::make_shared<const std::vector<int16_t>>(std::move(m.pcm)), true, 44100, false, GameVolume(true));
        m.pcm.clear();
        Say((inLobby ? "Lobby music: " : "Now playing: ") + m.nowPlaying);
    } else {
        SetVoiceVolume(kVoiceSong, GameVolume(true));
    }
    m.playing = true;
}

// What the local player has just done with an item, held for a moment so that everyone sees the pose (potion, ocarina, bow, swing...).
int gActionFrames = 0;
royale::Anim gActionAnim = royale::Anim::Idle;
double gActionStart = 0;
royale::ItemId gActionItem = royale::ItemId::Count;   // the bottle being drunk or the ability being used: what Link holds for it (LocalLink_Draw)
void StartAction(royale::Anim pose, float seconds, royale::ItemId item = royale::ItemId::Count) {
    gActionAnim = pose;
    gActionFrames = std::max(1, static_cast<int>(seconds * royale::kTickHz));
    gActionStart = ImGui::GetTime();
    gActionItem = item;
}
royale::Anim PoseForWeapon(royale::ItemId weapon) {
    const royale::WeaponStats w = royale::WeaponOf(weapon);
    if (!w.ranged) return royale::Anim::Attack;
    if (weapon == royale::ItemId::Hookshot) return royale::Anim::Shoot;   // the chain is fired like a bow (Grip::Hook picks the hookshot pose)
    const royale::AmmoKind a = royale::AmmoUsedBy(weapon);
    return (a == royale::AmmoKind::Arrows || a == royale::AmmoKind::Seeds) ? royale::Anim::Shoot : royale::Anim::Throw;
}

// Whether Link is playing a particular animation of the game's. The animation names are compared, not their addresses: every source file
// has its own copy of each name.
bool AnimIs(const void* playing, const char* name) {
    if (playing == nullptr) return false;
    const char* p = static_cast<const char*>(playing);
    return std::strncmp(p, "__OTR__", 7) == 0 && std::strcmp(p, name) == 0;
}

// What everybody else should see you doing. Your own Link runs the real game, so its moves are read straight off it: the Z-target side hops,
// back flips, jump slashes and spin attacks, the shield, the sword, jumps and the lock-on footwork.
uint8_t ClassifyAnim(Player* player) {
    using royale::Anim;
    static float lastX = 0, lastZ = 0;
    const float dx = player->actor.world.pos.x - lastX, dz = player->actor.world.pos.z - lastZ;
    lastX = player->actor.world.pos.x; lastZ = player->actor.world.pos.z;
    if (gEmote.id >= 0) return royale::EmoteAnim(gEmote.id); // others see the gesture
    if (player->stateFlags1 & PLAYER_STATE1_DEAD) return static_cast<uint8_t>(Anim::Dead);
    const void* a = player->skelAnime.animation;
    auto is = [&](const char* name) { return AnimIs(a, name); };
    if (is(gPlayerAnim_link_fighter_Lside_jump) || is(gPlayerAnim_link_fighter_Lside_jump_end)) return static_cast<uint8_t>(Anim::HopL);
    if (is(gPlayerAnim_link_fighter_Rside_jump) || is(gPlayerAnim_link_fighter_Rside_jump_end)) return static_cast<uint8_t>(Anim::HopR);
    if (is(gPlayerAnim_link_fighter_backturn_jump) || is(gPlayerAnim_link_fighter_backturn_jump_end)) return static_cast<uint8_t>(Anim::Backflip);
    if (is(gPlayerAnim_link_fighter_Lpower_jump_kiru) || is(gPlayerAnim_link_fighter_Lpower_jump_kiru_hit) || is(gPlayerAnim_link_fighter_jump_rollkiru) ||
        is(gPlayerAnim_link_fighter_jump_kiru_finsh))
        return static_cast<uint8_t>(Anim::JumpSlash);
    if (is(gPlayerAnim_link_fighter_rolling_kiru) || is(gPlayerAnim_link_fighter_Lrolling_kiru) || is(gPlayerAnim_link_fighter_Wrolling_kiru))
        return static_cast<uint8_t>(Anim::SpinAttack);
    if (gActionFrames > 0) return static_cast<uint8_t>(gActionAnim);
    if (player->stateFlags1 & PLAYER_STATE1_SHIELDING) return static_cast<uint8_t>(Anim::Guard);
    if (player->meleeWeaponState != 0) return static_cast<uint8_t>(Anim::Attack);   // swinging the real sword
    const bool ground = (player->actor.bgCheckFlags & 1) != 0;
    if (!ground && !gSkydiving && !(player->stateFlags1 & (PLAYER_STATE1_HANGING_OFF_LEDGE | PLAYER_STATE1_CLIMBING_LADDER | PLAYER_STATE1_IN_WATER | PLAYER_STATE1_CLIMBING_LEDGE)))
        return static_cast<uint8_t>(Anim::Jump);
    const float v = std::fabs(player->linearVelocity);
    const float runTop = gSprinting ? 7.5f * kSprintMult : 7.5f;
    if (is(gPlayerAnim_link_normal_landing_roll) || is(gPlayerAnim_link_normal_landing_roll_free) || (v > runTop && ground)) return static_cast<uint8_t>(Anim::Roll);   // a roll is faster than any run
    if (player->stateFlags1 & (PLAYER_STATE1_HOSTILE_LOCK_ON | PLAYER_STATE1_PARALLEL)) {
        // Z-targeting: the footwork depends on which way you move relative to where you face.
        if (dx * dx + dz * dz < 0.25f) return static_cast<uint8_t>(Anim::Stance);
        const float yaw = player->actor.shape.rot.y * (3.14159265f / 32768.0f);
        const float fwd = dx * std::sin(yaw) + dz * std::cos(yaw), right = -dx * std::cos(yaw) + dz * std::sin(yaw);
        if (std::fabs(fwd) >= std::fabs(right)) return static_cast<uint8_t>(fwd > 0 ? Anim::Run : Anim::Back);
        return static_cast<uint8_t>(right > 0 ? Anim::SideR : Anim::SideL);
    }
    if (v < 0.5f) return static_cast<uint8_t>(Anim::Idle);
    if (gSprinting && v >= 4.0f) return static_cast<uint8_t>(Anim::Sprint);
    return static_cast<uint8_t>(v < 4.0f ? Anim::Walk : Anim::Run);
}

uint8_t gLastEpoch = 0;
royale::MatchState gLastState = royale::MatchState::Lobby;
bool gWasJoined = false;
int gLastCountdownShown = -1;
int gAttackCooldown = 0;     // game frames until B may attack again
bool gPendingStart = false;  // the host pressed Start; waiting to be in Hyrule Field, measure the map, then begin
bool gSoloStartWanted = false;   // "Fortnite Map solo test" was pressed: start the match as soon as the host is in its own lobby
int gInFieldFrames = 0;      // frames spent in the field without a transition, so the scene's collision is ready
bool gSpectating = false;

// While a match is live the server's health replaces the save's. Keep the real values so leaving a match, or the match
// ending, doesn't leave the player's save file with 3 hearts.
bool gHealthOverridden = false;
s16 gSavedCapacity = 0, gSavedHealth = 0;
s16 gMatchHealth = 16;   // what the save's health should read this frame while the server owns it (never 0, see OnPlayerUpdate)
u8 gMatchSeqId = 0xFF, gMatchAmbienceId = 0xFF;   // the scene's music, to put back if the game's own death ever starts (see CancelGameDeath)

bool IsLive(const royale::HudState& h) {
    return h.state == royale::MatchState::Drop || h.state == royale::MatchState::InMatch;
}

void RestoreHealth() {
    if (!gHealthOverridden) return;
    gHealthOverridden = false;
    gSaveContext.healthCapacity = gSavedCapacity;
    // Come back alive: an eliminated player left health at 0.
    gSaveContext.health = gSavedHealth > 0 ? gSavedHealth : gSavedCapacity;
}

void Say(const std::string& text) {
    Notification::Emit({ .message = text });
}

// B attacks with the weapon the server says you hold; D-pad Down drinks a potion. Damage, range, cooldown and healing are
// decided by the server, so this only chooses a target: the nearest living player in front of Link and within weapon range.
int gCurrentPoi = -1;        // which point of interest the player is standing in, -1 for none

// "Entering ..." when you walk into a town.
void NoticePoi(Player* player, const royale::HudState& hud) {
    if (!gSession.Client() || !InField() || !(hud.state == royale::MatchState::Drop || hud.state == royale::MatchState::InMatch) || gSkydiving) { gCurrentPoi = -1; return; }
    int now = -1;
    for (const royale::Poi& p : gSession.Client()->Pois()) {
        const float dx = p.center.x - player->actor.world.pos.x, dz = p.center.z - player->actor.world.pos.z;
        if (dx * dx + dz * dz < p.radius * p.radius) { now = p.name; break; }
    }
    if (now != gCurrentPoi && now >= 0) Say(std::string("Entering ") + royale::kPoiNames[now]);
    gCurrentPoi = now;
}

int gNextWeaponSlot = 1;
int gLastWeaponSlot = 0;     // the reserve slot last swapped in (D-pad Left swaps it back)
int gJumpAssistFrames = 0;   // frames left in which a jump pulls you onto a ledge in front of you     // which backup slot D-pad Left swaps in next

// A swing that finds no player cuts the bush or breaks the rock in front of Link (a boulder needs something heavy or explosive).
