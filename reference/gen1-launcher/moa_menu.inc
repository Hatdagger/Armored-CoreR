/* Armored Core Gen 1 launcher: a controller-first front end drawn over the
 * framework's own launcher (recomp-ui), in the Master of Arena system-menu look.
 *
 * Brought over from the AC1Recomp fork's recomp-ui moa_menu.inc (the user's own
 * design, chat 2026-11-20: "pull from old repo"), trimmed to what this step of
 * the ladder needs: START GAME, SETTINGS, MODS, UPDATES, EXIT. Rogue Arena,
 * Extra Garage, Extras and the custom network pages come back with their own
 * steps.
 *
 * Included by recomp-ui's launcher_imgui.cpp when RECOMP_LAUNCHER_MOA_MENU is
 * set (patches/recomp-ui/0001-title-menu-hook.patch adds the hook). It owns the
 * launcher's dashboard, settings and mods views and draws them as one list at a
 * time: D-pad to move, one button to confirm or toggle, one to go back.
 * Everything else (the controller page, every modal, network pages) is the
 * stock launcher, reached from here and returned from with its own Back.
 *
 * It draws straight onto the launcher window's draw list on a 640x480 design
 * grid scaled to the window, and reads input through ImGui's key state. Game
 * state goes through the launcher model's own functions and the mod provider.
 *
 * Expected from the including file: LauncherModel, LauncherTheme, asset(),
 * ui_pick_file(), request_mod_commit_launch(), mod_note_error(),
 * moa_menu_open_folder(), and (included before its anonymous namespace)
 * moa_update.h and the framework's disc_identity.h.
 */

#include <ctime>

namespace moa_menu {

enum Page {
    PAGE_MAIN = 0, PAGE_SETTINGS, PAGE_MODS, PAGE_UPDATES, PAGE_START, PAGE_START_LOAD, PAGE_VERSUS, PAGE__COUNT
};

struct State {
    int    cursor[PAGE__COUNT] = {0};
    int    top[PAGE__COUNT] = {0};
    bool   classic_settings = false;   /* the stock page was asked for */
    bool   classic_mods = false;
    int    last_view = -1;
    double next_scan = 0.0;
    std::vector<std::string> new_packages;   /* files waiting in mods/new */
    bool   updates_page = false;       /* the Updates page is up */
    bool   start_page = false;         /* the START GAME page is up */
    bool   start_list = false;         /* ... showing LOAD GAME's save list */
    bool   versus_page = false;        /* the VERSUS page is up */
    bool   disc_scanned = false;       /* looked for a disc beside the game */
    char   note[200] = {0};
    double note_until = 0.0;
};
static State g_state;

/* ---- menu sounds ------------------------------------------------------------
 * The game's own menu sounds, from the player's disc (launcher/moa_ui_sound.*).
 * Anything on the page may ask for a sound; once a frame the most important
 * request plays, so one press never makes two sounds. */
static int g_sfx = -1;
static int sfx_rank(int s) {
    switch (s) {
        case moa_sound::SFX_REFUSED: return 4;
        case moa_sound::SFX_BACK: case moa_sound::SFX_WINDOW: return 3;
        case moa_sound::SFX_OK: return 2;
        default: return 1;
    }
}
static void sfx(int s) { if (g_sfx < 0 || sfx_rank(s) > sfx_rank(g_sfx)) g_sfx = s; }

/* Design grid. */
static const float kW = 640.0f, kH = 480.0f;
static const float kRowH = 26.0f;

static const ImU32 kBg      = IM_COL32(6, 9, 13, 255);
static const ImU32 kGrid    = IM_COL32(26, 43, 51, 110);
static const ImU32 kPanel   = IM_COL32(11, 18, 24, 225);
static const ImU32 kLine    = IM_COL32(47, 74, 85, 255);
static const ImU32 kText    = IM_COL32(185, 212, 207, 255);
static const ImU32 kBright  = IM_COL32(255, 244, 220, 255);
static const ImU32 kDim     = IM_COL32(95, 127, 128, 255);
static const ImU32 kHi      = IM_COL32(240, 160, 32, 255);
static const ImU32 kOn      = IM_COL32(95, 224, 138, 255);
static const ImU32 kOff     = IM_COL32(61, 84, 88, 255);
static const ImU32 kWarn    = IM_COL32(226, 91, 58, 255);
static const ImU32 kOnText  = IM_COL32(6, 17, 10, 255);

struct Gfx {
    ImDrawList* dl;
    ImFont*     font;
    float       s;      /* window pixels per design unit */
    ImVec2      o;      /* window position of design (0,0) */
};

static ImVec2 at(const Gfx& g, float x, float y) {
    return ImVec2(g.o.x + x * g.s, g.o.y + y * g.s);
}

static float text_width(const Gfx& g, float size, const char* s) {
    return g.font->CalcTextSizeA(size * g.s, FLT_MAX, 0.0f, s).x / g.s;
}

static void text(const Gfx& g, float x, float y, float size, ImU32 color,
                 const char* s, float wrap = 0.0f) {
    g.dl->AddText(g.font, size * g.s, at(g, x, y), color, s, nullptr,
                  wrap > 0.0f ? wrap * g.s : 0.0f);
}

static void text_right(const Gfx& g, float right, float y, float size,
                       ImU32 color, const char* s) {
    text(g, right - text_width(g, size, s), y, size, color, s);
}

static std::string upper(const char* s) {
    std::string out(s ? s : "");
    for (char& c : out) c = (char)std::toupper((unsigned char)c);
    return out;
}

static bool same_word(const char* a, const char* b) {
    if (!a || !b) return false;
    for (; *a && *b; ++a, ++b)
        if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b))
            return false;
    return *a == *b;
}

static void panel(const Gfx& g, float x, float y, float w, float h,
                  const char* caption) {
    g.dl->AddRectFilled(at(g, x, y), at(g, x + w, y + h), kPanel);
    g.dl->AddRect(at(g, x, y), at(g, x + w, y + h), kLine);
    if (caption && caption[0]) {
        const float cw = text_width(g, 10.0f, caption);
        g.dl->AddRectFilled(at(g, x + 10.0f, y - 6.0f),
                            at(g, x + 22.0f + cw, y + 6.0f), kBg);
        text(g, x + 16.0f, y - 6.0f, 10.0f, kDim, caption);
    }
}

static void header(const Gfx& g, const char* title, const char* sub) {
    text(g, 24.0f, 22.0f, 19.0f, IM_COL32(223, 238, 234, 255), title);
    text_right(g, kW - 24.0f, 32.0f, 10.0f, kDim, sub);
    g.dl->AddLine(at(g, 24.0f, 52.0f), at(g, kW - 24.0f, 52.0f), kLine);
}

static void footer(const Gfx& g, const char* hints) {
    g.dl->AddLine(at(g, 24.0f, kH - 36.0f), at(g, kW - 24.0f, kH - 36.0f), kLine);
    text(g, 24.0f, kH - 28.0f, 10.0f, kDim, hints);
    if (g_state.note[0] && ImGui::GetTime() < g_state.note_until)
        text_right(g, kW - 24.0f, kH - 28.0f, 10.0f, kHi, g_state.note);
}

static void note(const char* s) {
    std::snprintf(g_state.note, sizeof g_state.note, "%s", s ? s : "");
    g_state.note_until = ImGui::GetTime() + 4.0;
}

static void lamp(const Gfx& g, float right, float y, bool on, const char* label) {
    const float w = 38.0f, h = 16.0f;
    const ImVec2 a = at(g, right - w, y), b = at(g, right, y + h);
    if (on) g.dl->AddRectFilled(a, b, kOn);
    g.dl->AddRect(a, b, on ? kOn : kOff);
    const float tw = text_width(g, 11.0f, label);
    text(g, right - w + (w - tw) * 0.5f, y + 2.0f, 11.0f, on ? kOnText : kOff, label);
}

/* One selectable row; returns true when it was clicked with the mouse. */
static bool row(const Gfx& g, float x, float y, float w, bool selected,
                bool enabled, const char* label, const char* value,
                bool is_toggle, bool toggle_on, bool* hovered_out) {
    const ImVec2 a = at(g, x, y), b = at(g, x + w, y + kRowH);
    const bool hovered = ImGui::IsMouseHoveringRect(a, b, false);
    if (hovered_out) *hovered_out = hovered;
    if (selected) {
        g.dl->AddRectFilledMultiColor(a, b, IM_COL32(240, 160, 32, 70),
                                      IM_COL32(240, 160, 32, 0),
                                      IM_COL32(240, 160, 32, 0),
                                      IM_COL32(240, 160, 32, 70));
        g.dl->AddTriangleFilled(at(g, x + 12.0f, y + 8.0f),
                                at(g, x + 21.0f, y + 13.0f),
                                at(g, x + 12.0f, y + 18.0f), kHi);
    }
    const ImU32 color = !enabled ? kOff : selected ? kBright : kText;
    text(g, x + 30.0f, y + 6.0f, 13.0f, color, label);
    if (is_toggle) {
        lamp(g, x + w - 14.0f, y + 5.0f, toggle_on && enabled,
             !enabled ? "N/A" : toggle_on ? "ON" : "OFF");
    } else if (value && value[0]) {
        text_right(g, x + w - 14.0f, y + 6.0f, 13.0f, enabled ? kHi : kOff, value);
    }
    const bool clicked = hovered && enabled && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    if (clicked) sfx(moa_sound::SFX_OK);
    return clicked;
}

/* ---- input ---------------------------------------------------------------- */

struct Input {
    bool up, down, left, right, confirm, back, alt, start, page_up, page_down;
    bool info;   /* TRIANGLE / Y (also counted as back elsewhere), or I */
    bool tab_prev, tab_next;   /* L1 / R1, or Q / E */
};

static bool key(ImGuiKey k, bool repeat) { return ImGui::IsKeyPressed(k, repeat); }

static Input read_input(const LauncherModel* m) {
    Input in = {};
    /* A modal owns the screen, a text field owns the keys, and a rebind
     * capture owns the pad. */
    if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId |
                                    ImGuiPopupFlags_AnyPopupLevel) ||
        ImGui::GetIO().WantTextInput || m->capturing || m->hk_capturing ||
        m->camera_capturing || m->setup_wizard_open)
        return in;
    in.up    = key(ImGuiKey_UpArrow, true) || key(ImGuiKey_GamepadDpadUp, true) ||
               key(ImGuiKey_GamepadLStickUp, true);
    in.down  = key(ImGuiKey_DownArrow, true) || key(ImGuiKey_GamepadDpadDown, true) ||
               key(ImGuiKey_GamepadLStickDown, true);
    in.left  = key(ImGuiKey_LeftArrow, true) || key(ImGuiKey_GamepadDpadLeft, true) ||
               key(ImGuiKey_GamepadLStickLeft, true);
    in.right = key(ImGuiKey_RightArrow, true) || key(ImGuiKey_GamepadDpadRight, true) ||
               key(ImGuiKey_GamepadLStickRight, true);
    in.confirm = key(ImGuiKey_Enter, false) || key(ImGuiKey_KeypadEnter, false) ||
                 key(ImGuiKey_Space, false) || key(ImGuiKey_GamepadFaceDown, false);
    in.back  = key(ImGuiKey_Escape, false) || key(ImGuiKey_Backspace, false) ||
               key(ImGuiKey_GamepadFaceRight, false) ||
               key(ImGuiKey_GamepadFaceUp, false);
    in.alt   = key(ImGuiKey_Delete, false) || key(ImGuiKey_GamepadFaceLeft, false);
    in.start = key(ImGuiKey_GamepadStart, false);
    in.info  = key(ImGuiKey_GamepadFaceUp, false) || key(ImGuiKey_I, false);
    in.page_up   = key(ImGuiKey_PageUp, true) || key(ImGuiKey_GamepadR1, true);
    in.page_down = key(ImGuiKey_PageDown, true) || key(ImGuiKey_GamepadL1, true);
    in.tab_prev  = key(ImGuiKey_GamepadL1, false) || key(ImGuiKey_Q, false);
    in.tab_next  = key(ImGuiKey_GamepadR1, false) || key(ImGuiKey_E, false);
    return in;
}

