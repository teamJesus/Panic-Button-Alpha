// Wiring test sketch for:
// - 5 buttons (active LOW, use INPUT_PULLUP)
// - piezo buzzer
// - 1602 LCD with I2C backpack (4 wires: VCC/GND/SDA/SCL)
// - LoRa RFM9x (optional; enable by defining USE_LORA below)

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

// ============ PIN DEFINITIONS ============
// Button pins (active LOW with INPUT_PULLUP)
#define PIN_BUTTON_1 8
#define PIN_BUTTON_2 4
#define PIN_BUTTON_3 5
#define PIN_BUTTON_4 6
#define PIN_BUTTON_5 7

#define QUIET_DEBUG

// Buzzer pin (PWM-capable)
#define PIN_BUZZER 10

// I2C LCD address and dimensions
#define I2C_LCD_ADDR 0x27
#define LCD_COLS 16
#define LCD_ROWS 2

// ============ LORA PINS & CONFIG ============
// Uncomment to enable LoRa functionality (requires LoRa lib in platformio.ini)
#define USE_LORA

#ifdef USE_LORA
#include <SPI.h>
#include <LoRa.h>
// Default LoRa pins for many Arduino Uno/Nano RFM9x modules
// Adjust these to match your wiring if different.
#define PIN_LORA_SS 15 // CS
// Map RST to D9 to avoid conflict with the interrupt pin (D2)
#define PIN_LORA_RST 2
// Interrupt pin for LoRa RX events (DIO0 / G0)
#define PIN_LORA_DIO0 3
// LoRa SPI pin mapping (you provided these pins):
// Note: On an Arduino Nano (ATmega168) the hardware SPI pins are fixed to D11/D12/D13.
// If you're using a different board where SPI pins are remappable, these defines let you
// document your wiring. If you're on a Nano, wire MOSI->D11, MISO->D12, SCK->D13 instead.
// Module G0 pin (labelled G0 on the module) is the same as DIO0 and should be wired to D2
#endif

// ============ OPERATIONAL CONSTANTS ============
const unsigned long DEBOUNCE_MS = 10;
const unsigned long BAUD_RATE = 9600;
const unsigned long LORA_FREQ = 915E6;
const unsigned int BEEP_DURATION_MS = 80;
const unsigned int BEEP_FREQ_HZ = 4000;
// How often the transmitter re-sends the 'pressed' packet while a button is held (ms)
#define HOLD_SEND_INTERVAL_MS 200
// How long the receiver will keep showing a received press without updates before clearing (ms)
#define RECEIVE_TIMEOUT_MS 1000
// Naming constants
#define NAME_MAX_LEN 12
#define NAME_EEPROM_ADDR 0
#define LONG_PRESS_MS 1000

LiquidCrystal_I2C lcd(I2C_LCD_ADDR, LCD_COLS, LCD_ROWS);

// State
// Debounce / button state tracking
byte stableState[5] = {HIGH, HIGH, HIGH, HIGH, HIGH};
byte lastReading[5] = {HIGH, HIGH, HIGH, HIGH, HIGH};
unsigned long lastDebounce[5] = {0, 0, 0, 0, 0};
bool loRaOk = false;
// Non-blocking buzzer state
unsigned long buzzerEndTime = 0;
byte buzzerFreqActive = 0;
// Per-button last time we re-sent a hold packet (transmitter)
unsigned long lastHoldSend[5] = {0, 0, 0, 0, 0};
// Per-remote-button last received timestamp (receiver) for buttons 1..4
unsigned long lastReceivedAt[4] = {0, 0, 0, 0};

// Naming mode state
bool namingMode = false;
char deviceName[NAME_MAX_LEN + 1];
byte namePos = 0;
// Button long-press tracking
unsigned long pressStart[5] = {0, 0, 0, 0, 0};
byte longPressHandled[5] = {0, 0, 0, 0, 0};

// Helper: update name display on LCD while in naming mode
void updateNameDisplay()
{
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Edit:");
    lcd.setCursor(0, 1);
    for (int i = 0; i < NAME_MAX_LEN && i < LCD_COLS; ++i)
    {
        char c = deviceName[i];
        if (c < 32)
            c = ' ';
        lcd.print(c);
    }
}

