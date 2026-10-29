#ifndef FW_STUB_TRIGGER_H
#define FW_STUB_TRIGGER_H
#include <stdint.h>
enum { TRIGGER_1 = 0, TRIGGER_2, TRIGGER_3, TRIGGER_4, TRIGGER_5, TRIGGER_6, TRIGGER_7, TRIGGER_OFF = 0, TRIGGER_ON = 1 };
void trigger_triggerVoice (uint8_t voice, uint8_t onOff);
uint8_t trigger_isGateModeOn (void);
#endif
