#include "IOBoard.h"

void IOBoard::begin() {
    pinMode(PIN_RELAY1, OUTPUT); digitalWrite(PIN_RELAY1, LOW);
    pinMode(PIN_RELAY2, OUTPUT); digitalWrite(PIN_RELAY2, LOW);
    pinMode(PIN_RELAY3, OUTPUT); digitalWrite(PIN_RELAY3, LOW);
    pinMode(PIN_RELAY4, OUTPUT); digitalWrite(PIN_RELAY4, LOW);

    pinMode(PIN_INPUT1, INPUT_PULLUP);
    pinMode(PIN_INPUT2, INPUT_PULLUP);
    pinMode(PIN_INPUT3, INPUT_PULLUP);
    pinMode(PIN_INPUT4, INPUT_PULLUP);
}

void IOBoard::_setRelay(uint8_t pin, bool on) {
    digitalWrite(pin, on ? LOW : HIGH);
}

bool IOBoard::_readInput(uint8_t pin) {
    return digitalRead(pin) == LOW; // LOW = acionado (lógica invertida)
}

void IOBoard::relay1(bool on) { _setRelay(PIN_RELAY1, on); }
void IOBoard::relay2(bool on) { _setRelay(PIN_RELAY2, on); }
void IOBoard::relay3(bool on) { _setRelay(PIN_RELAY3, on); }
void IOBoard::relay4(bool on) { _setRelay(PIN_RELAY4, on); }

void IOBoard::allRelays(bool on) {
    relay1(on); relay2(on); relay3(on); relay4(on);
}

bool IOBoard::input1() { return _readInput(PIN_INPUT1); }
bool IOBoard::input2() { return _readInput(PIN_INPUT2); }
bool IOBoard::input3() { return _readInput(PIN_INPUT3); }
bool IOBoard::input4() { return _readInput(PIN_INPUT4); }