// Helper: save current deviceName to EEPROM
void saveNameToEEPROM()
{
    for (int i = 0; i < NAME_MAX_LEN; ++i)
    {
        EEPROM.update(NAME_EEPROM_ADDR + i, deviceName[i]);
    }
}

// Start a non-blocking beep: returns immediately and stops automatically later
void beep(unsigned int ms = BEEP_DURATION_MS, unsigned int freq = BEEP_FREQ_HZ)
{
    #ifdef QUIET_DEBUG
        return; 
    #endif
    tone(PIN_BUZZER, freq);
    buzzerEndTime = millis() + ms;
    buzzerFreqActive = freq;
}

void setup()
{
    // Delay to allow USB/serial monitor to connect
    delay(2000);
    
    Serial.begin(BAUD_RATE);
    delay(500); // wait for serial monitor to be ready
    
    // Load device name from EEPROM (fixed length NAME_MAX_LEN)
    for (int i = 0; i < NAME_MAX_LEN; ++i)
    {
        char c = EEPROM.read(NAME_EEPROM_ADDR + i);
        if (c == 0xFF || c == 0)
            c = 'a';
        deviceName[i] = c;
    }
    deviceName[NAME_MAX_LEN] = '\0';
    
    // Buttons
    pinMode(PIN_BUTTON_1, INPUT_PULLUP);
    pinMode(PIN_BUTTON_2, INPUT_PULLUP);
    pinMode(PIN_BUTTON_3, INPUT_PULLUP);
    pinMode(PIN_BUTTON_4, INPUT_PULLUP);
    pinMode(PIN_BUTTON_5, INPUT_PULLUP);

    // Buzzer
    pinMode(PIN_BUZZER, OUTPUT);
    noTone(PIN_BUZZER);

    // LCD init
    Wire.begin();
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Wiring Test");
    lcd.setCursor(0, 1);
    lcd.print("Buttons: -----");

#ifdef USE_LORA
    // LoRa init
    lcd.setCursor(0, 1);
    lcd.print("LoRa init...");
    delay(500);
    // Reset LoRa module (if RST pin wired)
    pinMode(PIN_LORA_RST, OUTPUT);
    digitalWrite(PIN_LORA_RST, HIGH);
    delay(50);
    digitalWrite(PIN_LORA_RST, LOW);
    delay(50);
    digitalWrite(PIN_LORA_RST, HIGH);
    delay(50);

    SPI.begin();
    // Set chip select pin
    pinMode(PIN_LORA_SS, OUTPUT);
    digitalWrite(PIN_LORA_SS, HIGH);

    // Tell the LoRa library which pins we wired (SS, RST, DIO0)
    LoRa.setPins(PIN_LORA_SS, PIN_LORA_RST, PIN_LORA_DIO0);

    // Debug: print pin mapping
    // Initialize LoRa module
    if (!LoRa.begin(LORA_FREQ))
    {
        loRaOk = false;
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("LoRa: FAILED");
    }
    else
    {
        loRaOk = true;
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("LoRa: OK");
        lcd.setCursor(0, 1);
        lcd.print("Buttons: -----");
    }
#else
    lcd.setCursor(0, 0);
    lcd.print("LoRa: disabled ");
#endif

    delay(500);
}

