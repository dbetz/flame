/*
 * code to read a rotary encoder
 */

#include <propeller.h>
#include "encoder.h"

// require this many samples to match before accepting a new value
#define DEBOUNCE_TARGET 2000

static _COGMEM unsigned int pin;

static _COGMEM unsigned int nextValue;
static _COGMEM unsigned int nextCount;
static _COGMEM unsigned int tempValue;

static _COGMEM unsigned int lastValue;
static _COGMEM unsigned int thisValue;

_NATIVE void main(volatile struct encoder_mailbox *m)
{
    pin = m->pin;

    nextValue = -1;
    nextCount = 0;

    lastValue = 0;
    m->value = 0;

    for (;;) {

        tempValue = (INA >> pin) & 3;

        if (tempValue == nextValue) {
            if (++nextCount >= DEBOUNCE_TARGET) {
                thisValue = nextValue;
                nextCount = 0;
            }
        }
        else {
            nextValue = tempValue;
            nextCount = 0;
        }

        switch ((lastValue << 2) | thisValue) {
        case 0b0000:    // no movement
        case 0b0101:
        case 0b1111:
        case 0b1010:
            // nothing to do
            break;
        case 0b0001:    // clockwise
        case 0b0111:
        case 0b1110:
        case 0b1000:
            if (m->value < m->maxValue)
                ++m->value;
            else if (m->wrap)
                m->value = m->minValue;
            break;
        case 0b0010:    // counter-clockwise
        case 0b1011:
        case 0b1101:
        case 0b0100:
            if (m->value > m->minValue)
                --m->value;
            else if (m->wrap)
                m->value = m->maxValue;
            break;
        }

        lastValue = thisValue;
    }
}

