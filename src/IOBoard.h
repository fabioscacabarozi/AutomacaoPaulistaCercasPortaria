#pragma once
#include <Arduino.h>

// ── Pinagem confirmada por diagnóstico ────────────────────────────────────────
#define PIN_RELAY1  23
#define PIN_RELAY2   5
#define PIN_RELAY3   4
#define PIN_RELAY4  13

#define PIN_INPUT1  25
#define PIN_INPUT2  26
#define PIN_INPUT3  27
#define PIN_INPUT4  33

class IOBoard {
public:
    void begin();

    // Saídas (relés) — true = ligado, false = desligado
    void relay1(bool on);
    void relay2(bool on);
    void relay3(bool on);
    void relay4(bool on);
    void allRelays(bool on);

    // Entradas — true = acionado, false = solto
    bool input1();
    bool input2();
    bool input3();
    bool input4();

private:
    void _setRelay(uint8_t pin, bool on);
    bool _readInput(uint8_t pin);
};
