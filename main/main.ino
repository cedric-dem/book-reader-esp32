#include <M5Unified.h>
#include <vector>
#include <string>

// --- THEMES & STYLING ---
struct Theme {
    const char* name;
    uint32_t bg;
    uint32_t fg;
    uint32_t accent;
};

Theme themes[] = {
    {"Dark",   TFT_BLACK, TFT_WHITE, TFT_DARKGREY},
    {"Light",  TFT_LIGHTGREY, TFT_BLACK, TFT_DARKGREY},
    {"Matrix", TFT_BLACK, TFT_GREEN, TFT_DARKGREEN},
    {"Pastel", 0xFCE4, 0x5146, 0xAD55}, // Custom 16-bit RGB565 approximations
    {"Sepia",  0xF799, 0x4208, 0x8410},
    {"Red",    TFT_BLACK, TFT_RED, 0x8000},
    {"Blue",   TFT_BLACK, TFT_BLUE, 0x0010},
    {"Amber",  TFT_BLACK, 0xFC00, 0x8400}
};
int currentThemeIndex = 0;

// --- LANGUAGES ---
enum Lang { LANG_EN, LANG_FR, LANG_ES };
Lang currentLang = LANG_EN;

const char* menuTexts[3][4] = {
    {"Books", "Options", "Theme: ", "Back"},
    {"Livres", "Options", "Thème : ", "Retour"},
    {"Libros", "Opciones", "Tema: ", "Atrás"}
};

// --- APP STATES ---
enum AppState { STATE_MENU, STATE_BOOKS_LIST, STATE_OPTIONS, STATE_READING };
AppState currentState = STATE_MENU;

int menuSelection = 0;
const int menuMaxItems = 3;

// --- DISPLAY BUFFER ---
// Everything is drawn into this off-screen canvas, then pushed to the LCD in
// one go. This avoids the flicker caused by fillScreen() hitting the panel
// directly before the text is redrawn.
M5Canvas canvas(&M5.Display);

// --- EXTERNAL BUTTONS ---
// Only pins exposed on the M5StickC Plus2 connectors are used here.
// GPIO 12/15 are the LCD's reset/data lines, 21 is the internal I2C bus,
// and 20/23 are not broken out, so they must not be used as buttons.
// Wire each button between the pin and GND (internal pull-ups are enabled).
// Set a pin to -1 to disable that button.
// Note: G0 is a boot pin - don't hold that button while powering on.
// Note: G25 and G36 share the same header pin, so only G25 is used.
const int PIN_BTN_BACK     = 26; // Back to menu
const int PIN_BTN_FASTER   = 32; // Faster (+10%)
const int PIN_BTN_SLOWER   = 33; // Slower (-10%)
const int PIN_BTN_FOCUS    = 25; // Focus mode toggle
const int PIN_BTN_BACK10   = 0;  // Back 10 words
const int PIN_BTN_FWD10    = -1; // Forward 10 words (no free pin left - assign one if you free up another)

const int extPins[] = {PIN_BTN_BACK, PIN_BTN_FASTER, PIN_BTN_SLOWER, PIN_BTN_FOCUS, PIN_BTN_BACK10, PIN_BTN_FWD10};
const int extPinCount = sizeof(extPins) / sizeof(extPins[0]);
bool extPrevPressed[extPinCount] = {false};
unsigned long extLastChange[extPinCount] = {0};

// Returns true once per press (falling edge, debounced), without blocking.
bool extPressed(int idx) {
    int pin = extPins[idx];
    if (pin < 0) return false;
    bool pressed = digitalRead(pin) == LOW;
    bool fired = false;
    if (pressed != extPrevPressed[idx] && millis() - extLastChange[idx] > 40) {
        extLastChange[idx] = millis();
        extPrevPressed[idx] = pressed;
        fired = pressed;
    }
    return fired;
}

// --- HARDCODED SAMPLE BOOK & READING ENGINE ---
std::vector<std::string> sampleWords = {
    "Welcome", "to", "your", "M5StickC", "Plus", "2", "speed", "reader.",
    "This", "custom", "firmware", "implements", "RSVP", "reading,",
    "flashing", "one", "word", "at", "a", "time", "right", "in", "the", "center.",
    "Longer", "words", "stay", "on", "screen", "slightly", "longer.",
    "Punctuation", "marks,", "like", "commas,", "add", "pause", "weight.",
    "Periods.", "double", "the", "delay.",
    "Use", "the", "side", "buttons", "to", "change", "speed,", "pause,", "or", "skip", "words."
};

