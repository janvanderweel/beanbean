#include "button.h"
#include <Arduino_DebugUtils.h>

void Button::poll()
{
    const unsigned long delta = 10;
    if (! m_isInit)
    {
        m_isInit = true;
        pinMode(m_b, INPUT_PULLUP);
        m_last = digitalRead(m_b);
        m_state = m_last;
        m_lastReported  = m_last;
        time = millis();
    }
    int s = digitalRead(m_b) == LOW;
//    digitalWrite(LED_BUILTIN, s);
    if (s != m_last)
    {
      m_last = s;
      time = millis();
//      Debug.print(DBG_VERBOSE, " Button:poll %d, %lu", m_last, time);
    }
    else if (m_state != m_last)
    {
       unsigned long t = millis() - time;
       if (t > delta)
       {
          m_state = m_last;
//          Debug.print(DBG_VERBOSE, " Button:pollX %d, %lu", m_last, t);
       }
    }
}

bool Button::operator()() 
{
    poll(); 
    bool rc = m_state && !  m_lastReported;
//     Debug.print(DBG_VERBOSE, " Button:() S:%d, R:%d, >:%d\n", m_state, m_lastReported, rc);
    m_lastReported  = m_state;
    return rc;
}
