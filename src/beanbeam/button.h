#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>

class Button
{
public:
  Button(int b) : m_b(b), m_isInit(false) { }
  void poll();
  bool operator()();
private:
  const int m_b;
  bool m_isInit;
  bool m_lastReported;
  bool m_state;
  bool m_last;
  unsigned long time;  
      
};
#endif // BUTTON_H
