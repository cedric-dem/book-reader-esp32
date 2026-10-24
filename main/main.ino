#include <M5Unified.h>
#include <LittleFS.h>
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

// --- TEXT FILE BOOKS (LittleFS) ---
// .txt files placed in the sketch's "data" folder are uploaded to the internal
// flash with the LittleFS upload tool and show up in the Books list.
// Books are not loaded into RAM: an index pass stores the byte offset of every
// CHUNK_WORDS-th word, and words are then read from the file one chunk at a time.
const int CHUNK_WORDS = 64;
const size_t MAX_WORD_LEN = 48;     // Longer "words" are truncated
const int MAX_BOOKS = 30;

bool fsReady = false;
std::vector<std::string> bookFiles; // Full paths of .txt files found
int bookSelection = 0;              // Selected entry in the Books list
int openedBookIdx = -2;             // Entry currently loaded (-2 = none)
std::string listMessage = "";       // Error/status line on the Books screen

bool readingFromFile = false;       // false = built-in sample book
File bookFile;
std::vector<uint32_t> chunkOffsets; // Byte offset of the first word of each chunk
int fileWordCount = 0;
std::vector<std::string> chunkWords;
int loadedChunk = -1;

static inline bool isSeparator(char c) {
    return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == '\v';
}

static bool endsWithTxt(const std::string& s) {
    if (s.size() < 4) return false;
    std::string ext = s.substr(s.size() - 4);
    for (auto& ch : ext) ch = tolower(ch);
    return ext == ".txt";
}

// Find every .txt file in the root of the filesystem
void scanBooks() {
    bookFiles.clear();
    if (!fsReady) return;
    File root = LittleFS.open("/");
    if (!root || !root.isDirectory()) return;
    File f = root.openNextFile();
    while (f && (int)bookFiles.size() < MAX_BOOKS) {
        if (!f.isDirectory()) {
            std::string path = f.path();
            if (endsWithTxt(path)) bookFiles.push_back(path);
        }
        f = root.openNextFile();
    }
}

