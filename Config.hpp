#pragma once
#include <string>

// ---------------------------------------------------------------------------
//  Every setting of the menu lives here. Saved to <mod save dir>/config.txt
// ---------------------------------------------------------------------------

struct Col {
    float r = 1.f, g = 1.f, b = 1.f, a = 1.f;
};

inline Col rgb(int r, int g, int b, float a = 1.f) {
    return Col{r / 255.f, g / 255.f, b / 255.f, a};
}

// Page patterns
enum Pattern { PatNone = 0, PatRuled, PatGrid, PatDots, PatHatch, PatStars, PatScan, PatHalftone, PatCount };

struct ThemeColors {
    Col panel;       // notebook page (top)
    Col panel2;      // notebook page (bottom) - same as panel = no gradient
    Col blob;        // the messy "paint" behind the page
    Col tab;         // side tabs
    Col tabActive;   // selected side tab
    Col tabText;     // text on the tabs
    Col outline;     // thick marker lines
    Col text;        // normal text
    Col textDim;     // hints
    Col accent;      // checkmarks, slider fill
    Col accentText;  // text drawn on top of accent
    Col field;       // boxes / inputs
    Col pattern;     // color of the page pattern (alpha = how strong)
    int patternKind = PatNone;
    bool glow = false;  // neon glow around lines
};

// The default look, taken from your drawing: grey page, light grey tabs,
// thick black marker lines and white "scribble" text.
inline ThemeColors sketchColors() {
    ThemeColors t;
    t.panel = rgb(99, 99, 99);
    t.panel2 = rgb(92, 92, 92);
    t.blob = rgb(99, 99, 99);
    t.tab = rgb(190, 191, 186);
    t.tabActive = rgb(229, 230, 224);
    t.tabText = rgb(22, 22, 22);
    t.outline = rgb(0, 0, 0);
    t.text = rgb(255, 255, 255);
    t.textDim = rgb(210, 210, 205);
    t.accent = rgb(255, 255, 255);
    t.accentText = rgb(28, 28, 28);
    t.field = rgb(82, 82, 82);
    t.pattern = rgb(0, 0, 0, 0.10f);
    t.patternKind = PatNone;
    t.glow = false;
    return t;
}

struct MenuConfig {
    // ---- menu & look -----------------------------------------------------
    int menuKey = 0x09;  // Tab
    float uiScale = 1.0f;
    float animSpeed = 1.0f;
    int themeIndex = 0;
    int fontIndex = 1;
    bool sketchy = true;
    float sketchAmount = 1.0f;  // how wobbly the pencil lines are
    bool doubleLines = true;    // draw lines twice, like a pencil sketch
    bool lineBoil = false;
    bool paintBlob = true;
    bool pencilShading = true;  // pencil hatching on the paint blob
    bool tape = true;           // tape on the page corners
    bool doodles = true;        // little doodles in the corners
    float bounce = 1.0f;        // how bouncy the animations are
    bool cascade = true;        // rows slide in one after another
    float outline = 3.0f;
    float rounding = 6.0f;
    bool softCursor = true;
    bool toasts = true;
    ThemeColors custom = sketchColors();

    // ---- bot --------------------------------------------------------------
    int botMode = 1;  // 0 = macro (clicks only), 1 = replay (clicks + frame fixes)
    int fixInterval = 1;
    bool blockInputOnPlay = true;
    bool autoStopAtEnd = true;
    char macroName[64] = "my-macro";
    int importOffset = 0;        // move imported clicks earlier/later (ticks)
    char importDir[512] = "";    // extra folder to look for other bots' macros
    char importSearch[64] = "";
    bool frameStepper = false;
    int stepKey = 'C';
    int stepToggleKey = -1;

    // ---- predict (the bot that beats levels by itself) ---------------------
    int predictTiming = 1;       // 0 early, 1 normal, 2 late
    int predictMargin = 24;      // safety margin after a click (ticks)
    int predictBudget = 14;      // thinking time per frame (ms)
    bool predictVerify = true;   // double-check from the start
    bool predictShow = true;     // show the bot's progress in the level
    int predictFormat = 0;       // 0 = .gdr (other bots), 1 = .skm (this menu)
    char predictDir[512] = "";   // "save to folder"
    char predictName[128] = "";

