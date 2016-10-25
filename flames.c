#include <stdio.h>
#include <stdlib.h>
#include <propeller.h>
#include "fds.h"
#include "encoder.h"
#include "ws2812.h"
#include "eeprom.h"

#define RGB_LED_PIN         0

#define LCD_RX_PIN          8   // not really needed but fds requires an rx pin
#define LCD_TX_PIN          9

#define ENCODER_A_PIN       10
#define ENCODER_B_PIN       11

#define RED_LED_PIN		    12
#define GREEN_LED_PIN	    13
#define BUTTON_PIN		    14
#define BLUE_LED_PIN	    15

#if 1
#define RGB_ROW_WIDTH       144
#define RGB_PIXEL_HEIGHT    1
#else
#define RGB_ROW_WIDTH       28
#define RGB_PIXEL_HEIGHT    5
#endif

#define RGB_LED_COUNT       (RGB_ROW_WIDTH * RGB_PIXEL_HEIGHT)

enum {
    LCD_CLEAR               = 0x0c,
    LCD_BACKLIGHT_ON        = 0x11,
    LCD_BACKLIGHT_OFF       = 0x12,
    LCD_CURSOR_OFF_NO_BLINK = 0x16,
    LCD_CURSOR_OFF_BLINK    = 0x17,
    LCD_MOVE_CURSOR         = 0x80
};

#define STACK_SIZE 16

/*
 * This is the structure which we'll pass to the C cog.
 * It contains a small stack for working area, and the
 * mailbox which we use to communicate. See encoder.h
 * for the definition.
 */
struct par {
  unsigned stack[STACK_SIZE];
  struct encoder_mailbox m;
};

struct par encoder;

#define usefw(fw)           extern unsigned char _load_start_ ## fw ## _cog[];
#define LOAD_START(fw)      _load_start_ ## fw ## _cog

usefw(encoder_fw);

typedef struct {
    uint32_t *buf;
    int preset;
    int rowWidth;
    int pixelHeight;
    int ticksPerMS;

    volatile int pixelWidth;
    volatile int red;
    volatile int green;
    volatile int blue;
    volatile int depth;
    volatile int rate;

    int pixelWidthSetting;
    int levelSetting;
    int redSetting;
    int greenSetting;
    int blueSetting;
    int depthSetting;
    int rateSetting;
} FLAME_STATE;

FLAME_STATE flameState;

long stack[32 + EXTRA_STACK_LONGS];

FdSerial_t lcd;

ws2812_t ledState;
uint32_t ledValues[RGB_LED_COUNT];

typedef struct {
    const char *label;
    const char *format;
    volatile int *pValue;
    int minValue;
    int maxValue;
    int valueRow;
    int valueCol;
} ADJUSTER;

ADJUSTER adjusters[] = {
{   "L",        "%02d", &flameState.levelSetting,       0,  99,     0,  1   },
{   "R",        "%02d", &flameState.redSetting,         0,  99,     0,  5   },
{   "G",        "%02d", &flameState.greenSetting,       0,  99,     0,  9   },
{   "B",        "%02d", &flameState.blueSetting,        0,  99,     0,  13  },
{   "#",        "%01d", &flameState.preset,             1,  1,      1,  1   },
{   "P",        "%02d", &flameState.pixelWidthSetting,  1,  10,     1,  5   },
{   "D",        "%02d", &flameState.depthSetting,       0,  99,     1,  9   },
{   "S",        "%02d", &flameState.rateSetting,        0,  99,     1,  13  },
{   NULL,       NULL,   NULL,                           0,  0,      0,  0   },
};

#define EEPROM_BASE     0x8000
#define EEPROM_MAGIC    "FIRE"
#define EEPROM_VERSION  2

typedef struct {
    char magic[4];
    int version;
    int pixelWidthSetting;
    int levelSetting;
    int redSetting;
    int greenSetting;
    int blueSetting;
    int depthSetting;
    int rateSetting;
} EEPROM_DATA;

