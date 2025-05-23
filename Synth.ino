#include <avr/io.h>
#include <avr/interrupt.h>
#include <Arduino.h>

uint16_t syncPhaseAcc;
uint16_t syncPhaseInc;
uint16_t grainPhaseAcc;
uint16_t grainPhaseInc;
uint16_t grainAmp;
uint8_t grainDecay;
uint16_t grain2PhaseAcc;
uint16_t grain2PhaseInc;
uint16_t grain2Amp;
uint8_t grain2Decay;

boolean triggerGate;

// Analog input mappings
#define SYNC_CONTROL         (0)
#define GRAIN_FREQ_CONTROL   (1)
#define GRAIN_DECAY_CONTROL  (2)
#define GRAIN2_FREQ_CONTROL  (3)
#define GRAIN2_DECAY_CONTROL (4)
#define GATE_CONTROL         (2)

// PWM output pins
#define PWM_PIN       3                 // OCR2B
#define PWM_PIN_B     11                // OCR2A (inverted)
#define PWM_VALUE     OCR2B
#define PWM_VALUE_B   OCR2A
#define LED_PIN       13
#define LED_PORT      PORTB
#define LED_BIT       5
#define PWM_INTERRUPT TIMER2_OVF_vect

// Lookup tables (same as original)
uint16_t antilogTable[] = {
  64830, 64132, 63441, 62757, 62081, 61413, 60751, 60097, 59449, 58809, 58176, 57549, 56929, 56316, 55709, 55109,
  54515, 53928, 53347, 52773, 52204, 51642, 51085, 50535, 49991, 49452, 48920, 48393, 47871, 47356, 46846, 46341,
  45842, 45348, 44859, 44376, 43898, 43425, 42958, 42495, 42037, 41584, 41136, 40693, 40255, 39821, 39392, 38968,
  38548, 38133, 37722, 37316, 36914, 36516, 36123, 35734, 35349, 34968, 34591, 34219, 33850, 33486, 33125, 32768
};
uint16_t mapPhaseInc(uint16_t input) {
  return (antilogTable[input & 0x3f]) >> (input >> 6);
}

uint16_t midiTable[] = {
  17, 18, 19, 20, 22, 23, 24, 26, 27, 29, 31, 32, 34, 36, 38, 41, 43, 46, 48, 51, 54, 58, 61, 65, 69, 73,
  77, 82, 86, 92, 97, 103, 109, 115, 122, 129, 137, 145, 154, 163, 173, 183, 194, 206, 218, 231,
  244, 259, 274, 291, 308, 326, 346, 366, 388, 411, 435, 461, 489, 518, 549, 581, 616, 652, 691,
  732, 776, 822, 871, 923, 978, 1036, 1097, 1163, 1232, 1305, 1383, 1465, 1552, 1644, 1742,
  1845, 1955, 2071, 2195, 2325, 2463, 2610, 2765, 2930, 3104, 3288, 3484, 3691, 3910, 4143,
  4389, 4650, 4927, 5220, 5530, 5859, 6207, 6577, 6968, 7382, 7821, 8286, 8779, 9301, 9854,
  10440, 11060, 11718, 12415, 13153, 13935, 14764, 15642, 16572, 17557, 18601, 19708, 20879,
  22121, 23436, 24830, 26306
};
uint16_t mapMidi(uint16_t input) {
  return (midiTable[(1023 - input) >> 3]);
}

uint16_t pentatonicTable[54] = {
  0, 19, 22, 26, 29, 32, 38, 43, 51, 58, 65, 77, 86, 103, 115, 129, 154, 173, 206, 231, 259, 308, 346,
  411, 461, 518, 616, 691, 822, 923, 1036, 1232, 1383, 1644, 1845, 2071, 2463, 2765, 3288,
  3691, 4143, 4927, 5530, 6577, 7382, 8286, 9854, 11060, 13153, 14764, 16572, 19708, 22121, 26306
};
uint16_t mapPentatonic(uint16_t input) {
  uint8_t value = (1023 - input) / (1024 / 53);
  return (pentatonicTable[value]);
}

uint16_t fibonacciBeatTable[20] = {
  1, 1, 2 , 3, 5, 8, 13, 21, 34, 55, 89, 144
};
uint16_t mapFibonacciBeat(uint16_t input) {
  uint8_t value = (1023 - input) / (1024 / 20);
  return (fibonacciBeatTable[value]);
}

uint16_t bpmTable[16] = {
  1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
};
uint16_t mapBPM(uint16_t input) {
  uint8_t value = (1023 - input) / (1024 / 16);
  return (bpmTable[value]);
}

// Initialize PWM
void audioOn() {
  TCCR2A = _BV(COM2B1) | _BV(COM2A1) | _BV(WGM20);  // Enable both OCR2B (D3) and OCR2A (D11)
  TCCR2B = _BV(CS20);                               // No prescaler
  TIMSK2 = _BV(TOIE2);                              // Enable Timer2 overflow interrupt
}

void setup() {
  pinMode(PWM_PIN, OUTPUT);       // D3
  pinMode(PWM_PIN_B, OUTPUT);     // D11
  pinMode(LED_PIN, OUTPUT);
  pinMode(GATE_CONTROL, INPUT);   // Gate input
  audioOn();
}

void loop() {
  if (digitalRead(GATE_CONTROL) == HIGH) {
    if (triggerGate == true) {
      grainStart();
      triggerGate = false;
    }
    grainBuild();
  } else {
    triggerGate = true;
    grainBuild();
  }
}

void grainStart() {
  grainPhaseAcc = 0;
  grainAmp = 0x7fff;
  grain2PhaseAcc = 0;
  grain2Amp = 0x7fff;
  LED_PORT ^= 1 << LED_BIT;
}

void grainBuild() {
  syncPhaseInc = mapBPM(analogRead(SYNC_CONTROL));
  grainPhaseInc  = mapPhaseInc(analogRead(GRAIN_FREQ_CONTROL)) / 2;
  grainDecay     = analogRead(GRAIN_DECAY_CONTROL) / 8;
  grain2PhaseInc = mapPhaseInc(analogRead(GRAIN2_FREQ_CONTROL)) / 2;
  grain2Decay    = analogRead(GRAIN2_DECAY_CONTROL) / 4;
}

SIGNAL(PWM_INTERRUPT) {
  uint8_t value;
  uint16_t output;

  syncPhaseAcc += syncPhaseInc;
  if (syncPhaseAcc < syncPhaseInc) {
    if (digitalRead(GATE_CONTROL) == HIGH) {
      grainStart();
    }
  }

  grainPhaseAcc += grainPhaseInc;
  grain2PhaseAcc += grain2PhaseInc;

  value = (grainPhaseAcc >> 7) & 0xff;
  if (grainPhaseAcc & 0x8000) value = ~value;
  output = value * (grainAmp >> 8);

  value = (grain2PhaseAcc >> 7) & 0xff;
  if (grain2PhaseAcc & 0x8000) value = ~value;
  output += value * (grain2Amp >> 8);

  grainAmp -= (grainAmp >> 8) * grainDecay;
  grain2Amp -= (grain2Amp >> 8) * grain2Decay;

  output >>= 9;
  if (output > 255) output = 255;

  OCR2B = output;         // D3
  OCR2A = 255 - output;   // D11 (inverted)
}