    // ---- player -----------------------------------------------------------
    bool noclip = false;
    bool noclipP1 = true;
    bool noclipP2 = true;
    bool deathFlash = true;
    int noclipKey = -1;
    bool safeMode = false;
    bool autoSafe = true;
    bool customRespawn = false;
    float respawnDelay = 0.f;
    bool jumpHack = false;
    bool autoclicker = false;
    int acHold = 2;
    int acRelease = 2;
    bool acP2 = false;
    bool noDeathEffect = false;
    bool noSpawnEffect = false;
    bool hidePlayer = false;
    bool noUnfocusPause = false;
    bool noNewBest = false;

    // ---- visual -----------------------------------------------------------
    bool hitboxes = false;
    bool hitboxOnDeath = false;
    bool hitboxTrail = false;
    int trailLen = 60;
    bool hitBorders = false;
    int hitBordersKey = -1;
    bool hbRainbow = false;
    bool hbOnlyPlayer = false;
    float borderWidth = 0.6f;
    bool hbFill = false;
    float hbFillAlpha = 0.25f;
    Col hbHazard = rgb(255, 70, 70);
    Col hbSolid = rgb(80, 150, 255);
    Col hbSpecial = rgb(90, 255, 120);
    Col hbOther = rgb(255, 255, 255);
    Col hbPlayer = rgb(255, 215, 60);
    bool playerBox = true;
    bool rainbow = false;
    float rainbowSpeed = 0.5f;
    bool rainbowTrail = true;
    bool noWaveTrail = false;
    bool solidWave = false;
    bool noTrail = false;
    bool hideAttempts = false;
    bool hidePercent = false;
    bool hideProgress = false;
    bool accuratePercent = false;
    int percentDecimals = 2;
    bool deathMarkers = false;
    bool clickMarkers = false;

    // ---- speed ------------------------------------------------------------
    bool speedhack = false;
    float speed = 1.f;
    bool speedAudio = true;
    bool speedOnlyInLevel = false;
    int speedKey = -1;
    bool fpsBypass = false;
    float fpsValue = 240.f;
    bool lockDelta = false;
    bool slowmo = false;        // hold a key for slow motion
    int slowmoKey = 0x10;       // Shift
    float slowmoSpeed = 0.3f;

    // ---- labels -----------------------------------------------------------
    bool lblFps = true;
    bool lblCps = false;
    bool lblNoclipAcc = true;
    bool lblNoclipDeaths = false;
    bool lblAttempts = false;
    bool lblBest = false;
    bool lblClock = false;
    bool lblTime = false;
    bool lblJumps = false;
    bool lblPlayer = false;
    bool lblBot = true;
    bool lblCheat = true;
    bool lblSpeed = true;
    bool lblLevel = false;
    bool lblDeaths = false;
    bool lblSession = false;
    bool fpsGraph = false;
    bool inputDisplay = false;
    float inputScale = 1.0f;
    int lblCorner = 0;
    float lblScale = 1.f;
    float lblAlpha = 0.9f;

    // ---- extra ------------------------------------------------------------
    bool noTransition = false;
    bool unlockAll = false;
    int restartKey = 'R';
    int practiceKey = -1;
    int screenshotKey = 0x78;   // F9
    bool muteUnfocused = false;

    // ---- render (video maker) --------------------------------------------
    int rEngine = 0;          // 0 = FFmpeg (best, needs install), 1 = built-in (fast, worse)
    int rBQuality = 1;        // built-in: 0 low, 1 medium, 2 high
    int rBFpsIdx = 1;         // built-in: 0 = 30 FPS, 1 = 60 FPS
    bool rBRealtime = true;   // built-in: record while the game runs at normal speed
    char ffmpegPath[512] = "";
    char renderDir[512] = "";
    int rResIdx = 1;
    int rW = 1920;
    int rH = 1080;
    int rFpsIdx = 1;
    int rEncIdx = 0;
    int rQuality = 18;
    int rPresetIdx = 3;
    char rExtra[256] = "";
    bool rMusic = true;
    float rMusicOffset = 0.f;
    float rEndPad = 3.f;
    bool rRestart = true;
    bool rAutoWithBot = false;
    bool rCloseMenu = true;   // close the menu when a video starts
};

MenuConfig& cfg();
void loadConfig();
void saveConfig();
void saveConfigIfChanged();
void resetConfig();