void loop()
{
    // Read buttons and update LCD and buzzer when presses detected
    const int buttonPins[5] = {PIN_BUTTON_1, PIN_BUTTON_2, PIN_BUTTON_3, PIN_BUTTON_4, PIN_BUTTON_5};
    for (int i = 0; i < 5; ++i)
    {
        int reading = digitalRead(buttonPins[i]);

        // If the reading changed from last time, reset the debounce timer
        if (reading != lastReading[i])
        {
            lastDebounce[i] = millis();
            lastReading[i] = reading;
        }

        // If the reading has been stable for longer than the debounce interval,
        // and it's different from the last stable state, we have a confirmed change.
        if ((millis() - lastDebounce[i]) > DEBOUNCE_MS && reading != stableState[i])
        {
            stableState[i] = reading;
            // record press start for long-press detection
            if (stableState[i] == LOW)
            {
                pressStart[i] = millis();
                longPressHandled[i] = false;
            }
            else
            {
                pressStart[i] = 0;
                longPressHandled[i] = false;
            }
            // Confirmed state change
            if (stableState[i] == LOW)
            { // pressed (active LOW)
                Serial.print("Button ");
                Serial.print(i + 1);
                Serial.println(" pressed");
                // If in naming mode, map buttons to name editing
                if (namingMode)
                {
                    if (i == 0)
                    { // button1 decrease char
                        char &ch = deviceName[namePos];
                        if (ch <= 32)
                            ch = 126;
                        else
                            ch--;
                        updateNameDisplay();
                        beep(40, 2000);
                    }
                    else if (i == 1)
                    { // button2 increase char
                        char &ch = deviceName[namePos];
                        if (ch >= 126)
                            ch = 32;
                        else
                            ch++;
                        updateNameDisplay();
                        beep(40, 2000);
                    }
                    else if (i == 2)
                    { // button3 move next
                        namePos++;
                        if (namePos >= NAME_MAX_LEN)
                            namePos = 0;
                        updateNameDisplay();
                        beep(40, 2000);
                    }
                }
                else
                {
                    lcd.setCursor(i, 1);
                    lcd.print((char)('1' + i));
                    // small beep
                    beep(BEEP_DURATION_MS, BEEP_FREQ_HZ);
// send press only if not in naming mode
#ifdef USE_LORA
                    if (loRaOk && i == 3)
                    {
                        // Only button 4 (index 3) transmits. Build C-string without using Arduino String.
                        char out[NAME_MAX_LEN + 4]; // 'P' + digit + optional '|' + name + NUL
                        int pos = 0;
                        out[pos++] = 'P';
                        out[pos++] = (char)('1' + i);
                        out[pos] = '\0';

                        // Trim trailing spaces from deviceName
                        const char *namePtr = deviceName;
                        size_t len = strlen(namePtr);
                        while (len > 0 && namePtr[len - 1] == ' ')
                            --len;

                        if (len > 0)
                        {
                            out[pos++] = '|';
                            memcpy(out + pos, namePtr, len);
                            pos += len;
                        }
                        out[pos] = '\0';

                        LoRa.beginPacket();
                        LoRa.print(out);
                        LoRa.endPacket();
                        lastHoldSend[i] = millis();
                    }
#endif
                }
            }
            else
            { // released
                // If in naming mode, do not send release; handle long-press saving elsewhere
                if (!namingMode)
                {
                    lcd.setCursor(i, 1);
                    lcd.print('-');
#ifdef USE_LORA
                    if (loRaOk && i == 3)
                    {
                        char out[4] = {'R', (char)('1' + i), '\0'};
                        LoRa.beginPacket();
                        LoRa.print(out);
                        LoRa.endPacket();
                        lastHoldSend[i] = 0;
                    }
#endif
                }
            }
        }
    }

    // Handle long-press actions (enter/exit naming mode when button3 is held)
    for (int i = 0; i < 5; ++i)
    {
        if (stableState[i] == LOW && pressStart[i] != 0 && !longPressHandled[i])
        {
            if ((millis() - pressStart[i]) >= LONG_PRESS_MS)
            {
                // long-press detected
                longPressHandled[i] = true;
                if (i == 2)
                { // button3 long-press
                    if (!namingMode)
                    {
                        // enter naming mode
                        namingMode = true;
                        namePos = 0;
                        updateNameDisplay();
                    }
                    else
                    {
                        // save name and exit naming mode
                        saveNameToEEPROM();
                        namingMode = false;
                        lcd.clear();
                        lcd.setCursor(0, 0);
                        lcd.print("Name saved");
                        delay(600);
                        lcd.clear();
                        lcd.setCursor(0, 0);
                        lcd.print("Buttons: -----");
                    }
                }
            }
        }
    }

    // Check for incoming LoRa packets (non-blocking)
#ifdef USE_LORA
    if (loRaOk)
    {
        int packetSize = LoRa.parsePacket();
        if (packetSize)
        {
            // Read entire packet into a char buffer
            char payload[64] = {0}; // fixed-size buffer for received packet
            int payloadLen = 0;
            while (LoRa.available() && payloadLen < (int)sizeof(payload) - 1)
            {
                payload[payloadLen++] = (char)LoRa.read();
            }
            payload[payloadLen] = '\0'; // null-terminate
            if (payloadLen > 0)
            {
                // Expecting 'P#' for press or 'R#' for release, or legacy single-digit '#'
                unsigned long now = millis();
                if (payloadLen == 1)
                {
                    char c = payload[0];
                    int idx = (int)(c - '1');
                    if (idx >= 0 && idx <= 3)
                    {
                        lcd.setCursor(idx, 1);
                        lcd.print(c);
                        beep(BEEP_DURATION_MS, BEEP_FREQ_HZ);
                        lastReceivedAt[idx] = now;
                    }
                }
                else if (payloadLen >= 2)
                {
                    char cmd = payload[0];
                    char val = payload[1];
                    int idx = (int)(val - '1');
                    if (idx >= 0 && idx <= 3)
                    {
                        if (cmd == 'P')
                        {
                            lcd.setCursor(idx, 1);
                            lcd.print(val);
                            // If there's a name attached (format: P4|name), parse and display it on row 0
                            const char *pipePos = strchr(payload, '|');
                            if (pipePos != NULL && pipePos + 1 < payload + payloadLen)
                            {
                                const char *nameStart = pipePos + 1;
                                int nameLen = payloadLen - (nameStart - payload);
                                // Clear row 0 first
                                lcd.setCursor(0, 0);
                                for (int p = 0; p < LCD_COLS; ++p)
                                    lcd.print(' ');
                                // Print name (truncate to LCD_COLS if necessary)
                                lcd.setCursor(0, 0);
                                for (int p = 0; p < nameLen && p < LCD_COLS; ++p)
                                    lcd.print(nameStart[p]);
                            }
                            beep(BEEP_DURATION_MS, BEEP_FREQ_HZ);
                            lastReceivedAt[idx] = now;
                        }
                        else if (cmd == 'R')
                        {
                            // clear the digit (print '-') and clear name row if it was for button4
                            lcd.setCursor(idx, 1);
                            lcd.print('-');
                            lastReceivedAt[idx] = 0;
                            if (idx == 3)
                            {
                                // clear name row
                                lcd.setCursor(0, 0);
                                for (int p = 0; p < LCD_COLS; ++p)
                                    lcd.print(' ');
                            }
                        }
                    }
                }
            }
        }
    }
#endif

  
    // Resend 'P#' periodically while a local button is held (improves reliability)
#ifdef USE_LORA
    if (loRaOk)
    {
        // Stop buzzer if time expired (non-blocking beep)
        if (buzzerEndTime != 0 && millis() >= buzzerEndTime)
        {
            noTone(PIN_BUZZER);
            buzzerEndTime = 0;
            buzzerFreqActive = 0;
        }
        unsigned long now = millis();
        // Only resend for button 4 (index 3)
        if (stableState[3] == LOW)
        {

            if (lastHoldSend[3] == 0 || (now - lastHoldSend[3]) >= HOLD_SEND_INTERVAL_MS)
            {
                // Build C-string packet for hold-resend
                char out[NAME_MAX_LEN + 4] = {0};
                int pos = 0;
                out[pos++] = 'P';
                out[pos++] = '4';

                // Trim trailing spaces from deviceName
                const char *namePtr = deviceName;
                size_t len = strlen(namePtr);
                while (len > 0 && namePtr[len - 1] == ' ')
                    --len;

                if (len > 0)
                {
                    out[pos++] = '|';
                    memcpy(out + pos, namePtr, len);
                    pos += len;
                }
                out[pos] = '\0';

                LoRa.beginPacket();
                LoRa.print(out);
                LoRa.endPacket();
                lastHoldSend[3] = now;
            }
        }

        // Clear remote digits if timed out (no heartbeat/press updates)
        for (int idx = 0; idx <= 3; ++idx)
        {
            if (lastReceivedAt[idx] != 0 && (millis() - lastReceivedAt[idx]) > RECEIVE_TIMEOUT_MS)
            {
                lcd.setCursor(idx, 1);
                lcd.print('-');
                lastReceivedAt[idx] = 0;
                // If this was button 4, also clear the name row
                if (idx == 3)
                {
                    lcd.setCursor(0, 0);
                    for (int p = 0; p < 16; ++p)
                        lcd.print(' ');
                }
            }
        }
    }
#endif
}
