#include <stdio.h>
#include <stdlib.h>
#include <propeller.h>
#include "fds.h"
#include "encoder.h"
#include "ws2812.h"
#include "eeprom.h"

#define LCD_RX_PIN          8   // not really needed but fds requires an rx pin
#define LCD_TX_PIN          9

#define RGB_LED_PIN         0

#if 1
#define RGB_LED_COUNT       144
#define RGB_ROW_WIDTH       144
#define RGB_PIXEL_WIDTH     1
#define RGB_PIXEL_HEIGHT    1
#else
#define RGB_LED_COUNT       140
#define RGB_ROW_WIDTH       28
#define RGB_PIXEL_WIDTH     2
#define RGB_PIXEL_HEIGHT    5
#endif

#define RED_LED		        12
#define GREEN_LED	        13
#define BUTTON		        14
#define BLUE_LED	        15

enum {
    LCD_CLEAR               = 0x0c,
    LCD_BACKLIGHT_ON        = 0x11,
    LCD_BACKLIGHT_OFF       = 0x12,
    LCD_CURSOR_OFF_NO_BLINK = 0x16,
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
    int rowWidth;
    int pixelWidth;
    int pixelHeight;
    int ticksPerMS;

    volatile int red;
    volatile int green;
    volatile int blue;
    volatile int depth;
    volatile int rate;

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
    volatile int *pValue;
    int minValue;
    int maxValue;
    int valueRow;
    int valueCol;
} ADJUSTER;

ADJUSTER adjusters[] = {
{   "Level",    &flameState.levelSetting,   0,  99,     0,  1   },
{   "Red  ",    &flameState.redSetting,     0,  99,     0,  5   },
{   "Green",    &flameState.greenSetting,   0,  99,     0,  9   },
{   "Blue ",    &flameState.blueSetting,    0,  99,     0,  13  },
{   "Depth",    &flameState.depthSetting,   0,  99,     1,  9   },
{   "Rate ",    &flameState.rateSetting,    0,  99,     1,  13  },
{   NULL,       NULL,                       0,  0,      0,  0   },
};

#define EEPROM_BASE     0x8000
#define EEPROM_MAGIC    "FIRE"
#define EEPROM_VERSION  1

typedef struct {
    char magic[4];
    int version;
    int levelSetting;
    int redSetting;
    int greenSetting;
    int blueSetting;
    int depthSetting;
    int rateSetting;
} EEPROM_DATA;

EEPROM_DATA eepromData;

static void do_flame(void *params);

static void lcdMoveCursor(int row, int col);
static void lcdPutStr(int row, int col, const char *buf);
static void lcdUpdateValue(int row, int col, int value);

static void updateSettings(void);
static void loadSettings(void);
static void saveSettings(void);

static void selectMenu(ADJUSTER *adjuster);

static ADJUSTER *getAdjusterByIndex(int index);
static void displayAdjusterLabel(ADJUSTER *adjuster);
static void displayAdjusterValue(ADJUSTER *adjuster);
static void selectAdjuster(ADJUSTER *adjuster);

