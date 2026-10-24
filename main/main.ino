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
    M5.Lcd.setRotation(1); 
    M5.Lcd.setTextSize(2);
    
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

    // Map external/mapped inputs if wired per your GPIO specs
    // Pin 26 (Top Right): Back / Save progress
    if (digitalRead(26) == LOW) {
        currentState = STATE_MENU;
        isPlaying = false;
        delay(200); // Debounce
    }
    // Pin 15 (Right Top): Faster (+10%)
    if (digitalRead(15) == LOW) {
        if (currentState == STATE_READING && speedMultiplierIdx < 10) speedMultiplierIdx++;
        delay(150);
    }
    // Pin 20 (Right Down): Slower (-10%)
    if (digitalRead(20) == LOW) {
        if (currentState == STATE_READING && speedMultiplierIdx > 0) speedMultiplierIdx--;
        delay(150);
    }
    // Pin 23 (Left Square): Focus mode toggle (hide/show bars)
    if (digitalRead(23) == LOW) {
        if (currentState == STATE_READING) {
            focusMode = !focusMode;
        }
        delay(200);
    }
    // Pin 12 (Right Left): Back 10 words
    if (digitalRead(12) == LOW) {
        if (currentState == STATE_READING) {
            currentWordIndex = max(0, currentWordIndex - 10);
        }
        delay(150);
    }
    // Pin 21 (Right Right): Forward 10 words
    if (digitalRead(21) == LOW) {
        if (currentState == STATE_READING) {
            currentWordIndex = min((int)sampleWords.size() - 1, currentWordIndex + 10);
        }
        delay(150);
    }
}

// --- RENDER SCREENS ---
void drawMenu() {
    Theme t = themes[currentThemeIndex];
    M5.Lcd.fillScreen(t.bg);
    M5.Lcd.setTextColor(t.fg, t.bg);
    
    M5.Lcd.setCursor(10, 10);
    M5.Lcd.println("=== SPEED READER ===");
    
    const char* items[3] = {"1. Books List", "2. Options", "3. Language"};
    for (int i = 0; i < 3; i++) {
        M5.Lcd.setCursor(20, 45 + (i * 25));
        if (i == menuSelection) {
            M5.Lcd.setTextColor(t.accent, t.bg);
            M5.Lcd.print("> ");
        } else {
            M5.Lcd.setTextColor(t.fg, t.bg);
            M5.Lcd.print("  ");
        }
        M5.Lcd.println(items[i]);
    }
}

void drawBooksList() {
    Theme t = themes[currentThemeIndex];
    M5.Lcd.fillScreen(t.bg);
    M5.Lcd.setTextColor(t.fg, t.bg);
    
    M5.Lcd.setCursor(10, 10);
    M5.Lcd.println("Books : 1 (Sample)");
    
    M5.Lcd.setCursor(20, 50);
    M5.Lcd.setTextColor(t.accent, t.bg);
    M5.Lcd.println("> Sample_Novel.epub");
    
    M5.Lcd.setTextColor(t.fg, t.bg);
    M5.Lcd.setCursor(10, 200);
    M5.Lcd.setTextSize(1);
    M5.Lcd.println("Press BtnA/BtnB to select. PWR to back.");
    M5.Lcd.setTextSize(2);
}

void drawOptions() {
    Theme t = themes[currentThemeIndex];
    M5.Lcd.fillScreen(t.bg);
    M5.Lcd.setTextColor(t.fg, t.bg);
    
    M5.Lcd.setCursor(10, 10);
    M5.Lcd.println("OPTIONS");
    
    M5.Lcd.setCursor(10, 50);
    M5.Lcd.printf("Theme: %s\n", t.name);
    
    M5.Lcd.setCursor(10, 80);
    M5.Lcd.printf("Lang: %s\n", currentLang == LANG_EN ? "EN" : (currentLang == LANG_FR ? "FR" : "ES"));

    M5.Lcd.setCursor(10, 200);
    M5.Lcd.setTextSize(1);
    M5.Lcd.println("BtnA: Next Theme | BtnB: Lang");
    M5.Lcd.setTextSize(2);
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
    M5.Lcd.fillScreen(t.bg);
    
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
        M5.Lcd.drawRect(10, 5, 220, 6, t.accent);
        int progressWidth = (int)((float)currentWordIndex / sampleWords.size() * 220);
        M5.Lcd.fillRect(10, 5, progressWidth, 6, t.accent);

        // Speed and status indicators at the bottom
        M5.Lcd.setTextSize(1);
        M5.Lcd.setCursor(10, 120);
        M5.Lcd.setTextColor(t.accent, t.bg);
        M5.Lcd.printf("Speed: %.0f%% | Word %d/%d", speeds[speedMultiplierIdx] * 100, currentWordIndex + 1, sampleWords.size());
        M5.Lcd.setTextSize(2);
    }

    // Draw Current Word precisely in the center of the screen
    M5.Lcd.setTextColor(t.fg, t.bg);
    std::string currentWord = sampleWords[currentWordIndex];
    
    // Calculate string width for centering on 240x135 landscape display
    int textWidth = currentWord.length() * 12; // Approximate width for font size 2
    int centerX = max(5, (240 - textWidth) / 2);
    int centerY = 60;
    
    M5.Lcd.setCursor(centerX, centerY);
    M5.Lcd.print(currentWord.c_str());
}