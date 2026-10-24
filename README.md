# book-reader-esp32

RSVP speed reader for the M5StickC Plus2.

## Adding books (.txt files)

The M5StickC Plus2 has no SD card slot, so books are stored in its internal flash using LittleFS.

1. Put your `.txt` files in `main/data/` (next to `main.ino`). `example.txt` is included as a sample.
2. In Arduino IDE 2, install the **arduino-littlefs-upload** plugin
   (https://github.com/earlephilhower/arduino-littlefs-upload): drop the `.vsix` into
   `C:\Users\<you>\.arduinoIDE\plugins\` and restart the IDE.
3. Make sure the selected partition scheme has a filesystem partition (Tools → Partition Scheme,
   e.g. "Default" — it includes a SPIFFS/LittleFS area of ~1.5 MB).
4. Close the Serial Monitor, then press `Ctrl+Shift+P` → **Upload LittleFS to Pico/ESP8266/ESP32**.
5. Upload the sketch as usual. The books appear in **Books List**.

Notes:
- Only files in the root of `data/` are listed (up to 30).
- Plain ASCII text displays best: the default font doesn't render accented characters.
- Books are streamed from flash 64 words at a time, so large files don't need to fit in RAM.
- Re-opening the book you were reading resumes where you stopped (until power-off).

## Books screen controls

- **BtnA**: next book
- **BtnB**: open the selected book
- **PWR**: back to the menu