EEPROM_DATA eepromData;

static void do_flame(void *params);

static void updateSettings(void);
static void loadSettings(void);
static void saveSettings(void);

static void selectAdjuster(ADJUSTER *adjuster);
static void displayAdjusterValue(ADJUSTER *adjuster);

static void lcdMoveCursor(int row, int col);
static void lcdPutStr(int row, int col, const char *buf);

int main(void)
{
    uint32_t buttonMask = 1 << BUTTON_PIN;
    ADJUSTER *adjuster;
    int ret;

    ret = FdSerial_start(&lcd, LCD_RX_PIN, LCD_TX_PIN, 0, 19200);
    FdSerial_tx(&lcd, LCD_CLEAR);
    FdSerial_tx(&lcd, LCD_CURSOR_OFF_BLINK);
    FdSerial_tx(&lcd, LCD_BACKLIGHT_ON);
    printf("FdSerial_start returned %d\n", ret);

    printf("Initializing encoder...\n");
    encoder.m.pin = ENCODER_A_PIN;
    encoder.m.minValue = 0;
    encoder.m.maxValue = 255;
    ret = cognew(LOAD_START(encoder_fw), &encoder.m);
    printf("cognew returned %d\n", ret);

    printf("Initializing LED strip...\n");
    ret = ws2812b_init(&ledState);
    printf("ws2812b_init returned %d\n", ret);

    eeprom_init();
        
    flameState.buf = ledValues;
    flameState.preset = 1;
    flameState.rowWidth = RGB_ROW_WIDTH;
    flameState.pixelHeight = RGB_PIXEL_HEIGHT;
    flameState.ticksPerMS = CLKFREQ / 1000;
    loadSettings();
    updateSettings();

    ret = cogstart(do_flame, &flameState, stack, sizeof(stack));
    printf("cogstart returned %d\n", ret);

    printf("Entering idle loop...\n");
    int lastButtonValue = 0;
    int lastValue = 0;
    
    for (adjuster = adjusters; adjuster->label; ++adjuster)
        displayAdjusterValue(adjuster);
    adjuster = adjusters;
    selectAdjuster(adjuster);

    for (;;) {

        if (INA & buttonMask) {
            if (!lastButtonValue) {
                lastButtonValue = 1;
                saveSettings();
                ++adjuster;
                if (!adjuster->label)
                    adjuster = adjusters;
                selectAdjuster(adjuster);
            }
        }
        else {
            lastButtonValue = 0;
        }

        if (encoder.m.value != lastValue) {
            lastValue = encoder.m.value;
            *adjuster->pValue = lastValue;
            displayAdjusterValue(adjuster);
            lcdMoveCursor(adjuster->valueRow, adjuster->valueCol - 1);
            updateSettings();
        }
    }

    return 0;
}

static void updateSettings(void)
{
    flameState.pixelWidth = flameState.pixelWidthSetting;
    flameState.red = (flameState.levelSetting * flameState.redSetting * 255) / (99 * 99);
    flameState.green = (flameState.levelSetting * flameState.greenSetting * 255) / (99 * 99);
    flameState.blue = (flameState.levelSetting * flameState.blueSetting * 255) / (99 * 99);
    flameState.depth = (flameState.depthSetting * 255) / 99;
    flameState.rate = ((99 - flameState.rateSetting) * 990) / 99;
}