// Opens a .txt file and builds the chunk index. Returns false if the file
// can't be opened or contains no words.
bool openBook(const std::string& path) {
    if (bookFile) bookFile.close();
    chunkOffsets.clear();
    chunkWords.clear();
    fileWordCount = 0;
    loadedChunk = -1;

    bookFile = LittleFS.open(path.c_str(), "r");
    if (!bookFile) return false;

    uint8_t buf[512];
    uint32_t pos = 0;
    bool inWord = false;
    int n;
    while ((n = bookFile.read(buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++, pos++) {
            if (isSeparator(buf[i])) {
                inWord = false;
            } else if (!inWord) {
                if (fileWordCount % CHUNK_WORDS == 0) chunkOffsets.push_back(pos);
                fileWordCount++;
                inWord = true;
            }
        }
    }
    if (fileWordCount == 0) {
        bookFile.close();
        return false;
    }
    return true;
}

// Reads the words of chunk `c` from the open file into chunkWords
void loadChunk(int c) {
    chunkWords.clear();
    loadedChunk = c;
    if (c < 0 || c >= (int)chunkOffsets.size()) return;
    bookFile.seek(chunkOffsets[c]);

    std::string w;
    uint8_t buf[256];
    int n;
    while ((int)chunkWords.size() < CHUNK_WORDS && (n = bookFile.read(buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n && (int)chunkWords.size() < CHUNK_WORDS; i++) {
            char ch = (char)buf[i];
            if (isSeparator(ch)) {
                if (!w.empty()) { chunkWords.push_back(w); w.clear(); }
            } else if (w.size() < MAX_WORD_LEN) {
                w += ch;
            }
        }
    }
    if (!w.empty() && (int)chunkWords.size() < CHUNK_WORDS) chunkWords.push_back(w);

    // Strip a UTF-8 byte order mark from the very first word
    if (c == 0 && !chunkWords.empty() && chunkWords[0].compare(0, 3, "\xEF\xBB\xBF") == 0) {
        chunkWords[0].erase(0, 3);
    }
}

int wordCount() {
    return readingFromFile ? fileWordCount : (int)sampleWords.size();
}

std::string getWord(int i) {
    if (!readingFromFile) {
        return (i >= 0 && i < (int)sampleWords.size()) ? sampleWords[i] : "";
    }
    if (i < 0 || i >= fileWordCount) return "";
    int c = i / CHUNK_WORDS;
    if (c != loadedChunk) loadChunk(c);
    int idx = i % CHUNK_WORDS;
    return idx < (int)chunkWords.size() ? chunkWords[idx] : "";
}

// Display name: "/my_book.txt" -> "my_book"
std::string bookTitle(const std::string& path) {
    std::string name = path;
    size_t slash = name.find_last_of('/');
    if (slash != std::string::npos) name = name.substr(slash + 1);
    if (endsWithTxt(name)) name = name.substr(0, name.size() - 4);
    return name;
}

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
void openSelectedBook();
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

    // Internal flash filesystem holding the .txt books (formatted on first use)
    fsReady = LittleFS.begin(true);
    scanBooks();

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
            // Next book (the built-in sample is the last entry)
            bookSelection = (bookSelection + 1) % ((int)bookFiles.size() + 1);
            listMessage = "";
        } else if (currentState == STATE_OPTIONS) {
            currentThemeIndex = (currentThemeIndex + 1) % 8;
        } else if (currentState == STATE_READING) {
            // Slower speed (-10%)
            if (speedMultiplierIdx > 0) speedMultiplierIdx--;
        }
    }

    if (M5.BtnB.wasClicked()) {
        if (currentState == STATE_MENU) {
            if (menuSelection == 0) {
                scanBooks();
                if (bookSelection > (int)bookFiles.size()) bookSelection = 0;
                listMessage = "";
                currentState = STATE_BOOKS_LIST;
            }
            else if (menuSelection == 1) currentState = STATE_OPTIONS;
            else if (menuSelection == 2) {
                // Toggle Language
                currentLang = (Lang)((currentLang + 1) % 3);
            }
        } else if (currentState == STATE_BOOKS_LIST) {
            openSelectedBook();
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
        if (currentState == STATE_READING) currentWordIndex = min(wordCount() - 1, currentWordIndex + 10);
    }
}