int currentWordIndex = 0;
bool isPlaying = false;
int baseWPM = 300;         // Words per minute baseline
int speedMultiplierIdx = 5; // 5 = 100% (range 0 to 10: 50% to 150%)
float speeds[] = {0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f, 1.1f, 1.2f, 1.3f, 1.4f, 1.5f};
unsigned long lastWordTime = 0;
bool focusMode = false;     // Hides status/progress bars

// --- FUNCTION PROTOTYPES ---
void drawMenu();
void drawBooksList();
void drawOptions();
void drawReading();
void handleInput();
unsigned long calculateWordDelay(std::string word);

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    // Configure display orientation (Horizontal landscape fits M5StickC Plus 2 well for RSVP)
    M5.Display.setRotation(1);

    // Off-screen buffer the size of the display (240x135 in landscape, ~65 KB)
    canvas.setColorDepth(16);
    canvas.createSprite(M5.Display.width(), M5.Display.height());
    canvas.setTextSize(2);

    // External buttons: pin -> button -> GND
    for (int i = 0; i < extPinCount; i++) {
        if (extPins[i] >= 0) pinMode(extPins[i], INPUT_PULLUP);
    }

    // Initialize buttons & state
    currentWordIndex = 0;
    isPlaying = false;
}

void loop() {
    M5.update();
    handleInput();

    // Render current screen
    switch (currentState) {
        case STATE_MENU:
            drawMenu();
            break;
        case STATE_BOOKS_LIST:
            drawBooksList();
            break;
        case STATE_OPTIONS:
            drawOptions();
            break;
        case STATE_READING:
            drawReading();
            break;
    }

    // Send the finished frame to the display in one go
    canvas.pushSprite(0, 0);

    delay(30); // Prevent CPU thrashing
}

// --- INPUT HANDLING ---
void handleInput() {
    // M5StickC Plus 2 built-in buttons:
    // BtnA (Main face button below screen) -> Next / Down
    // BtnB (Side power/home style or lower button depending on model) -> Select / Action
    // BtnPWR -> Back / Return

    if (M5.BtnA.wasClicked()) {
        if (currentState == STATE_MENU) {
            menuSelection = (menuSelection + 1) % menuMaxItems;
        } else if (currentState == STATE_BOOKS_LIST) {
            currentState = STATE_READING;
            isPlaying = true;
        } else if (currentState == STATE_OPTIONS) {
            currentThemeIndex = (currentThemeIndex + 1) % 8;
        } else if (currentState == STATE_READING) {
            // Slower speed (-10%)
            if (speedMultiplierIdx > 0) speedMultiplierIdx--;
        }
    }

    if (M5.BtnB.wasClicked()) {
        if (currentState == STATE_MENU) {
            if (menuSelection == 0) currentState = STATE_BOOKS_LIST;
            else if (menuSelection == 1) currentState = STATE_OPTIONS;
            else if (menuSelection == 2) {
                // Toggle Language
                currentLang = (Lang)((currentLang + 1) % 3);
            }
        } else if (currentState == STATE_OPTIONS) {
            currentLang = (Lang)((currentLang + 1) % 3);
        } else if (currentState == STATE_READING) {
            // Toggle Play / Pause
            isPlaying = !isPlaying;
        }
    }

    // Long press or special check for going back
    if (M5.BtnPWR.wasClicked()) {
        if (currentState != STATE_MENU) {
            currentState = STATE_MENU;
            isPlaying = false;
        }
    }

    // External buttons (non-blocking, one action per press)
    if (extPressed(0)) { // Back / Save progress
        currentState = STATE_MENU;
        isPlaying = false;
    }
    if (extPressed(1)) { // Faster (+10%)
        if (currentState == STATE_READING && speedMultiplierIdx < 10) speedMultiplierIdx++;
    }
    if (extPressed(2)) { // Slower (-10%)
        if (currentState == STATE_READING && speedMultiplierIdx > 0) speedMultiplierIdx--;
    }
    if (extPressed(3)) { // Focus mode toggle (hide/show bars)
        if (currentState == STATE_READING) focusMode = !focusMode;
    }
    if (extPressed(4)) { // Back 10 words
        if (currentState == STATE_READING) currentWordIndex = max(0, currentWordIndex - 10);
    }
    if (extPressed(5)) { // Forward 10 words
        if (currentState == STATE_READING) currentWordIndex = min((int)sampleWords.size() - 1, currentWordIndex + 10);
    }
}