static void loadSettings(void)
{
    int ret = eeprom_read(EEPROM_BASE, (uint8_t *)&eepromData, sizeof(EEPROM_DATA));
    if (ret != 0 || strncmp(eepromData.magic, EEPROM_MAGIC, sizeof(eepromData.magic)) != 0 || eepromData.version != EEPROM_VERSION) {
        strncpy(eepromData.magic, EEPROM_MAGIC, sizeof(eepromData.magic));
        eepromData.version = EEPROM_VERSION;
        eepromData.pixelWidthSetting = 2;
        eepromData.levelSetting = 50;
        eepromData.redSetting = 88; // 226
        eepromData.greenSetting = 47; // 121
        eepromData.blueSetting = 14; // 35
        eepromData.depthSetting = 21; // 55
        eepromData.rateSetting = 99;
    }
    flameState.pixelWidthSetting = eepromData.pixelWidthSetting;
    flameState.levelSetting = eepromData.levelSetting;
    flameState.redSetting = eepromData.redSetting;
    flameState.greenSetting = eepromData.greenSetting;
    flameState.blueSetting = eepromData.blueSetting;
    flameState.depthSetting = eepromData.depthSetting;
    flameState.rateSetting = eepromData.rateSetting;
}

static void saveSettings(void)
{
    if (flameState.pixelWidthSetting != eepromData.pixelWidthSetting
    ||  flameState.levelSetting != eepromData.levelSetting
    ||  flameState.redSetting != eepromData.redSetting
    ||  flameState.greenSetting != eepromData.greenSetting
    ||  flameState.blueSetting != eepromData.blueSetting
    ||  flameState.depthSetting != eepromData.depthSetting
    ||  flameState.rateSetting != eepromData.rateSetting) {
        EEPROM_DATA newData = eepromData;
        newData.pixelWidthSetting = flameState.pixelWidthSetting;
        newData.levelSetting = flameState.levelSetting;
        newData.redSetting = flameState.redSetting;
        newData.greenSetting = flameState.greenSetting;
        newData.blueSetting = flameState.blueSetting;
        newData.depthSetting = flameState.depthSetting;
        newData.rateSetting = flameState.rateSetting;
        if (eeprom_write(EEPROM_BASE, (uint8_t *)&newData, sizeof(EEPROM_DATA)) == 0)
            eepromData = newData;
    }
}

static void displayAdjusterValue(ADJUSTER *adjuster)
{
    char buf[10];
    lcdPutStr(adjuster->valueRow, adjuster->valueCol - 1, adjuster->label);
    sprintf(buf, adjuster->format, *adjuster->pValue);
    lcdPutStr(adjuster->valueRow, adjuster->valueCol, buf);
}

static void selectAdjuster(ADJUSTER *adjuster)
{
    lcdMoveCursor(adjuster->valueRow, adjuster->valueCol - 1);
    encoder.m.value = *adjuster->pValue;
    encoder.m.minValue = adjuster->minValue;
    encoder.m.maxValue = adjuster->maxValue;
    encoder.m.wrap = 0;
}

static void do_flame(void *params)
{
    FLAME_STATE *state = params;
    for (;;) {
        int x, px, py;
        int i = 0;
        for (x = 0; x < state->rowWidth; x += state->pixelWidth) {
            int flicker = rand() % state->depth;
            int red = state->red - flicker;
            int green = state->green - flicker;
            int blue = state->blue - flicker;
            if (red < 0) red = 0;
            if (green < 0) green = 0;
            if (blue < 0) blue = 0;
            int color = (red << 16) | (green << 8) | blue;
            int j = i;
            for (py = 0; py < state->pixelHeight; ++py) {
                for (px = 0; px < state->pixelWidth; ++px) {
                    if (j + px < state->rowWidth)
                        state->buf[j + px] = color;
                }
                j += state->rowWidth;
            }
            i += state->pixelWidth;
        }
        ws2812_update(&ledState, RGB_LED_PIN, state->buf, RGB_LED_COUNT);
        waitcnt(CNT + (10 + rand() % state->rate) * state->ticksPerMS);
    }
}

static void lcdMoveCursor(int row, int col)
{
    FdSerial_tx(&lcd, LCD_MOVE_CURSOR + row * 20 + col);
}

static void lcdPutStr(int row, int col, const char *buf)
{
    lcdMoveCursor(row, col);
    while (*buf)
        FdSerial_tx(&lcd, *buf++);
}