static void move_cursor(Page page, int count, const Input& in, int visible) {
    int& c = g_state.cursor[page];
    int& t = g_state.top[page];
    if (count <= 0) { c = t = 0; return; }
    if (in.up)   c = (c + count - 1) % count;
    if (in.down) c = (c + 1) % count;
    if ((in.up || in.down) && count > 1) sfx(moa_sound::SFX_CURSOR);
    if (c < 0) c = 0;
    if (c >= count) c = count - 1;
    if (c < t) t = c;
    if (c >= t + visible) t = c - visible + 1;
    if (t > count - visible) t = count - visible;
    if (t < 0) t = 0;
}

/* ---- Updates page ----------------------------------------------------------------
 *
 * Opt-in: nothing is looked up or downloaded until a button here is pressed
 * (launcher/moa_update.cpp does the work on its own thread). Only the
 * program's own files are replaced; memory cards, saves, mods, disc images and
 * settings are left alone.
 *
 * Dev builds (not public releases) also list the newest GitHub Actions build,
 * read with the player's own GitHub token, pasted here once.
 */
static bool update_live(const moa_update::Snapshot& s) {
    using moa_update::State;
    return s.state == State::Available || s.state == State::Downloading ||
           s.state == State::Applying;
}

/* After an update installs, the launcher shows it for a moment, then the game
 * restarts by itself (moa_update::restart) and this copy closes. */
static double g_restart_at = -1.0;
static bool   g_restart_tried = false;

static void restart_after_update(LauncherModel* m) {
    if (g_restart_tried || m->in_session || m->view != LNG_VIEW_DASHBOARD) return;
    if (moa_update::snapshot().state != moa_update::State::Done) return;
    const double now = ImGui::GetTime();
    if (g_restart_at < 0.0) { g_restart_at = now + 2.0; return; }
    if (now < g_restart_at) return;
    g_restart_tried = true;
    if (moa_update::restart()) m->action = LNG_ACTION_QUIT;
}

static bool g_token_popup = false;
static char g_token_buf[256] = {0};

static void open_updates_page() {
    g_state.updates_page = true;
    g_state.cursor[PAGE_UPDATES] = 0;
    g_state.note[0] = '\0';
}

