#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>

class Button
{
public:
  Button(int b, bool activeLow = true)
    : m_b(b), m_activeLow(activeLow), m_isInit(false), m_lastReported(false), m_state(false), m_last(false), time(0) { }
  void poll();
  bool operator()();
private:
  const int m_b;
  const bool m_activeLow;
  bool m_isInit;
  bool m_lastReported;
  bool m_state;
  bool m_last;
  unsigned long time;
};
#endif // BUTTON_H