int main(void)
{
    uint32_t ledMask = (1 << RED_LED) | (1 << GREEN_LED) | (1 << BLUE_LED);
    uint32_t redMask = 1 << RED_LED;
    uint32_t blueMask = 1 << BLUE_LED;
    uint32_t buttonMask = 1 << BUTTON;
    ADJUSTER *adjuster;
    int ret;

    ret = FdSerial_start(&lcd, LCD_RX_PIN, LCD_TX_PIN, 0, 19200);
    FdSerial_tx(&lcd, LCD_CLEAR);
    FdSerial_tx(&lcd, LCD_CURSOR_OFF_NO_BLINK);
    FdSerial_tx(&lcd, LCD_BACKLIGHT_ON);
    printf("FdSerial_start returned %d\n", ret);
    lcdPutStr(0, 0, "L   R   G   B  ");
    lcdPutStr(1, 0, "      1 D   R  ");

    printf("Initializing encoder...\n");
    encoder.m.pin = 10;
    encoder.m.minValue = 0;
    encoder.m.maxValue = 255;
    ret = cognew(LOAD_START(encoder_fw), &encoder.m);
    printf("cognew returned %d\n", ret);

    printf("Initializing LED strip...\n");
    ret = ws2812b_init(&ledState);
    printf("ws2812b_init returned %d\n", ret);

    eeprom_init();
        
    flameState.buf = ledValues;
    flameState.rowWidth = RGB_ROW_WIDTH;
    flameState.pixelWidth = RGB_PIXEL_WIDTH;
    flameState.pixelHeight = RGB_PIXEL_HEIGHT;
    flameState.ticksPerMS = CLKFREQ / 1000;
    loadSettings();
    updateSettings();

    ret = cogstart(do_flame, &flameState, stack, sizeof(stack));
    printf("cogstart returned %d\n", ret);

    DIRA |= ledMask;
	OUTA &= ~blueMask;

    printf("Entering idle loop...\n");
    int lastButtonValue = 0;
    int lastValue = 0;
    int adjustingValue = 0;
    
    for (adjuster = adjusters; adjuster->label; ++adjuster)
        displayAdjusterValue(adjuster);
    adjuster = adjusters;
    displayAdjusterLabel(adjuster);
    selectMenu(adjuster);

    for (;;) {

        if (INA & buttonMask) {
            if (!lastButtonValue) {
                lastButtonValue = 1;
                adjustingValue = !adjustingValue;
                OUTA |= ledMask;
                if (adjustingValue) {
                    selectAdjuster(adjuster);
                    OUTA &= ~redMask;
                }
                else {
                    saveSettings();
                    selectMenu(adjuster);
                    OUTA &= ~blueMask;
                }
            }
        }
        else {
            lastButtonValue = 0;
        }

        if (encoder.m.value != lastValue) {
            lastValue = encoder.m.value;
            if (adjustingValue) {
                *adjuster->pValue = lastValue;
                displayAdjusterValue(adjuster);
                updateSettings();
            }
            else {
                if (!(adjuster = getAdjusterByIndex(lastValue)))
                    adjuster = adjusters;
                displayAdjusterLabel(adjuster);
            }
        }
    }

    return 0;
}

static void updateSettings(void)
{
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
        eepromData.levelSetting = 50;
        eepromData.redSetting = 88; // 226
        eepromData.greenSetting = 47; // 121
        eepromData.blueSetting = 14; // 35
        eepromData.depthSetting = 21; // 55
        eepromData.rateSetting = 99;
    }
    flameState.levelSetting = eepromData.levelSetting;
    flameState.redSetting = eepromData.redSetting;
    flameState.greenSetting = eepromData.greenSetting;
    flameState.blueSetting = eepromData.blueSetting;
    flameState.depthSetting = eepromData.depthSetting;
    flameState.rateSetting = eepromData.rateSetting;
}

static void saveSettings(void)
{
    if (flameState.levelSetting != eepromData.levelSetting
    ||  flameState.redSetting != eepromData.redSetting
    ||  flameState.greenSetting != eepromData.greenSetting
    ||  flameState.blueSetting != eepromData.blueSetting
    ||  flameState.depthSetting != eepromData.depthSetting
    ||  flameState.rateSetting != eepromData.rateSetting) {
        EEPROM_DATA newData = eepromData;
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

static void selectMenu(ADJUSTER *adjuster)
{
    encoder.m.minValue = 0;
    encoder.m.maxValue = sizeof(adjusters) / sizeof(ADJUSTER) - 2;
    encoder.m.value = adjuster - adjusters;
    encoder.m.wrap = 1;
}

static ADJUSTER *getAdjusterByIndex(int index)
{
    if (index < 0 || index >= sizeof(adjusters) / sizeof(ADJUSTER) - 1)
        return NULL;
    return &adjusters[index];
}

static void displayAdjusterLabel(ADJUSTER *adjuster)
{
    lcdPutStr(1, 0, adjuster->label);
}

static void displayAdjusterValue(ADJUSTER *adjuster)
{
    lcdUpdateValue(adjuster->valueRow, adjuster->valueCol, *adjuster->pValue);
}

static void selectAdjuster(ADJUSTER *adjuster)
{
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
                for (px = 0; px < state->pixelWidth; ++px)
                    state->buf[j + px] = color;
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

static void lcdUpdateValue(int row, int col, int value)
{
    char buf[10];
    sprintf(buf, "%02d", value);
    lcdPutStr(row, col, buf);    
}