// --- RENDER SCREENS ---
void drawMenu() {
    Theme t = themes[currentThemeIndex];
    canvas.fillScreen(t.bg);
    canvas.setTextColor(t.fg, t.bg);

    canvas.setCursor(10, 10);
    canvas.println("== MAIN MENU ==");

    const char* items[3] = {"1. Books List", "2. Options"};
    for (int i = 0; i < 3; i++) {
        canvas.setCursor(20, 45 + (i * 25));
        if (i == menuSelection) {
            canvas.setTextColor(t.accent, t.bg);
            canvas.print("> ");
        } else {
            canvas.setTextColor(t.fg, t.bg);
            canvas.print("  ");
        }
        canvas.println(items[i]);
    }
}

void drawBooksList() {
    Theme t = themes[currentThemeIndex];
    canvas.fillScreen(t.bg);
    canvas.setTextColor(t.fg, t.bg);

    canvas.setCursor(10, 10);
    canvas.println("Books : 1 (Sample)");

    canvas.setCursor(20, 50);
    canvas.setTextColor(t.accent, t.bg);
    canvas.println("> Sample_Novel.epub");

    canvas.setTextColor(t.fg, t.bg);
    canvas.setCursor(10, 122);
    canvas.setTextSize(1);
    canvas.println("Press BtnA/BtnB to select. PWR to back.");
    canvas.setTextSize(2);
}

void drawOptions() {
    Theme t = themes[currentThemeIndex];
    canvas.fillScreen(t.bg);
    canvas.setTextColor(t.fg, t.bg);

    canvas.setCursor(10, 10);
    canvas.println("OPTIONS");

    canvas.setCursor(10, 50);
    canvas.printf("Theme: %s\n", t.name);

    canvas.setCursor(10, 80);
    canvas.printf("Lang: %s\n", currentLang == LANG_EN ? "EN" : (currentLang == LANG_FR ? "FR" : "ES"));

    canvas.setCursor(10, 122);
    canvas.setTextSize(1);
    canvas.println("BtnA: Next Theme | BtnB: Lang");
    canvas.setTextSize(2);
}

unsigned long calculateWordDelay(std::string word) {
    float baseDelay = (60.0f / baseWPM) * 1000.0f; // ms per word
    float currentSpeedMult = speeds[speedMultiplierIdx];

    // Length factor: longer words stay slightly longer
    float lengthFactor = 1.0f + (max(0, (int)word.length() - 5) * 0.05f);

    // Punctuation multipliers
    float punctFactor = 1.0f;
    if (!word.empty()) {
        char lastChar = word.back();
        if (lastChar == '.') punctFactor = 2.0f;
        else if (lastChar == ',' || lastChar == ';' || lastChar == ':') punctFactor = 1.5f;
    }

    return (unsigned long)(baseDelay * lengthFactor * punctFactor / currentSpeedMult);
}

void drawReading() {
    Theme t = themes[currentThemeIndex];
    canvas.fillScreen(t.bg);

    // Handle word pacing logic
    if (isPlaying && (millis() - lastWordTime > calculateWordDelay(sampleWords[currentWordIndex]))) {
        currentWordIndex++;
        if (currentWordIndex >= sampleWords.size()) {
            currentWordIndex = 0;
            isPlaying = false; // Stop at end of sample book
        }
        lastWordTime = millis();
    }

    // Draw Status / Progress Bars (if Focus Mode is off)
    if (!focusMode) {
        // Top progress bar (Chapter / Book progress)
        canvas.drawRect(10, 5, 220, 6, t.accent);
        int progressWidth = (int)((float)currentWordIndex / sampleWords.size() * 220);
        canvas.fillRect(10, 5, progressWidth, 6, t.accent);

        // Speed and status indicators at the bottom
        canvas.setTextSize(1);
        canvas.setCursor(10, 120);
        canvas.setTextColor(t.accent, t.bg);
        canvas.printf("Speed: %.0f%% | Word %d/%d", speeds[speedMultiplierIdx] * 100, currentWordIndex + 1, (int)sampleWords.size());
        canvas.setTextSize(2);
    }

    // Draw Current Word precisely in the center of the screen
    canvas.setTextColor(t.fg, t.bg);
    std::string currentWord = sampleWords[currentWordIndex];

    // Calculate string width for centering on 240x135 landscape display
    int textWidth = canvas.textWidth(currentWord.c_str());
    int centerX = max(5, (240 - textWidth) / 2);
    int centerY = 60;

    canvas.setCursor(centerX, centerY);
    canvas.print(currentWord.c_str());
}