/* Paste-a-token box. Keyboard and mouse: a token is pasted, not typed. */
static void draw_token_popup() {
    if (g_token_popup) { ImGui::OpenPopup("GITHUB TOKEN"); g_token_popup = false; }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(vp->Size.x * 0.6f, 0.0f));
    if (!ImGui::BeginPopupModal("GITHUB TOKEN", nullptr,
                                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
        return;
    ImGui::TextWrapped("Paste your fine-grained GitHub token (this repository only, read-only "
                       "Actions and Contents). It is kept in your user profile, outside the "
                       "game folder, and only ever sent to api.github.com.");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    const bool enter = ImGui::InputText("##token", g_token_buf, sizeof g_token_buf,
                                        ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::Button("PASTE")) {
        const char* clip = ImGui::GetClipboardText();
        std::snprintf(g_token_buf, sizeof g_token_buf, "%s", clip ? clip : "");
    }
    ImGui::SameLine();
    if (ImGui::Button("SAVE") || enter) {
        note(moa_update::set_token(g_token_buf) ? "TOKEN SAVED" : "THAT DOES NOT LOOK LIKE A GITHUB TOKEN");
        std::memset(g_token_buf, 0, sizeof g_token_buf);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("CANCEL") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        std::memset(g_token_buf, 0, sizeof g_token_buf);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

static void draw_updates_page(const Gfx& g, const Input& in) {
    using moa_update::State;
    using moa_update::Channel;
    const moa_update::Snapshot s = moa_update::snapshot();
    const bool busy = s.state == State::Checking || s.state == State::Downloading ||
                      s.state == State::Applying;
    std::string why;
    const bool can = moa_update::supported(&why);
    const bool dev = moa_update::dev_available();
    const bool token = dev && moa_update::has_token();

    enum Act { CHECK, APPLY, DEV_CHECK, DEV_APPLY, TOKEN, FORGET };
    struct Row { Act act; const char* label; bool enabled; };
    Row rows[6];
    int n = 0;
    rows[n++] = { CHECK, "CHECK FOR UPDATE", !busy };
    rows[n++] = { APPLY, "DOWNLOAD AND APPLY UPDATE",
                  !busy && can && s.state == State::Available && s.channel == Channel::Release };
    if (dev) {
        rows[n++] = { DEV_CHECK, "CHECK FOR DEV BUILD", !busy && token };
        rows[n++] = { DEV_APPLY, "DOWNLOAD AND APPLY DEV BUILD",
                      !busy && can && token && s.state == State::Available && s.channel == Channel::Dev };
        rows[n++] = { TOKEN, token ? "REPLACE GITHUB TOKEN" : "SET GITHUB TOKEN", !busy };
        if (token) rows[n++] = { FORGET, "FORGET GITHUB TOKEN", !busy };
    }

    move_cursor(PAGE_UPDATES, n, in, n);
    int& cur = g_state.cursor[PAGE_UPDATES];
    header(g, "UPDATES", "NOTHING IS DOWNLOADED UNTIL YOU ASK");
    const float sel_h = 14.0f + kRowH * n + 10.0f;
    panel(g, 24.0f, 76.0f, 592.0f, sel_h, "SELECT");
    int clicked = -1;
    for (int i = 0; i < n; ++i) {
        bool hov = false;
        if (row(g, 25.0f, 90.0f + kRowH * i, 590.0f, cur == i, rows[i].enabled,
                rows[i].label, "", false, false, &hov))
            clicked = i;
        if (hov && (ImGui::GetIO().MouseDelta.x != 0.0f || ImGui::GetIO().MouseDelta.y != 0.0f))
            cur = i;
    }

    const float vy = 76.0f + sel_h + 16.0f;
    panel(g, 24.0f, vy, 592.0f, 416.0f - vy, "VERSION");
    float y = vy + 16.0f;
    text(g, 38.0f, y, 11.0f, kDim, "INSTALLED");
    std::string installed = s.current == "dev" ? "DEVELOPMENT BUILD" : s.current;
    if (!s.current_dev.empty()) installed += "  (DEV BUILD FROM ACTIONS)";
    text(g, 170.0f, y, 11.0f, kText, installed.c_str());
    y += 19.0f;
    text(g, 38.0f, y, 11.0f, kDim, "NEWEST");
    text(g, 170.0f, y, 11.0f, s.latest.empty() ? kDim : kText,
         s.latest.empty() ? "NOT CHECKED YET" : s.latest.c_str());
    y += 23.0f;
    if (!s.message.empty()) {
        const ImU32 c = s.state == State::Failed ? kWarn
                      : (s.state == State::Available) ? kHi
                      : (s.state == State::UpToDate || s.state == State::Done) ? kOn : kText;
        text(g, 38.0f, y, 11.0f, c, s.message.c_str(), 560.0f);
    }
    y += 22.0f;
    if (s.state == State::Downloading) {
        const float w = 540.0f;
        const float f = s.total ? std::min(1.0f, (float)((double)s.done / (double)s.total)) : 0.0f;
        g.dl->AddRect(at(g, 38.0f, y), at(g, 38.0f + w, y + 12.0f), kLine);
        g.dl->AddRectFilled(at(g, 39.0f, y + 1.0f), at(g, 39.0f + (w - 2.0f) * f, y + 11.0f), kHi);
        char b[80];
        std::snprintf(b, sizeof b, "%.1f / %.1f MB", s.done / 1048576.0, s.total / 1048576.0);
        text(g, 38.0f, y + 17.0f, 10.0f, kDim, b);
    } else if (s.state == State::Applying) {
        text(g, 38.0f, y, 10.0f, kWarn, "DO NOT CLOSE THE GAME WHILE THIS RUNS.");
    }
    y += 38.0f;
    if (!can) { text(g, 38.0f, y, 11.0f, kWarn, why.c_str(), 560.0f); y += 26.0f; }
    if (y < 360.0f)
        text(g, 38.0f, y, 11.0f, kText,
             "An update replaces only the game's own program files. Your memory "
             "cards, saves, installed mods, disc images and settings are not touched. "
             "When an update is installed the game restarts by itself.", 560.0f);
    footer(g, "A / ENTER  SELECT      B / ESC  BACK");

    draw_token_popup();
    if (in.back) { g_state.updates_page = false; return; }
    if (busy) return;
    if (clicked >= 0) cur = clicked;
    if (!(in.confirm || clicked >= 0) || cur < 0 || cur >= n || !rows[cur].enabled) return;
    switch (rows[cur].act) {
        case CHECK:     moa_update::check(); break;
        case APPLY:     moa_update::download_and_apply(); break;
        case DEV_CHECK: moa_update::check_dev(); break;
        case DEV_APPLY: moa_update::download_and_apply(); break;
        case TOKEN:     g_token_popup = true; break;
        case FORGET:    moa_update::forget_token(); note("TOKEN FORGOTTEN"); break;
    }
}

/* ---- main page ------------------------------------------------------------- */

enum MainEntry { MAIN_START = 0, MAIN_VERSUS, MAIN_ROGUE, MAIN_SETTINGS, MAIN_MODS, MAIN_UPDATES, MAIN_EXIT };

static int enabled_mod_count(const LauncherModel* m, int* total_out) {
    int on = 0, total = 0;
    const RecompLauncherCModProvider* mods = m->mods;
    if (mods && mods->feature_count && mods->feature_get) {
        const int n = mods->feature_count(mods->ctx);
        for (int i = 0; i < n; ++i) {
            RecompLauncherCModFeature f;
            std::memset(&f, 0, sizeof f);
            if (!mods->feature_get(mods->ctx, i, &f)) continue;
            if (f.hidden && !f.enabled) continue;
            ++total;
            if (f.enabled) ++on;
        }
    }
    if (total_out) *total_out = total;
    return on;
}

static void start_game(LauncherModel* m) {
    /* The stock PLAY button's decisions, in the same order. A START GAME
     * choice (acg_start_set) only stands if the game really starts now. */
    if (launcher_model_bios_blocks_play(m)) {
        acg_start_set(ACG_START_NONE, 0, nullptr);
        launcher_model_bios_play_prompt(m);
    } else if (!launcher_model_can_launch(m)) {
        acg_start_set(ACG_START_NONE, 0, nullptr);
        if (m->setup_wizard_supported) m->setup_wizard_open = true;
        else { note("SELECT A DISC IMAGE FIRST"); sfx(moa_sound::SFX_REFUSED); }
    } else if (!launcher_model_prepare_rom_patch(m)) {
        acg_start_set(ACG_START_NONE, 0, nullptr);
        launcher_model_set_view(m, LNG_VIEW_MODS);
    } else {
        request_mod_commit_launch(m);
    }
}

/* ---- which disc this program is for -------------------------------------------
 *
 * Each disc version is its own program (CMakeLists.txt ACG_REGION). The US one
 * sits in RecompData/, the Japanese one in RecompData/Japan/; when the disc
 * found or picked is the other version's, the framework closes this program
 * and starts that one (regional/regional-launch.toml). ACG_REGION_JP is set
 * for the Japanese program.
 */
#ifndef ACG_REGION_JP
#define ACG_REGION_JP 0
#endif
#if ACG_REGION_JP
static const char* const kRegionTag = "MASTER OF ARENA  JAPAN";
#else
static const char* const kRegionTag = "MASTER OF ARENA";
#endif

/* ---- START GAME page ---------------------------------------------------------
 *
 *   LOAD GAME    pick Master of Arena data on memory card 1; the game opens on
 *                the hub with it loaded (no title, no memory card screens)
 *   NEW GAME     straight to the game's name entry
 *   LAUNCH GAME  the title screen; FULL BOOT off (default) skips the logos and
 *                intro movie, on plays the game's whole boot
 *
 * The choice goes to the game through acg_start_set() (game/acg_start.h);
 * game/acg_quick_start.c does the rest when the game would open its title.
 * Brought over from the old fork's START GAME page; the memory card reading
 * below is new.
 */

static bool g_full_boot = false;
static bool g_full_boot_read = false;

static std::string full_boot_file() {
    char* dir = SDL_GetPrefPath("", "Armored-Core-Gen-1");
    if (!dir) return std::string();
    std::string p = std::string(dir) + "full-boot.txt";
    SDL_free(dir);
    return p;
}

static bool full_boot() {
    if (!g_full_boot_read) {
        g_full_boot_read = true;
        const std::string p = full_boot_file();
        if (std::FILE* f = p.empty() ? nullptr : std::fopen(p.c_str(), "rb")) {
            char buf[4] = {0};
            g_full_boot = std::fread(buf, 1, 2, f) == 2 && buf[0] == 'o' && buf[1] == 'n';
            std::fclose(f);
        }
    }
    return g_full_boot;
}

static void set_full_boot(bool on) {
    g_full_boot = on;
    g_full_boot_read = true;
    const std::string p = full_boot_file();
    if (std::FILE* f = p.empty() ? nullptr : std::fopen(p.c_str(), "wb")) {
        std::fputs(on ? "on\n" : "off\n", f);
        std::fclose(f);
    }
}

/* One Master of Arena save on memory card 1, as the game's DATA LOAD lists it. */
struct CardSave {
    char letter = 0;                  /* BASLUS-01030A ... O */
    std::string ac, pilot;
    unsigned sorties = 0, credits = 0;
    std::vector<unsigned char> block; /* what LOAD reads (ACG_SAVE_BYTES) */
};
static std::vector<CardSave> g_card_saves;
static bool g_card_read_ok = false;

/* Names are Shift-JIS, two bytes a character, ended by '>' (or a zero).
 * Full-width letters, digits and a few symbols become their ASCII twins;
 * anything else (kana and so on) shows as '?'. */
static std::string card_name(const unsigned char* p) {
    std::string out;
    for (int i = 0; i < 9; ++i) {
        const unsigned hi = p[2 * i], lo = p[2 * i + 1];
        if (hi == 0 || hi == '>') break;
        char c = '?';
        if (hi >= 0x20 && hi < 0x7F) c = (char)hi;
        else if (hi == 0x82 && lo >= 0x4F && lo <= 0x58) c = (char)('0' + (lo - 0x4F));
        else if (hi == 0x82 && lo >= 0x60 && lo <= 0x79) c = (char)('A' + (lo - 0x60));
        else if (hi == 0x82 && lo >= 0x81 && lo <= 0x9A) c = (char)('A' + (lo - 0x81));
        else if (hi == 0x81 && lo == 0x40) c = ' ';
        else if (hi == 0x81 && lo == 0x44) c = '.';
        else if (hi == 0x81 && lo == 0x5B) c = '-';
        else if (hi == 0x81 && lo == 0x49) c = '!';
        else if (hi == 0x81 && lo == 0x48) c = '?';
        out.push_back(c);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

/* Memory card 1, the file the game will use (the framework hands the launcher
 * its real path). A raw 128 KB card image: block 0 holds the directory, one
 * 128-byte frame per block; each save here is one 8 KB block. */
static void read_card_saves(const LauncherModel* m) {
    g_card_saves.clear();
    g_card_read_ok = false;
    const char* path = m->s.memcard_path[0];
    if (!path || !path[0]) return;
    std::vector<unsigned char> mc(131072);
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return;
    const size_t got = std::fread(mc.data(), 1, mc.size(), f);
    std::fclose(f);
    if (got != mc.size() || mc[0] != 'M' || mc[1] != 'C') return;
    g_card_read_ok = true;
    for (int i = 1; i < 16; ++i) {
        const unsigned char* fr = mc.data() + i * 128;
        /* In use and the first block of its file; named BASLUS-01030 + A..O
         * (the game's own saves; Z is its emblem file). */
        if (fr[0] != 0x51 || std::memcmp(fr + 10, "BASLUS-01030", 12) != 0) continue;
        const char letter = (char)fr[22];
        if (letter < 'A' || letter > 'O' || fr[23] != 0) continue;
        const unsigned char* b = mc.data() + i * 8192;
        if (!acg_start_save_ok(b, ACG_SAVE_BYTES)) continue;
        CardSave s;
        s.letter = letter;
        s.ac = card_name(b + 0x1CC);
        s.pilot = card_name(b + 0x1DE);
        s.sorties = (unsigned)b[0x1A8] | (unsigned)b[0x1A9] << 8;
        s.credits = (unsigned)b[0x1C8] | (unsigned)b[0x1C9] << 8 |
                    (unsigned)b[0x1CA] << 16 | (unsigned)b[0x1CB] << 24;
        s.block.assign(b, b + ACG_SAVE_BYTES);
        g_card_saves.push_back(std::move(s));
    }
    std::sort(g_card_saves.begin(), g_card_saves.end(),
              [](const CardSave& a, const CardSave& b) { return a.letter < b.letter; });
}

static void open_start_page() {
    g_state.start_page = true;
    g_state.start_list = false;
    g_state.cursor[PAGE_START] = 0;
}

static void quick_start(LauncherModel* m, int mode, const unsigned char* save) {
    if (!acg_start_set(mode, full_boot() ? 1 : 0, save)) {
        note("THAT DATA COULD NOT BE READ");
        sfx(moa_sound::SFX_REFUSED);
        return;
    }
    g_state.start_page = g_state.start_list = false;   /* the main menu afterwards */
    start_game(m);
}

static void draw_start_list(LauncherModel* m, const Gfx& g, const Input& in) {
    const int count = (int)g_card_saves.size();
    const int visible = 10;
    move_cursor(PAGE_START_LOAD, count, in, visible);
    int& cur = g_state.cursor[PAGE_START_LOAD];
    header(g, "LOAD GAME", "MEMORY CARD 1");
    panel(g, 24.0f, 76.0f, 592.0f, 290.0f, "SELECT DATA");
    text(g, 55.0f, 84.0f, 10.0f, kDim, "NO.  AC NAME");
    text_right(g, 470.0f, 84.0f, 10.0f, kDim, "SORTIES");
    text_right(g, 600.0f, 84.0f, 10.0f, kDim, "CREDITS");
    int activate = -1;
    if (count == 0)
        text(g, 38.0f, 104.0f, 11.0f, kDim,
             g_card_read_ok ? "NO MASTER OF ARENA DATA ON MEMORY CARD 1."
                            : "MEMORY CARD 1 COULD NOT BE READ.", 560.0f);
    const int top = g_state.top[PAGE_START_LOAD];
    for (int i = top; i < count && i < top + visible; ++i) {
        const CardSave& s = g_card_saves[(size_t)i];
        const float y = 98.0f + kRowH * (i - top);
        char label[64], sorties[16], credits[24];
        std::snprintf(label, sizeof label, "%c    %s", s.letter, upper(s.ac.c_str()).c_str());
        std::snprintf(sorties, sizeof sorties, "%u", s.sorties);
        std::snprintf(credits, sizeof credits, "%u C", s.credits);
        bool hovered = false;
        if (row(g, 25.0f, y, 590.0f, i == cur, true, label, credits, false, false, &hovered))
            activate = i;
        text_right(g, 470.0f, y + 6.0f, 13.0f, kText, sorties);
        if (hovered && (ImGui::GetIO().MouseDelta.x != 0.0f || ImGui::GetIO().MouseDelta.y != 0.0f))
            cur = i;
    }
    if (count > 0) {
        const CardSave& s = g_card_saves[(size_t)cur];
        char buf[96];
        panel(g, 24.0f, 374.0f, 592.0f, 44.0f, "DATA");
        std::snprintf(buf, sizeof buf, "PILOT %s      FILE BASLUS-01030%c",
                      upper(s.pilot.c_str()).c_str(), s.letter);
        text(g, 38.0f, 390.0f, 11.0f, kText, buf);
    }
    footer(g, "A / ENTER  LOAD      D-PAD  MOVE      B / ESC  BACK");
    if (in.back) { g_state.start_list = false; return; }
    if ((in.confirm || in.start) && count > 0) activate = cur;
    if (activate >= 0) quick_start(m, ACG_START_LOAD, g_card_saves[(size_t)activate].block.data());
}

static void draw_start_page(LauncherModel* m, const Gfx& g, const Input& in) {
    if (g_state.start_list) { draw_start_list(m, g, in); return; }
    enum { S_LOAD = 0, S_NEW, S_LAUNCH, S__COUNT };
    move_cursor(PAGE_START, S__COUNT, in, S__COUNT);
    int& cur = g_state.cursor[PAGE_START];
    header(g, "START GAME", kRegionTag);
    panel(g, 24.0f, 76.0f, 592.0f, 96.0f, "SELECT");
    static const char* const kLabels[S__COUNT] = { "LOAD GAME", "NEW GAME", "LAUNCH GAME" };
    int activate = -1;
    for (int i = 0; i < S__COUNT; ++i) {
        bool hovered = false;
        const float y = 90.0f + (kRowH + 2.0f) * i;
        /* The Japanese program boots every start the normal way for now
         * (game/acg_start_none.c): LOAD and NEW are dimmed, no FULL BOOT. */
        const bool offered = !ACG_REGION_JP || i == S_LAUNCH;
        if (row(g, 25.0f, y, 590.0f, i == cur, offered, kLabels[i],
                offered ? nullptr : "LATER", i == S_LAUNCH && !ACG_REGION_JP,
                full_boot(), &hovered))
            activate = i;
        if (i == S_LAUNCH && !ACG_REGION_JP)
            text_right(g, 25.0f + 590.0f - 60.0f, y + 7.0f, 10.0f, kDim, "FULL BOOT");
        if (hovered && (ImGui::GetIO().MouseDelta.x != 0.0f || ImGui::GetIO().MouseDelta.y != 0.0f))
            cur = i;
    }
#if ACG_REGION_JP
    static const char* const kInfo[S__COUNT] = {
        "Not on the Japanese disc yet: its game code sits at different addresses "
        "than the US disc's, and they still have to be found. Use LAUNCH GAME and "
        "the game's own LOAD.",
        "Not on the Japanese disc yet. Use LAUNCH GAME and the game's own NEW GAME.",
        "Start the Japanese disc the normal way: logos, intro movie and title screen.",
    };
#else
    static const char* const kInfo[S__COUNT] = {
        "Choose Master of Arena data on memory card 1 and go straight to the hub "
        "with it loaded: no title screen and no memory card screens. Saving in the "
        "game still goes to memory card 1.",
        "Start a new game at the name entry, without the title screen.",
        "Start at the title screen. With FULL BOOT off the logos and intro movie are "
        "skipped; switch it on (LEFT / RIGHT) to see the game's whole boot.",
    };
#endif
    panel(g, 24.0f, 190.0f, 592.0f, 120.0f, "INFO");
    text(g, 38.0f, 206.0f, 11.0f, kHi, kLabels[cur]);
    text(g, 38.0f, 228.0f, 11.0f, kText, kInfo[cur], 560.0f);
    footer(g, cur == S_LAUNCH && !ACG_REGION_JP
                  ? "A / ENTER  START      LEFT / RIGHT  FULL BOOT      B / ESC  BACK"
                  : "A / ENTER  SELECT      D-PAD  MOVE      B / ESC  BACK");
    if (in.back) { g_state.start_page = false; return; }
    if (cur == S_LAUNCH && !ACG_REGION_JP && (in.left || in.right)) {
        set_full_boot(!full_boot());
        sfx(moa_sound::SFX_CURSOR);
    }
    if (in.confirm || in.start) activate = cur;
    if (activate < 0) return;
    cur = activate;
    if (ACG_REGION_JP && activate != S_LAUNCH) {
        note("NOT ON THE JAPANESE DISC YET. USE LAUNCH GAME");
        sfx(moa_sound::SFX_REFUSED);
        return;
    }
    switch (activate) {
        case S_LOAD:
            read_card_saves(m);
            g_state.start_list = true;
            g_state.cursor[PAGE_START_LOAD] = 0;
            g_state.top[PAGE_START_LOAD] = 0;
            break;
        case S_NEW:    quick_start(m, ACG_START_NEW, nullptr); break;
        case S_LAUNCH: quick_start(m, ACG_START_TITLE, nullptr); break;
    }
}

/* ---- VERSUS page and ROGUE ARENA ----------------------------------------------
 *
 * The modes come with their own steps on the version ladder
 * (docs/versioning.md); until then the entries are here, say when they come,
 * and refuse politely. When a mode is ready, its row gets its own start (as
 * START GAME's rows do) and loses its "not ready" mark.
 */

struct ComingMode {
    const char* label;
    const char* when;      /* shown on the row */
    const char* info;
};

static const ComingMode kVersusModes[] = {
    { "LOCAL LINK", "0.4",
      "Two players on one PC, one monitor each: two copies of the game joined "
      "the way the PlayStation's link cable joined two consoles. Each player "
      "sets up their own AC. Coming in version 0.4." },
    { "NETWORK", "0.6",
      "Versus over your home network (LAN) or straight to an address a friend "
      "gives you (Direct IP). Coming in version 0.6." },
    { "PLAYER VS AI", "0.7",
      "Fight a computer-flown AC in the versus arenas. Coming in version 0.7." },
    { "AI VS AI", "0.7",
      "Watch two computer-flown ACs fight each other. Coming in version 0.7." },
};
static const int kVersusCount = (int)(sizeof kVersusModes / sizeof kVersusModes[0]);

static const char* const kRogueInfo =
    "A roguelike run: a random AC, a ladder of opponents from every arena, prizes "
    "along the way, and one loss ends it. Coming in version 0.8.";

static void not_ready(const char* when) {
    char buf[80];
    std::snprintf(buf, sizeof buf, "NOT READY YET. COMING IN VERSION %s", when);
    note(buf);
    sfx(moa_sound::SFX_REFUSED);
}

static void open_versus_page() {
    g_state.versus_page = true;
    g_state.cursor[PAGE_VERSUS] = 0;
}

static void draw_versus_page(const Gfx& g, const Input& in) {
    move_cursor(PAGE_VERSUS, kVersusCount, in, kVersusCount);
    int& cur = g_state.cursor[PAGE_VERSUS];
    header(g, "VERSUS", "MASTER OF ARENA");
    panel(g, 24.0f, 76.0f, 592.0f, 124.0f, "SELECT");
    int activate = -1;
    for (int i = 0; i < kVersusCount; ++i) {
        char when[24];
        std::snprintf(when, sizeof when, "COMING IN %s", kVersusModes[i].when);
        bool hovered = false;
        if (row(g, 25.0f, 90.0f + (kRowH + 2.0f) * i, 590.0f, i == cur, false,
                kVersusModes[i].label, when, false, false, &hovered))
            activate = i;
        if (hovered) {
            if (ImGui::GetIO().MouseDelta.x != 0.0f || ImGui::GetIO().MouseDelta.y != 0.0f) cur = i;
            /* row() ignores clicks on rows that aren't ready; say why instead. */
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) activate = i;
        }
    }
    panel(g, 24.0f, 218.0f, 592.0f, 120.0f, "INFO");
    text(g, 38.0f, 234.0f, 11.0f, kHi, kVersusModes[cur].label);
    text(g, 38.0f, 256.0f, 11.0f, kText, kVersusModes[cur].info, 560.0f);
    footer(g, "A / ENTER  SELECT      D-PAD  MOVE      B / ESC  BACK");
    if (in.back) { g_state.versus_page = false; return; }
    if (in.confirm || in.start) activate = cur;
    if (activate < 0) return;
    cur = activate;
    not_ready(kVersusModes[activate].when);
}

/* ---- finding the disc ------------------------------------------------------
 *
 * First time the menu is drawn with no usable disc: look beside the game and in
 * the usual subfolders for an image whose SYSTEM.CNF names this game, any file
 * name, and select it. The framework's own disc check (disc_identity.cpp) does
 * the reading, so the launcher and the game judge a disc the same way.
 */
/* MoA disc 1, this program's own version first. The other one is taken too:
 * picking it makes the framework start that version's program (see "which
 * disc this program is for"), so a player with only a Japanese disc lands in
 * the Japanese program. */
#if ACG_REGION_JP
static const char* const kDiscSerials[] = { "SLPS-01855", "SLUS-01030" };
#else
static const char* const kDiscSerials[] = { "SLUS-01030", "SLPS-01855" };
#endif

static bool disc_candidate(const std::filesystem::path& p) {
    std::string ext = p.extension().u8string();
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    if (ext == ".cue" || ext == ".chd" || ext == ".iso" || ext == ".img") return true;
    if (ext == ".bin") {   /* a .bin with a .cue beside it is found through the .cue */
        std::filesystem::path cue = p;
        cue.replace_extension(".cue");
        std::error_code ec;
        return !std::filesystem::exists(cue, ec);
    }
    return false;
}

/* The player's folder when the game sits in RecompData/ under it (the
 * Windows release layout, see launcher/acg_starter.c); empty in a flat build. */
static std::filesystem::path player_root() {
    std::filesystem::path data = std::filesystem::u8path(asset("")).lexically_normal();
    if (!data.has_filename()) data = data.parent_path();   /* trailing slash */
    /* The Japanese program sits one level deeper, in RecompData/Japan/. */
    if (data.filename() == "Japan" && data.parent_path().filename() == "RecompData")
        data = data.parent_path();
    if (data.filename() == "RecompData" && data.has_parent_path()) return data.parent_path();
    return {};
}

/* The first image that carries `serial`. */
static std::string find_disc_with(const char* serial) {
    namespace fs = std::filesystem;
    /* The player's folder first, then beside the game itself. */
    std::vector<fs::path> roots;
    if (!player_root().empty()) roots.push_back(player_root());
    roots.push_back(fs::u8path(asset("")));
    static const char* const kSub[] = { "", "disc", "discs", "Disc", "Discs", "iso", "roms", "games" };
    for (const fs::path& root : roots)
    for (const char* sub : kSub) {
        std::error_code ec;
        const fs::path dir = sub[0] ? root / sub : root;
        if (!fs::is_directory(dir, ec)) continue;
        std::vector<fs::path> files;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code fec;
            if (it->is_regular_file(fec) && disc_candidate(it->path())) files.push_back(it->path());
        }
        std::sort(files.begin(), files.end());
        for (const fs::path& f : files) {
            const PSXRecompV4::DiscIdentity id =
                PSXRecompV4::identify_disc(f, serial, 0, false, false);
            if (id.opened && id.has_header && id.serial_matches) return f.u8string();
        }
    }
    return std::string();
}

static std::string find_disc() {
    for (const char* serial : kDiscSerials) {
        const std::string found = find_disc_with(serial);
        if (!found.empty()) return found;
    }
    return std::string();
}

/* First run (no settings.toml beside the game yet): the player-only settings
 * game.toml cannot carry. Borderless fullscreen, full volume, 1440p (6x). The
 * rest of the first-run look (4:3, 1920x1440 window, nearest filtering, vsync,
 * dithering off) comes from game.toml. */
static void first_run_defaults(LauncherModel* m) {
    std::error_code ec;
    if (std::filesystem::exists(std::filesystem::u8path(asset("settings.toml")), ec)) return;
    launcher_model_set_fullscreen(m, 1);
    launcher_model_volume_delta(m, 100);
    if (launcher_model_internal_resolution_offered(m)) {
        const int n = launcher_model_internal_resolution_count(m);
        for (int i = 0; i < n; ++i) {
            const char* l = launcher_model_internal_resolution_label_at(m, i);
            if (l && (std::strstr(l, "1440") || std::strstr(l, "6x") || std::strstr(l, "6X"))) {
                launcher_model_set_internal_resolution(m, i);
                break;
            }
        }
    }
}

static void adopt_disc(LauncherModel* m) {
    if (g_state.disc_scanned || m->in_session) return;
    g_state.disc_scanned = true;
    first_run_defaults(m);
    if (launcher_model_can_launch(m)) return;
    const std::string found = find_disc();
    if (found.empty()) return;
    launcher_model_set_rom(m, found.c_str());
    /* The first-run wizard asks for exactly this; it has nothing left to do. */
    if (launcher_model_can_launch(m)) m->setup_wizard_open = false;
}

static void draw_main(LauncherModel* m, const Gfx& g, const Input& in) {
    struct Entry { MainEntry id; const char* label; const char* info; };
    Entry entries[10];
    int count = 0;
    entries[count++] = { MAIN_START, m->in_session ? "RESUME GAME" : "START GAME",
        m->in_session ? "Return to the game in progress."
                      : "Load your data straight into the hub, start a new game, "
                        "or go to the title screen." };
    if (!m->in_session) {
        entries[count++] = { MAIN_VERSUS, "VERSUS",
            "Two-player and computer-player fights: local link on two monitors, "
            "network, player vs AI and AI vs AI. Not ready yet; see what's coming." };
        entries[count++] = { MAIN_ROGUE, "ROGUE ARENA", kRogueInfo };
    }
    entries[count++] = { MAIN_SETTINGS, "SETTINGS",
        "Display, sound and controls." };
    if (m->mods)
        entries[count++] = { MAIN_MODS, "MODS",
            "Switch installed mods on or off, and install new ones." };
    if (!m->in_session)
        entries[count++] = { MAIN_UPDATES, "UPDATES",
            "Look for a newer version and install it when you choose. Nothing "
            "happens by itself, and your saves, mods and discs are not touched." };
    entries[count++] = { MAIN_EXIT, m->in_session ? "QUIT GAME" : "EXIT",
        m->in_session ? "Close the game." : "Close the launcher." };

    move_cursor(PAGE_MAIN, count, in, count);
    int& cur = g_state.cursor[PAGE_MAIN];

    header(g, kRegionTag, "SYSTEM MENU");
    panel(g, 24.0f, 76.0f, 270.0f, 340.0f, "SELECT");
    int activate = -1;
    const moa_update::Snapshot upd = moa_update::snapshot();
    for (int i = 0; i < count; ++i) {
        bool hovered = false;
        if (row(g, 25.0f, 90.0f + kRowH * i, 268.0f, i == cur, true,
                entries[i].label,
                entries[i].id == MAIN_UPDATES && update_live(upd) ? "NEW"
                    : entries[i].id == MAIN_ROGUE ? "0.8" : nullptr,
                false, false, &hovered))
            activate = i;
        if (hovered && (ImGui::GetIO().MouseDelta.x != 0.0f ||
                        ImGui::GetIO().MouseDelta.y != 0.0f))
            cur = i;
    }
    if (in.confirm) activate = cur;
    if (in.start) activate = 0;

    /* Status. */
    panel(g, 310.0f, 76.0f, 306.0f, 150.0f, "STATUS");
    {
        float y = 92.0f;
        char buf[160];
        const bool ready = launcher_model_can_launch(m);
        text(g, 324.0f, y, 11.0f, kDim, "DISC");
        text(g, 424.0f, y, 11.0f, ready ? kOn : kWarn, ready ? "READY" : "NOT FOUND");
        y += 19.0f;
        text(g, 324.0f, y, 11.0f, kDim, "CONTROLLER");
        std::snprintf(buf, sizeof buf, "%s",
                      upper(launcher_model_player_src_label(m, 0)).c_str());
        text(g, 424.0f, y, 11.0f, kText, buf);
        y += 19.0f;
        text(g, 324.0f, y, 11.0f, kDim, "MEMORY CARD");
        std::snprintf(buf, sizeof buf, "%u / 15 BLOCKS",
                      (unsigned)launcher_model_memcard_blocks_used(m, 0));
        text(g, 424.0f, y, 11.0f, kText, buf);
        y += 19.0f;
        if (m->mods) {
            int total = 0;
            const int on = enabled_mod_count(m, &total);
            text(g, 324.0f, y, 11.0f, kDim, "MODS");
            std::snprintf(buf, sizeof buf, "%d ACTIVE OF %d", on, total);
            text(g, 424.0f, y, 11.0f, kText, buf);
            y += 19.0f;
        }
        text(g, 324.0f, y, 11.0f, kDim, "DISPLAY");
        std::snprintf(buf, sizeof buf, "%s  %s",
                      upper(launcher_model_aspect_label(m)).c_str(),
                      upper(launcher_model_fullscreen_label(m)).c_str());
        text(g, 424.0f, y, 11.0f, kText, buf);
        y += 19.0f;
        if (update_live(upd)) {
            text(g, 324.0f, y, 11.0f, kDim, "UPDATE");
            std::snprintf(buf, sizeof buf, "AVAILABLE  v%s", upd.latest.c_str());
            text(g, 424.0f, y, 11.0f, kHi, buf);
        } else if (upd.state == moa_update::State::Done) {
            text(g, 324.0f, y, 11.0f, kDim, "UPDATE");
            text(g, 424.0f, y, 11.0f, kOn, "INSTALLED. RESTART");
        }
    }

    panel(g, 310.0f, 242.0f, 306.0f, 174.0f, "INFO");
    text(g, 324.0f, 258.0f, 11.0f, kHi, entries[cur].label);
    text(g, 324.0f, 280.0f, 11.0f, kText, entries[cur].info, 278.0f);

    footer(g, "A / ENTER  SELECT      D-PAD  MOVE      START  START GAME");

    if (activate < 0) return;
    cur = activate;
    switch (entries[activate].id) {
        case MAIN_START:
            if (m->in_session) start_game(m);   /* RESUME GAME */
            else open_start_page();
            break;
        case MAIN_UPDATES:  open_updates_page(); break;
        case MAIN_VERSUS:   open_versus_page(); break;
        case MAIN_ROGUE:    not_ready("0.8"); break;
        case MAIN_SETTINGS: launcher_model_set_view(m, LNG_VIEW_SETTINGS); break;
        case MAIN_MODS:     launcher_model_set_view(m, LNG_VIEW_MODS); break;
        case MAIN_EXIT:     m->action = LNG_ACTION_QUIT; break;
    }
}

/* ---- settings pages ---------------------------------------------------------
 *
 * Every player setting in the Master of Arena look, on four tabs (L1 / R1, or
 * Q / E, switch between them). Each row is bound to one of two sources:
 *   - a launcher-model call (aspect, window, renderer, filters, volume, ...),
 *     the same calls the stock page makes; or
 *   - one of the framework's player settings (package psx.runtime.settings,
 *     psxrecomp's player_settings.h), read and written through the mod
 *     provider and saved straight to settings.toml by the framework.
 * A row only shows when this build has its source, so a framework bump that
 * drops or renames a setting hides the row instead of breaking the page.
 * Where the launcher model and the framework both own a setting (texture
 * filter, screen model, scanlines, HQ sound, rewind), only the model row is
 * listed, as on the stock page, so the two never fight over one value.
 * Left out on purpose: free-text and raw tuning values (post-effect chain,
 * PGXP tolerance and depth threshold, line limits), JogCon, mouse, BIOS-HLE
 * internals and the projection scale. STOCK SETTINGS PAGE on ADVANCED still
 * reaches all of them, plus hotkeys.
 */

enum SettingsTab { TAB_DISPLAY = 0, TAB_PICTURE, TAB_SYSTEM, TAB_ADVANCED, TAB__COUNT };
static const char* const kTabNames[TAB__COUNT] = { "DISPLAY", "PICTURE", "SOUND / SYSTEM", "ADVANCED" };
static int g_tab = TAB_DISPLAY;
static int g_tab_cursor[TAB__COUNT] = {0};
static int g_tab_top[TAB__COUNT] = {0};

struct Setting {
    int         tab;
    const char* label;
    const char* detail;
    /* Framework setting id ("video.dithering"); null for a model row. */
    const char* opt;
    /* Number rows: the stops LEFT / RIGHT walk through, comma separated.
     * Null for a boolean, a choice, or an integer that steps by one. */
    const char* stops;
    /* Model rows only. */
    bool        (*offered)(LauncherModel*);
    const char* (*value)(LauncherModel*);
    /* dir: 0 confirm, -1 left, +1 right. */
    void        (*step)(LauncherModel*, int dir);
};

/* ---- framework settings through the mod provider ---- */

static const char kRuntimeSettings[] = "psx.runtime.settings";

static bool rt_find(LauncherModel* m, const char* id, RecompLauncherCModFeature& f,
                    RecompLauncherCModOption& o) {
    const RecompLauncherCModProvider* p = m->mods;
    if (!p || !p->feature_count || !p->feature_get || !p->feature_option_get) return false;
    for (int i = 0, n = p->feature_count(p->ctx); i < n; ++i) {
        if (!p->feature_get(p->ctx, i, &f) || std::strcmp(f.package_id, kRuntimeSettings))
            continue;
        for (int j = 0; j < f.option_count; ++j)
            if (p->feature_option_get(p->ctx, f.package_id, f.id, j, &o) &&
                !std::strcmp(o.id, id))
                return true;
    }
    return false;
}

static bool rt_choice(LauncherModel* m, const RecompLauncherCModFeature& f,
                      const RecompLauncherCModOption& o, int index,
                      RecompLauncherCModChoice& out) {
    const RecompLauncherCModProvider* p = m->mods;
    std::memset(&out, 0, sizeof out);
    return p->feature_choice_get &&
           p->feature_choice_get(p->ctx, f.package_id, f.id, o.id, index, &out);
}

static bool rt_editable(LauncherModel* m) {
    return m->mods && m->mods->feature_set_option && launcher_model_can_open_mods(m);
}

static void rt_set(LauncherModel* m, const RecompLauncherCModFeature& f,
                   const RecompLauncherCModOption& o, const std::string& v) {
    if (!rt_editable(m)) { note("NEEDS A VERIFIED DISC FIRST"); return; }
    if (!m->mods->feature_set_option(m->mods->ctx, f.package_id, f.id, o.id, v.c_str())) {
        mod_note_error(m);
        note(m->mod_status);
    }
}

/* The shown value: ON / OFF, the choice's label, the number, or DEFAULT when
 * the player never chose one (the framework or game.toml decides then). */
static std::string rt_value(LauncherModel* m, const Setting& s) {
    RecompLauncherCModFeature f{}; RecompLauncherCModOption o{};
    if (!rt_find(m, s.opt, f, o)) return "";
    if (!o.value[0]) return "Default";
    if (o.type == RECOMP_MOD_OPTION_BOOLEAN)
        return std::strcmp(o.value, "true") == 0 ? "On" : "Off";
    if (o.type == RECOMP_MOD_OPTION_CHOICE) {
        for (int c = 0; c < o.choice_count; ++c) {
            RecompLauncherCModChoice ch;
            if (rt_choice(m, f, o, c, ch) && !std::strcmp(ch.value, o.value))
                return ch.label[0] ? ch.label : ch.value;
        }
    }
    return o.value;
}

static std::vector<double> parse_stops(const char* stops) {
    std::vector<double> out;
    for (const char* p = stops; p && *p;) {
        char* end = nullptr;
        const double v = std::strtod(p, &end);
        if (end == p) break;
        out.push_back(v);
        p = (*end == ',') ? end + 1 : end;
    }
    return out;
}

static std::string number_text(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

static void rt_step(LauncherModel* m, const Setting& s, int dir) {
    RecompLauncherCModFeature f{}; RecompLauncherCModOption o{};
    if (!rt_find(m, s.opt, f, o) || o.disabled) return;
    const int d = dir < 0 ? -1 : 1;
    if (o.type == RECOMP_MOD_OPTION_BOOLEAN) {
        rt_set(m, f, o, std::strcmp(o.value, "true") == 0 ? "false" : "true");
        return;
    }
    if (o.type == RECOMP_MOD_OPTION_CHOICE) {
        if (o.choice_count <= 0) return;
        int cur = -1;
        for (int c = 0; c < o.choice_count; ++c) {
            RecompLauncherCModChoice ch;
            if (rt_choice(m, f, o, c, ch) && !std::strcmp(ch.value, o.value)) { cur = c; break; }
        }
        int next = cur < 0 ? (d > 0 ? 0 : o.choice_count - 1)
                           : (cur + d + o.choice_count) % o.choice_count;
        RecompLauncherCModChoice ch;
        if (rt_choice(m, f, o, next, ch)) rt_set(m, f, o, ch.value);
        return;
    }
    /* Word stops ("native,720p,..."): walk them in order. */
    if (s.stops && parse_stops(s.stops).empty()) {
        std::vector<std::string> words;
        std::string w;
        for (const char* p = s.stops;; ++p) {
            if (*p == ',' || !*p) { if (!w.empty()) words.push_back(w); w.clear(); if (!*p) break; }
            else w.push_back(*p);
        }
        if (words.empty()) return;
        int cur = -1;
        for (size_t i = 0; i < words.size(); ++i) if (words[i] == o.value) { cur = (int)i; break; }
        const int n = (int)words.size();
        const int next = cur < 0 ? (d > 0 ? 0 : n - 1) : (cur + d + n) % n;
        rt_set(m, f, o, words[next]);
        return;
    }
    /* Numbers: walk the row's stops, or step an integer by one. */
    const bool has_value = o.value[0] != '\0';
    const double cur = has_value ? std::strtod(o.value, nullptr) : 0.0;
    const std::vector<double> stops = parse_stops(s.stops);
    if (!stops.empty()) {
        int pick = -1;
        if (!has_value) pick = d > 0 ? 0 : (int)stops.size() - 1;
        else if (d > 0) {
            for (size_t i = 0; i < stops.size(); ++i) if (stops[i] > cur + 1e-9) { pick = (int)i; break; }
            if (pick < 0) pick = 0;                       /* wrap to the first stop */
        } else {
            for (size_t i = stops.size(); i-- > 0;) if (stops[i] < cur - 1e-9) { pick = (int)i; break; }
            if (pick < 0) pick = (int)stops.size() - 1;   /* wrap to the last stop */
        }
        rt_set(m, f, o, number_text(stops[pick]));
        return;
    }
    if (o.type == RECOMP_MOD_OPTION_INTEGER) {
        const int64_t step = o.step > 0 ? o.step : 1;
        int64_t v = has_value ? (int64_t)cur + d * step : (d > 0 ? o.min_value : o.max_value);
        if (v > o.max_value) v = o.min_value;
        if (v < o.min_value) v = o.max_value;
        rt_set(m, f, o, std::to_string((long long)v));
    }
}

/* ---- model rows ---- */

static bool always(LauncherModel*) { return true; }

static const char* on_off(bool v) { return v ? "On" : "Off"; }

static const char* volume_value(LauncherModel* m) {
    static char buf[16];
    std::snprintf(buf, sizeof buf, "%d", m->s.volume);
    return buf;
}

static void internal_resolution_step(LauncherModel* m, int dir) {
    const int n = launcher_model_internal_resolution_count(m);
    if (n <= 0) return;
    int i = launcher_model_internal_resolution_index(m) + (dir < 0 ? -1 : 1);
    if (i < 0) i = n - 1;
    if (i >= n) i = 0;
    launcher_model_set_internal_resolution(m, i);
}

static void renderer_step(LauncherModel* m, int dir) {
    const int n = launcher_model_renderer_count(m);
    if (n <= 1) return;
    int cur = 0;
    const char* now = launcher_model_renderer_label(m);
    for (int i = 0; i < n; ++i)
        if (now && !std::strcmp(now, launcher_model_renderer_label_at(m, i))) { cur = i; break; }
    launcher_model_set_renderer(m, (cur + (dir < 0 ? n - 1 : 1)) % n);
}

static const char* scanline_strength_value(LauncherModel* m) {
    static char buf[16];
    std::snprintf(buf, sizeof buf, "%d%%", launcher_model_scanline_strength_pct(m));
    return buf;
}

static void scanline_strength_step(LauncherModel* m, int dir) {
    int v = launcher_model_scanline_strength_pct(m) + (dir < 0 ? -10 : 10);
    v = (v / 10) * 10;
    if (v > 100) v = 10;
    if (v < 10) v = 100;
    launcher_model_set_scanline_strength_pct(m, v);
}

#define MODEL(tab, label, detail, offered, value, step) \
    { tab, label, detail, nullptr, nullptr, offered, value, step }
#define RUNTIME(tab, label, detail, opt, stops) \
    { tab, label, detail, opt, stops, nullptr, nullptr, nullptr }

static const Setting kSettings[] = {
    /* DISPLAY */
    MODEL(TAB_DISPLAY, "ASPECT", "The shape of the picture. 4:3 is the original; wider "
          "shapes show more of the battlefield.",
          always, [](LauncherModel* m) { return launcher_model_aspect_label(m); },
          [](LauncherModel* m, int) { launcher_model_cycle_aspect(m); }),
    MODEL(TAB_DISPLAY, "FULLSCREEN", "Off, borderless or exclusive fullscreen.",
          always, [](LauncherModel* m) { return launcher_model_fullscreen_label(m); },
          [](LauncherModel* m, int) { launcher_model_cycle_fullscreen(m); }),
    MODEL(TAB_DISPLAY, "WINDOW SIZE", "Size of the window when fullscreen is off.",
          [](LauncherModel* m) { return m->has_window_size; },
          [](LauncherModel* m) { return launcher_model_window_size_label(m); },
          [](LauncherModel* m, int) { launcher_model_cycle_window_size(m); }),
    MODEL(TAB_DISPLAY, "RENDERER", "How the picture is drawn. OpenGL is the fast, sharp "
          "one; software is the fallback for unusual graphics cards.",
          [](LauncherModel* m) { return m->has_renderer && launcher_model_renderer_offered(m) &&
                                        launcher_model_renderer_count(m) > 1; },
          [](LauncherModel* m) { return launcher_model_renderer_label(m); },
          renderer_step),
    MODEL(TAB_DISPLAY, "INTERNAL RESOLUTION", "How finely the 3D picture is drawn. Higher "
          "is sharper and needs a faster graphics card.",
          [](LauncherModel* m) { return launcher_model_internal_resolution_offered(m) &&
                                        launcher_model_internal_resolution_count(m) > 0; },
          [](LauncherModel* m) { return launcher_model_internal_resolution_label(m); },
          internal_resolution_step),
    MODEL(TAB_DISPLAY, "AUTOMATIC RESOLUTION", "Lowers the internal resolution for a "
          "moment when the graphics card falls behind, so the frame rate holds.",
          [](LauncherModel* m) { return m->has_automatic_quality; },
          [](LauncherModel* m) { return on_off(m->s.automatic_quality); },
          [](LauncherModel* m, int) { launcher_model_toggle_automatic_quality(m); }),
    MODEL(TAB_DISPLAY, "VSYNC", "Matches the picture to the display to avoid tearing.",
          [](LauncherModel* m) { return m->has_vsync; },
          [](LauncherModel* m) { return launcher_model_vsync_label(m); },
          [](LauncherModel* m, int) { launcher_model_cycle_vsync(m); }),
    MODEL(TAB_DISPLAY, "SMOOTH MOTION", "Draws extra frames between the game's own, so "
          "movement looks smoother on high refresh displays.",
          [](LauncherModel* m) { return m->has_frame_interp; },
          [](LauncherModel* m) { return on_off(m->s.frame_interp); },
          [](LauncherModel* m, int) { launcher_model_toggle_frame_interp(m); }),
    MODEL(TAB_DISPLAY, "SMOOTH MOTION RATE", "How many frames a second smooth motion "
          "aims for. Display refresh follows your screen.",
          [](LauncherModel* m) { return m->has_frame_interp && m->s.frame_interp; },
          [](LauncherModel* m) { return launcher_model_interp_fps_label(m); },
          [](LauncherModel* m, int) { launcher_model_cycle_interp_fps(m); }),

    /* PICTURE */
    MODEL(TAB_PICTURE, "TEXTURE FILTER", "Smooths textures. Nearest keeps the original "
          "sharp pixels.",
          [](LauncherModel* m) { return m->has_texture_filter; },
          [](LauncherModel* m) { return launcher_model_texture_filter_label(m); },
          [](LauncherModel* m, int) { launcher_model_toggle_texture_filter(m); }),
    RUNTIME(TAB_PICTURE, "DITHERING", "The PS1's fine dot pattern. Off is clean at high "
            "resolution; On is the original look; Fine keeps it but smaller.",
            "video.dithering", nullptr),
    RUNTIME(TAB_PICTURE, "STEADY POLYGONS", "Stops the PS1's shaking, wobbling 3D "
            "(PGXP vertex correction). Applies next start.",
            "video.geometry_correction", nullptr),
    RUNTIME(TAB_PICTURE, "STEADY TEXTURES", "Stops textures warping across walls and "
            "floors (perspective-correct texturing). Applies next start.",
            "video.perspective_texturing", nullptr),
    MODEL(TAB_PICTURE, "ANTIALIASING", "Smooths jagged 3D edges. Higher costs more of the "
          "graphics card.",
          [](LauncherModel* m) { return m->has_antialiasing; },
          [](LauncherModel* m) { return launcher_model_aa_label(m); },
          [](LauncherModel* m, int) { launcher_model_cycle_aa(m); }),
    MODEL(TAB_PICTURE, "MOVIE FILTER", "How the intro and ending movies are scaled up.",
          [](LauncherModel* m) { return m->has_fmv_filter; },
          [](LauncherModel* m) { return launcher_model_fmv_filter_label(m); },
          [](LauncherModel* m, int) { launcher_model_cycle_fmv_filter(m); }),
    MODEL(TAB_PICTURE, "SCREEN MODEL", "Imitates an old TV: CRT, composite cable or "
          "Trinitron. Raw is the clean picture.",
          [](LauncherModel* m) { return m->has_screen_kind; },
          [](LauncherModel* m) { return launcher_model_screen_kind_label(m); },
          [](LauncherModel* m, int) { launcher_model_cycle_screen_kind(m); }),
    MODEL(TAB_PICTURE, "SCANLINES", "Dark lines between picture lines, as on a TV.",
          [](LauncherModel* m) { return m->has_scanlines; },
          [](LauncherModel* m) { return on_off(m->s.scanlines); },
          [](LauncherModel* m, int) { launcher_model_toggle_scanlines(m); }),
    MODEL(TAB_PICTURE, "SCANLINE STRENGTH", "How dark the scanlines are.",
          [](LauncherModel* m) { return m->has_scanlines && m->s.scanlines; },
          scanline_strength_value, scanline_strength_step),

    /* SOUND / SYSTEM */
    MODEL(TAB_SYSTEM, "VOLUME", "Overall sound level.",
          always, volume_value,
          [](LauncherModel* m, int dir) { launcher_model_volume_delta(m, dir < 0 ? -5 : 5); }),
    MODEL(TAB_SYSTEM, "HIGH-QUALITY SOUND", "A cleaner mix of the PS1 sound chip.",
          [](LauncherModel* m) { return m->has_spu_hq; },
          [](LauncherModel* m) { return on_off(m->s.spu_hq); },
          [](LauncherModel* m, int) { launcher_model_toggle_spu_hq(m); }),
    MODEL(TAB_SYSTEM, "MENU SOUNDS", "The game's own menu sounds, read from your disc. "
          "They follow VOLUME.",
          always, [](LauncherModel*) { return on_off(moa_sound::enabled()); },
          [](LauncherModel*, int) { moa_sound::set_enabled(!moa_sound::enabled()); }),
    MODEL(TAB_SYSTEM, "BUTTON LAYOUT", "Choose the controller and change which button "
          "does what.",
          always, [](LauncherModel*) -> const char* { return ">"; },
          [](LauncherModel* m, int dir) { if (dir >= 0) launcher_model_open_config(m, 0); }),
    MODEL(TAB_SYSTEM, "REWIND", "Keeps the last few seconds so the Rewind hotkey can "
          "step back. Costs some speed.",
          [](LauncherModel* m) { return m->has_rewind_depth; },
          [](LauncherModel* m) { return on_off(m->s.rewind_enabled); },
          [](LauncherModel* m, int) { launcher_model_toggle_rewind_enabled(m); }),
    RUNTIME(TAB_SYSTEM, "SKIP BIOS INTRO", "Starts without the PlayStation logo. "
            "Applies next start.", "video.fast_boot", nullptr),
    MODEL(TAB_SYSTEM, "SKIP THIS MENU", "Start the game directly next time, without this "
          "menu.",
          always, [](LauncherModel* m) { return on_off(m->s.skip_launcher); },
          [](LauncherModel* m, int) { launcher_model_request_skip_toggle(m); }),
    MODEL(TAB_SYSTEM, "RESTORE DEFAULTS", "Put every setting back to how it was first "
          "installed.",
          [](LauncherModel* m) { return launcher_model_can_restore_defaults(m); },
          [](LauncherModel*) -> const char* { return ">"; },
          [](LauncherModel* m, int dir) { if (dir >= 0) launcher_model_request_restore_defaults(m); }),

    /* ADVANCED */
    RUNTIME(TAB_ADVANCED, "ANISOTROPIC FILTER", "Keeps floor and wall textures sharp "
            "at a slant.", "video.anisotropic_filtering", "1,2,4,8,16"),
    RUNTIME(TAB_ADVANCED, "TEXTURE MIPMAPS", "Calms shimmering on far-off textures.",
            "video.texture_lod", nullptr),
    RUNTIME(TAB_ADVANCED, "EDGE SMOOTHING", "A cheap smoothing pass over the finished "
            "picture (FXAA).", "video.antialiasing_mode", nullptr),
    RUNTIME(TAB_ADVANCED, "SMOOTH SCALING", "Blends pixels when the picture is scaled "
            "to the window.", "video.antialiasing", nullptr),
    RUNTIME(TAB_ADVANCED, "BLOOM", "Glow around bright lights and explosions. 0 is off.",
            "video.bloom", "0,0.25,0.5,0.75,1,1.5,2"),
    RUNTIME(TAB_ADVANCED, "ACCURATE BLENDING", "Draws see-through effects exactly as the "
            "PS1 did. Leave on unless a card struggles.", "video.accurate_blending", nullptr),
    RUNTIME(TAB_ADVANCED, "MOVIE COLOR SMOOTHING", "Softens blocky colour edges in the "
            "movies.", "video.fmv_chroma_smoothing", nullptr),
    RUNTIME(TAB_ADVANCED, "SUPERSAMPLING", "Draws above the internal resolution and "
            "scales down. Very costly.", "video.supersample", "1,1.5,2,3,4"),
    RUNTIME(TAB_ADVANCED, "LOWEST AUTO RESOLUTION", "The floor automatic resolution "
            "never drops below.", "video.dynamic_resolution_min",
            "native,720p,1080p,1440p"),
    RUNTIME(TAB_ADVANCED, "SMOOTH MOTION METHOD", "Redraw makes real in-between frames; "
            "reprojection is cheaper but can smear.", "video.frame_generation_method", nullptr),
    RUNTIME(TAB_ADVANCED, "TEXTURE BATCHING", "Speeds up drawing of repeating textures. "
            "Turn off if textures look wrong.", "video.texture_window_batching", nullptr),
    RUNTIME(TAB_ADVANCED, "PGXP CPU MODE", "Steadier geometry for more of the game's "
            "maths. Slower; try it if polygons still shake.", "video.pgxp_cpu_mode", nullptr),
    RUNTIME(TAB_ADVANCED, "PGXP PRECISE CULLING", "Fewer gaps at polygon edges. Needs "
            "steady polygons; never used in netplay.", "video.pgxp_culling", nullptr),
    RUNTIME(TAB_ADVANCED, "PGXP COLOR CORRECTION", "Corrects colour on steadied "
            "polygons.", "video.pgxp_color_correction", nullptr),
    RUNTIME(TAB_ADVANCED, "PGXP SEAM FIX", "Closes thin cracks between polygons.",
            "video.pgxp_seam", nullptr),
    RUNTIME(TAB_ADVANCED, "PGXP DEPTH BUFFER", "Sorts overlapping polygons by real depth.",
            "video.pgxp_depth_buffer", nullptr),
    RUNTIME(TAB_ADVANCED, "AUDIO BUFFER (MS)", "Lower reacts faster; raise it if sound "
            "crackles.", "audio.buffer_ms", "30,40,50,60,80,100,150,200,300,500"),
    RUNTIME(TAB_ADVANCED, "ANALOG ANTI-DEADZONE", "Pushes small stick movements past the "
            "game's own dead zone. 0 is off.", "controller.anti_deadzone",
            "0,1000,2000,3000,4000,6000,8000"),
    RUNTIME(TAB_ADVANCED, "SELECT SHORTCUTS", "Select plus a button opens launcher "
            "functions on pads without spare buttons.", "controller.select_shortcuts", nullptr),
    RUNTIME(TAB_ADVANCED, "LOW LATENCY INPUT", "Reads the pad again just before each "
            "frame is shown.", "video.low_latency_input", nullptr),
    MODEL(TAB_ADVANCED, "REWIND BUFFER", "How far back rewind can go.",
          [](LauncherModel* m) { return m->has_rewind_depth && m->s.rewind_enabled; },
          [](LauncherModel* m) { return launcher_model_rewind_depth_label(m); },
          [](LauncherModel* m, int) { launcher_model_cycle_rewind_depth(m); }),
    MODEL(TAB_ADVANCED, "REWIND INTERVAL", "How often rewind saves a step. Fewer "
          "frames is finer and slower.",
          [](LauncherModel* m) { return m->has_rewind_depth && m->s.rewind_enabled; },
          [](LauncherModel* m) { return launcher_model_rewind_interval_label(m); },
          [](LauncherModel* m, int) { launcher_model_cycle_rewind_interval(m); }),
    MODEL(TAB_ADVANCED, "STOCK SETTINGS PAGE", "The framework's own settings page: "
          "hotkeys, post-effects and raw tuning values not listed here.",
          always, [](LauncherModel*) -> const char* { return ">"; },
          [](LauncherModel*, int dir) { if (dir >= 0) g_state.classic_settings = true; }),
};

#undef MODEL
#undef RUNTIME

static bool setting_offered(LauncherModel* m, const Setting& s) {
    if (s.opt) {
        RecompLauncherCModFeature f{}; RecompLauncherCModOption o{};
        return rt_find(m, s.opt, f, o);
    }
    return s.offered(m);
}

/* False when the row is shown but cannot change now (an option another one
 * overrides, or framework settings before the disc is verified). */
static bool setting_enabled(LauncherModel* m, const Setting& s) {
    if (!s.opt) return true;
    RecompLauncherCModFeature f{}; RecompLauncherCModOption o{};
    return rt_find(m, s.opt, f, o) && !o.disabled && rt_editable(m);
}

static std::string setting_value(LauncherModel* m, const Setting& s) {
    if (s.opt) return rt_value(m, s);
    const char* v = s.value(m);
    return v ? v : "";
}

static void setting_step(LauncherModel* m, const Setting& s, int dir) {
    if (s.opt) rt_step(m, s, dir);
    else s.step(m, dir);
}

/* Tabs across the top; returns the tab clicked, or -1. */
static int tab_strip(const Gfx& g, float x, float y, float w) {
    const float tw = w / TAB__COUNT;
    int clicked = -1;
    for (int t = 0; t < TAB__COUNT; ++t) {
        const float tx = x + tw * t;
        const ImVec2 a = at(g, tx + 2.0f, y), b = at(g, tx + tw - 2.0f, y + 20.0f);
        const bool sel = t == g_tab;
        const bool hovered = ImGui::IsMouseHoveringRect(a, b, false);
        g.dl->AddRectFilled(a, b, sel ? IM_COL32(240, 160, 32, 60) : kPanel);
        g.dl->AddRect(a, b, sel ? kHi : kLine);
        const float lw = text_width(g, 11.0f, kTabNames[t]);
        text(g, tx + (tw - lw) * 0.5f, y + 4.0f, 11.0f, sel ? kBright : kDim, kTabNames[t]);
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) clicked = t;
    }
    return clicked;
}

static void draw_settings_page(LauncherModel* m, const Gfx& g, const Input& in) {
    /* Tabs: L1 / R1 (or Q / E), or a click. */
    if (in.tab_prev) g_tab = (g_tab + TAB__COUNT - 1) % TAB__COUNT;
    if (in.tab_next) g_tab = (g_tab + 1) % TAB__COUNT;
    if (in.tab_prev || in.tab_next) sfx(moa_sound::SFX_WINDOW);

    const Setting* rows[sizeof kSettings / sizeof kSettings[0]];
    int count = 0;
    for (const Setting& s : kSettings)
        if (s.tab == g_tab && setting_offered(m, s)) rows[count++] = &s;

    /* Each tab keeps its own cursor; move_cursor works on the page slot. */
    const int visible = 11;
    g_state.cursor[PAGE_SETTINGS] = g_tab_cursor[g_tab];
    g_state.top[PAGE_SETTINGS] = g_tab_top[g_tab];
    move_cursor(PAGE_SETTINGS, count, in, visible);
    int& cur = g_state.cursor[PAGE_SETTINGS];
    const int top = g_state.top[PAGE_SETTINGS];

    header(g, "SETTINGS", "OPTIONAL SYSTEMS");
    const int tab_click = tab_strip(g, 24.0f, 58.0f, 592.0f);
    panel(g, 24.0f, 84.0f, 340.0f, 332.0f, kTabNames[g_tab]);
    int clicked = -1;
    for (int i = top; i < count && i < top + visible; ++i) {
        const std::string v = setting_value(m, *rows[i]);
        const bool enabled = setting_enabled(m, *rows[i]);
        const bool on = same_word(v.c_str(), "On") || same_word(v.c_str(), "Enabled");
        const bool off = same_word(v.c_str(), "Off") || same_word(v.c_str(), "Disabled");
        std::string shown;
        if (!on && !off && !v.empty()) {
            shown = upper(v.c_str());
            if (shown != ">") shown = "< " + shown + " >";
        }
        bool hovered = false;
        if (row(g, 25.0f, 98.0f + kRowH * (i - top), 338.0f, i == cur, enabled,
                rows[i]->label, shown.c_str(), on || off, on, &hovered))
            clicked = i;
        if (hovered && (ImGui::GetIO().MouseDelta.x != 0.0f ||
                        ImGui::GetIO().MouseDelta.y != 0.0f))
            cur = i;
    }
    if (count > visible) {
        if (top > 0) text_right(g, 356.0f, 88.0f, 10.0f, kDim, "MORE ^");
        if (top + visible < count) text_right(g, 356.0f, 398.0f, 10.0f, kDim, "MORE v");
    }

    panel(g, 380.0f, 84.0f, 236.0f, 332.0f, "DETAIL");
    if (count > 0) {
        text(g, 394.0f, 100.0f, 11.0f, kHi, rows[cur]->label);
        text(g, 394.0f, 122.0f, 11.0f, kText, rows[cur]->detail, 208.0f);
        if (rows[cur]->opt && !setting_enabled(m, *rows[cur]))
            text(g, 394.0f, 330.0f, 10.0f, kWarn,
                 rt_editable(m) ? "SET BY ANOTHER OPTION" : "NEEDS A VERIFIED DISC FIRST", 208.0f);
        char pos[32];
        std::snprintf(pos, sizeof pos, "ITEM %02d / %02d", cur + 1, count);
        text_right(g, 604.0f, 396.0f, 10.0f, kDim, pos);
    }
    footer(g, "A  CHANGE    LEFT / RIGHT  ADJUST    L1 / R1  TAB    B  BACK");

    g_tab_cursor[g_tab] = cur;
    g_tab_top[g_tab] = g_state.top[PAGE_SETTINGS];

    if (tab_click >= 0) { if (tab_click != g_tab) sfx(moa_sound::SFX_WINDOW); g_tab = tab_click; return; }
    if (clicked >= 0) {
        cur = clicked; g_tab_cursor[g_tab] = cur;
        if (setting_enabled(m, *rows[cur])) setting_step(m, *rows[cur], 0);
        else sfx(moa_sound::SFX_REFUSED);
        return;
    }
    if (count > 0 && setting_enabled(m, *rows[cur])) {
        if (in.confirm)      setting_step(m, *rows[cur], 0);
        else if (in.right)   setting_step(m, *rows[cur], 1);
        else if (in.left)    setting_step(m, *rows[cur], -1);
        if (in.left || in.right) sfx(moa_sound::SFX_CURSOR);
    } else if (count > 0 && (in.confirm || in.left || in.right)) {
        sfx(moa_sound::SFX_REFUSED);
    }
    if (in.back) launcher_model_set_view(m, LNG_VIEW_DASHBOARD);
}

/* ---- mods page --------------------------------------------------------------- */

/* The drop folder for new mod packages: Mods/ in the player's folder (the
 * Windows release layout), or mods/new beside the game in a flat build. The
 * game's own mods (bundled and installed) stay in its mods/ folder either way;
 * installing copies a package there. */
static std::string new_mods_dir() {
    const std::filesystem::path root = player_root();
    if (!root.empty()) return (root / "Mods").u8string();
    return asset("mods/new");
}

static const char* archive_extension(const LauncherModel* m) {
    return (m->mods && m->mods->archive_extension && m->mods->archive_extension[0])
               ? m->mods->archive_extension : ".psxmod";
}

static void scan_new_packages(const LauncherModel* m) {
    const double now = ImGui::GetTime();
    if (now < g_state.next_scan) return;
    g_state.next_scan = now + 1.0;
    g_state.new_packages.clear();
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::u8path(new_mods_dir());
    std::filesystem::create_directories(dir, ec);
    const std::string want = upper(archive_extension(m));
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::error_code fec;
        if (!it->is_regular_file(fec)) continue;
        if (upper(it->path().extension().u8string().c_str()) != want) continue;
        g_state.new_packages.push_back(it->path().filename().u8string());
        if (g_state.new_packages.size() >= 16) break;
    }
    std::sort(g_state.new_packages.begin(), g_state.new_packages.end());
}

static void install_new_package(LauncherModel* m, const std::string& name) {
    const RecompLauncherCModProvider* mods = m->mods;
    const std::filesystem::path src =
        std::filesystem::u8path(new_mods_dir()) / std::filesystem::u8path(name);
    const std::string path = src.u8string();
    if (!mods || !mods->install_archive ||
        !mods->install_archive(mods->ctx, path.c_str())) {
        mod_note_error(m);
        note(m->mod_status);
        return;
    }
    /* Installed: it is the game's copy now. Park the file so it is not
     * offered again, without deleting what the player put there. */
    std::error_code ec;
    std::filesystem::path done = src;
    done += ".installed";
    std::filesystem::rename(src, done, ec);
    g_state.next_scan = 0.0;
    note("INSTALLED. SWITCH IT ON IN THE LIST.");
}

static void remove_feature_package(LauncherModel* m, const RecompLauncherCModFeature& f) {
    const RecompLauncherCModProvider* mods = m->mods;
    if (!mods || !mods->package_count || !mods->package_get || !mods->remove_package)
        return;
    const int n = mods->package_count(mods->ctx);
    for (int i = 0; i < n; ++i) {
        RecompLauncherCModPackage p;
        std::memset(&p, 0, sizeof p);
        if (!mods->package_get(mods->ctx, i, &p)) continue;
        if (std::strcmp(p.id, f.package_id) != 0) continue;
        if (!p.removable) { note("BUILT IN: CANNOT BE REMOVED"); return; }
        if (!mods->remove_package(mods->ctx, p.id, p.version)) {
            mod_note_error(m);
            note(m->mod_status);
        } else {
            note("REMOVED");
        }
        return;
    }
}

static void draw_mods_page(LauncherModel* m, const Gfx& g, const Input& in) {
    const RecompLauncherCModProvider* mods = m->mods;
    std::vector<RecompLauncherCModFeature> features;
    if (mods && mods->feature_count && mods->feature_get) {
        const int n = mods->feature_count(mods->ctx);
        for (int i = 0; i < n; ++i) {
            RecompLauncherCModFeature f;
            std::memset(&f, 0, sizeof f);
            if (!mods->feature_get(mods->ctx, i, &f)) continue;
            if (f.hidden && !f.enabled) continue;
            features.push_back(f);
        }
    }
    scan_new_packages(m);

    /* One cursor over both lists: features, then the ADD rows. */
    enum AddRow { ADD_BROWSE = 0, ADD_FOLDER, ADD_CLASSIC, ADD__COUNT };
    const int nf = (int)features.size();
    const int nn = (int)g_state.new_packages.size();
    const int count = nf + nn + ADD__COUNT;
    const int visible_features = 7;
    /* The feature list scrolls; the ADD rows are always on screen. */
    {
        int& c = g_state.cursor[PAGE_MODS];
        int& t = g_state.top[PAGE_MODS];
        if (in.up)   c = (c + count - 1) % count;
        if (in.down) c = (c + 1) % count;
        if (c < 0) c = 0;
        if (c >= count) c = count - 1;
        if (c < nf) {
            if (c < t) t = c;
            if (c >= t + visible_features) t = c - visible_features + 1;
        }
        if (t > nf - visible_features) t = nf - visible_features;
        if (t < 0) t = 0;
    }
    int& cur = g_state.cursor[PAGE_MODS];
    const int top = g_state.top[PAGE_MODS];

    header(g, "MODS", "OPTIONAL PARTS");
    panel(g, 24.0f, 76.0f, 340.0f, 206.0f, "INSTALLED");
    int clicked = -1;
    for (int i = top; i < nf && i < top + visible_features; ++i) {
        const RecompLauncherCModFeature& f = features[i];
        bool hovered = false;
        if (row(g, 25.0f, 86.0f + kRowH * (i - top), 338.0f, i == cur,
                !f.has_error, upper(f.name).c_str(), nullptr, true,
                f.enabled != 0, &hovered))
            clicked = i;
        if (hovered && (ImGui::GetIO().MouseDelta.x != 0.0f ||
                        ImGui::GetIO().MouseDelta.y != 0.0f))
            cur = i;
    }
    if (nf == 0)
        text(g, 55.0f, 96.0f, 12.0f, kDim, "NO MODS INSTALLED");
    if (nf > top + visible_features)
        text(g, 55.0f, 268.0f, 9.0f, kDim, "MORE BELOW");
    if (top > 0)
        text_right(g, 350.0f, 268.0f, 9.0f, kDim, "MORE ABOVE");

    const float add_h = 14.0f + kRowH * (nn + ADD__COUNT > 5 ? 5 : nn + ADD__COUNT);
    panel(g, 24.0f, 298.0f, 340.0f, 118.0f, "ADD");
    (void)add_h;
    /* Up to four rows fit; new packages first, then the fixed entries. */
    const int add_total = nn + ADD__COUNT;
    int add_top = 0;
    if (cur >= nf) {
        const int rel = cur - nf;
        if (rel >= 4) add_top = rel - 3;
    }
    for (int i = add_top; i < add_total && i < add_top + 4; ++i) {
        const int index = nf + i;
        std::string label;
        const char* value = ">";
        if (i < nn) { label = g_state.new_packages[i]; value = "INSTALL >"; }
        else if (i - nn == ADD_BROWSE)  label = "BROWSE FOR A FILE";
        else if (i - nn == ADD_FOLDER)  label = "OPEN MODS FOLDER";
        else                            label = "MOD OPTIONS (FULL PAGE)";
        bool hovered = false;
        if (row(g, 25.0f, 306.0f + kRowH * (i - add_top), 338.0f, index == cur,
                true, label.c_str(), value, false, false, &hovered))
            clicked = index;
        if (hovered && (ImGui::GetIO().MouseDelta.x != 0.0f ||
                        ImGui::GetIO().MouseDelta.y != 0.0f))
            cur = index;
    }

    panel(g, 380.0f, 76.0f, 236.0f, 340.0f, "DETAIL");
    if (cur < nf) {
        const RecompLauncherCModFeature& f = features[cur];
        text(g, 394.0f, 92.0f, 11.0f, kHi, upper(f.name).c_str(), 208.0f);
        text(g, 394.0f, 128.0f, 11.0f, kText, f.description, 208.0f);
        char buf[200];
        float y = 300.0f;
        if (f.package_name[0]) {
            text(g, 394.0f, y, 10.0f, kDim, "PACKAGE");
            text(g, 394.0f, y + 13.0f, 10.0f, kText, f.package_name, 208.0f);
            y += 34.0f;
        }
        std::snprintf(buf, sizeof buf, "VERSION  %s",
                      f.package_version[0] ? f.package_version : "-");
        text(g, 394.0f, y, 10.0f, kDim, buf);
        y += 16.0f;
        if (f.status[0])
            text(g, 394.0f, y, 10.0f, f.has_error ? kWarn : kDim, f.status, 208.0f);
    } else if (cur - nf < nn) {
        text(g, 394.0f, 92.0f, 11.0f, kHi, "INSTALL");
        text(g, 394.0f, 114.0f, 11.0f, kText,
             "A mod package found in the Mods folder. "
             "Install copies it into the game and adds it to the list, "
             "switched off.", 208.0f);
    } else if (cur - nf - nn == ADD_BROWSE) {
        text(g, 394.0f, 92.0f, 11.0f, kHi, "BROWSE FOR A FILE");
        text(g, 394.0f, 114.0f, 11.0f, kText,
             "Pick a mod package anywhere on this computer. The file window "
             "belongs to the system and needs a mouse or keyboard.", 208.0f);
    } else if (cur - nf - nn == ADD_FOLDER) {
        text(g, 394.0f, 92.0f, 11.0f, kHi, "OPEN MODS FOLDER");
        text(g, 394.0f, 114.0f, 11.0f, kText,
             "Opens the folder for new mods. Mod packages placed there appear in "
             "this list, ready to install with the controller.", 208.0f);
    } else {
        text(g, 394.0f, 92.0f, 11.0f, kHi, "MOD OPTIONS");
        text(g, 394.0f, 114.0f, 11.0f, kText,
             "The full mods page, for mods that have settings of their own, "
             "version choices and search.", 208.0f);
    }
    {
        int total = 0;
        const int on = enabled_mod_count(m, &total);
        char pos[48];
        std::snprintf(pos, sizeof pos, "ACTIVE %02d / %02d   NEW %02d", on, total, nn);
        text_right(g, 604.0f, 396.0f, 10.0f, kDim, pos);
    }
    footer(g, "A / ENTER  TOGGLE OR INSTALL      X / DEL  REMOVE      B / ESC  BACK");

    const bool confirm = in.confirm || clicked >= 0;
    if (clicked >= 0) cur = clicked;
    if (confirm) {
        if (cur < nf) {
            const RecompLauncherCModFeature& f = features[cur];
            if (!mods->feature_enable ||
                !mods->feature_enable(mods->ctx, f.package_id, f.id, f.enabled ? 0 : 1)) {
                mod_note_error(m);
                note(m->mod_status);
            } else {
                m->mod_status[0] = '\0';
            }
        } else if (cur - nf < nn) {
            install_new_package(m, g_state.new_packages[cur - nf]);
        } else if (cur - nf - nn == ADD_BROWSE) {
            char pattern[64];
            std::snprintf(pattern, sizeof pattern, "*%s", archive_extension(m));
            ui_pick_file(m, "Install Mod Package", {pattern},
                         (mods && mods->archive_description)
                             ? mods->archive_description : "Mod package",
                         [m, mods](const char* path) {
                if (!path) return;
                if (!mods || !mods->install_archive ||
                    !mods->install_archive(mods->ctx, path)) {
                    mod_note_error(m);
                    note(m->mod_status);
                } else {
                    note("INSTALLED. SWITCH IT ON IN THE LIST.");
                }
            });
        } else if (cur - nf - nn == ADD_FOLDER) {
            if (!moa_menu_open_folder(new_mods_dir()))
                note("COULD NOT OPEN THE FOLDER");
        } else {
            g_state.classic_mods = true;
        }
    } else if (in.alt && cur < nf) {
        remove_feature_package(m, features[cur]);
    }
    if (in.back) launcher_model_set_view(m, LNG_VIEW_DASHBOARD);
}


/* ---- entry points -------------------------------------------------------------- */

/* True when this menu, not the stock page, draws the current view. */
static bool owns_view(const LauncherModel* m) {
    if (m->view != LNG_VIEW_DASHBOARD || m->in_session)
        g_state.start_page = g_state.start_list = g_state.versus_page = false;
    if (m->view != LNG_VIEW_DASHBOARD) g_state.updates_page = false;
    if (m->view == LNG_VIEW_DASHBOARD) {
        g_state.classic_settings = false;
        g_state.classic_mods = false;
        return true;
    }
    if (m->view == LNG_VIEW_SETTINGS) return !g_state.classic_settings;
    if (m->view == LNG_VIEW_MODS)     return !g_state.classic_mods && m->mods != nullptr;
    return false;
}

static void draw(LauncherModel* m, const LauncherTheme& th) {
    (void)th;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    Gfx g;
    g.dl = ImGui::GetWindowDrawList();
    g.font = ImGui::GetFont();
    g.s = std::min(vp->Size.x / kW, vp->Size.y / kH);
    if (g.s <= 0.0f) g.s = 1.0f;
    g.o = ImVec2(vp->Pos.x + (vp->Size.x - kW * g.s) * 0.5f,
                 vp->Pos.y + (vp->Size.y - kH * g.s) * 0.5f);

    /* Ground and grid across the whole window, not just the design area. */
    const ImVec2 a = vp->Pos, b = ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y);
    g.dl->PushClipRect(a, b, false);
    g.dl->AddRectFilled(a, b, kBg);
    const float step = 32.0f * g.s;
    for (float x = std::fmod(g.o.x, step); x < b.x; x += step)
        g.dl->AddLine(ImVec2(x, a.y), ImVec2(x, b.y), kGrid);
    for (float y = std::fmod(g.o.y, step); y < b.y; y += step)
        g.dl->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), kGrid);

    const Input in = read_input(m);
    if (launcher_model_can_launch(m) && m->rom_full[0]) moa_sound::set_disc(m->rom_full);
    g_sfx = -1;
    const int view_before = (int)m->view;
    const bool updates_before = g_state.updates_page;
    const bool start_before = g_state.start_page, list_before = g_state.start_list;
    const bool versus_before = g_state.versus_page;
    if (in.confirm) sfx(moa_sound::SFX_OK);
    if ((int)m->view != g_state.last_view) {   /* a note belongs to its page */
        g_state.last_view = (int)m->view;
        g_state.note[0] = '\0';
    }
    switch (m->view) {
        case LNG_VIEW_SETTINGS: draw_settings_page(m, g, in); break;
        case LNG_VIEW_MODS:     draw_mods_page(m, g, in); break;
        default: {
            adopt_disc(m);
            if (g_state.updates_page && !m->in_session) draw_updates_page(g, in);
            else if (g_state.start_page && !m->in_session) draw_start_page(m, g, in);
            else if (g_state.versus_page && !m->in_session) draw_versus_page(g, in);
            else draw_main(m, g, in);
            break;
        }
    }
    restart_after_update(m);
    /* Leaving a page is BACK; opening one is the window sound. */
    if ((int)m->view != view_before || g_state.updates_page != updates_before ||
        g_state.start_page != start_before || g_state.start_list != list_before ||
        g_state.versus_page != versus_before)
        sfx(in.back ? moa_sound::SFX_BACK : moa_sound::SFX_WINDOW);
    if (g_sfx >= 0) moa_sound::play(g_sfx, m->s.volume);
    g.dl->PopClipRect();
}

} /* namespace moa_menu */