// Opens the book selected in the Books list and starts reading.
// Re-opening the book you were already reading resumes where you left off.
void openSelectedBook() {
    bool isSample = bookSelection >= (int)bookFiles.size();

    if (bookSelection != openedBookIdx) {
        if (isSample) {
            if (bookFile) bookFile.close();
            readingFromFile = false;
        } else {
            // Indexing a large file takes a moment, so say so
            Theme t = themes[currentThemeIndex];
            canvas.fillScreen(t.bg);
            canvas.setTextColor(t.fg, t.bg);
            canvas.setCursor(10, 55);
            canvas.println("Loading...");
            canvas.pushSprite(0, 0);

            if (!openBook(bookFiles[bookSelection])) {
                readingFromFile = false;
                openedBookIdx = -2;
                listMessage = "Can't read file / empty";
                return;
            }
            readingFromFile = true;
        }
        openedBookIdx = bookSelection;
        currentWordIndex = 0;
    }

    listMessage = "";
    currentState = STATE_READING;
    isPlaying = true;
    lastWordTime = millis();
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

    int total = (int)bookFiles.size() + 1; // + built-in sample

    canvas.setCursor(10, 8);
    canvas.printf("Books: %d", total);

    // Scrolling window of up to 4 entries around the selection
    const int visible = 4;
    int first = max(0, min(bookSelection - visible + 1, total - visible));
    first = max(0, min(first, bookSelection));
    for (int i = first; i < min(total, first + visible); i++) {
        std::string name = (i < (int)bookFiles.size()) ? bookTitle(bookFiles[i]) : "Sample (built-in)";
        if (name.size() > 17) name = name.substr(0, 16) + "~";

        canvas.setCursor(10, 32 + (i - first) * 20);
        if (i == bookSelection) {
            canvas.setTextColor(t.accent, t.bg);
            canvas.print("> ");
        } else {
            canvas.setTextColor(t.fg, t.bg);
            canvas.print("  ");
        }
        canvas.print(name.c_str());
    }

    canvas.setTextSize(1);
    canvas.setTextColor(t.fg, t.bg);
    if (!fsReady) {
        canvas.setCursor(10, 112);
        canvas.print("Storage not available");
    } else if (!listMessage.empty()) {
        canvas.setCursor(10, 112);
        canvas.print(listMessage.c_str());
    }
    canvas.setCursor(10, 124);
    canvas.print("BtnA: next | BtnB: open | PWR: back");
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

// Battery level in percent (0-100). The reading is cached and only refreshed
// every 5 seconds so the bar doesn't flicker from ADC noise and we don't poll
// the hardware on every frame.
int readBatteryLevel() {
    static int cached = -1;
    static unsigned long lastRead = 0;
    if (cached < 0 || millis() - lastRead > 5000) {
        int lvl = M5.Power.getBatteryLevel();
        if (lvl >= 0) cached = constrain(lvl, 0, 100);
        else if (cached < 0) cached = 0;
        lastRead = millis();
    }
    return cached;
}

void drawReading() {
    Theme t = themes[currentThemeIndex];
    canvas.fillScreen(t.bg);

    int total = wordCount();
    if (total == 0) return;
    if (currentWordIndex >= total) currentWordIndex = total - 1;

    // Handle word pacing logic
    if (isPlaying && (millis() - lastWordTime > calculateWordDelay(getWord(currentWordIndex)))) {
        currentWordIndex++;
        if (currentWordIndex >= total) {
            currentWordIndex = 0;
            isPlaying = false; // Stop at end of book
        }
        lastWordTime = millis();
    }

    // Draw Status / Progress Bars (if Focus Mode is off)
    if (!focusMode) {
        // 5 progress bars: 3 above the word, 2 below it.
        // Bars 0-3 show book progress; the bottom bar (4) is the battery level.
        // Change the matching entry in barValues (0.0 to 1.0) to make a bar
        // track something else.
        const int barX = 10, barW = 220, barH = 6;
        const int barY[5] = {5, 17, 29,   // above the word (word sits at y=60..76)
                             112, 124};   // below the word
        float bookProgress = (float)currentWordIndex / total;
        float batteryLevel = readBatteryLevel() / 100.0f;
        float barValues[5] = {bookProgress, bookProgress, bookProgress,
                              bookProgress, batteryLevel};

        // The battery bar (4) is shortened to make room for its
        // "Battery : xx%" label on the left (small font, 6x8 px per char).
        const int battLabelW = 90;   // fits "Battery : 100%" (14 chars * 6 px) + gap

        for (int i = 0; i < 5; i++) {
            float v = constrain(barValues[i], 0.0f, 1.0f);
            // Battery bar turns red when it drops to 20% or below
            uint32_t color = (i == 4 && v <= 0.2f) ? TFT_RED : t.accent;
            int x = barX, w = barW;
            if (i == 4) {
                char label[20];
                snprintf(label, sizeof(label), "Battery : %d%%", (int)(v * 100.0f + 0.5f));
                canvas.setTextSize(1);
                canvas.setTextColor(color, t.bg);
                canvas.setCursor(barX, barY[i] - 1);   // 8 px text centred on the 6 px bar
                canvas.print(label);
                canvas.setTextSize(2);                 // restore the word font size
                x = barX + battLabelW;
                w = barW - battLabelW;
            }
            canvas.drawRect(x, barY[i], w, barH, color);
            canvas.fillRect(x, barY[i], (int)(v * w), barH, color);
        }
    }

    // Draw Current Word precisely in the center of the screen
    canvas.setTextColor(t.fg, t.bg);
    std::string currentWord = getWord(currentWordIndex);

    // Calculate string width for centering on 240x135 landscape display
    int textWidth = canvas.textWidth(currentWord.c_str());
    int centerX = max(5, (240 - textWidth) / 2);
    int centerY = 60;

    canvas.setCursor(centerX, centerY);
    canvas.print(currentWord.c_str());
}
