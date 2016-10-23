/*
 * struct to pass data to the toggle cog driver
 */

struct encoder_mailbox {
    int pin;
    volatile int minValue;
    volatile int maxValue;
    volatile int value;
    volatile int wrap;
};
