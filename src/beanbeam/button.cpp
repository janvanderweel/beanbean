#include "button.h"
#include <Arduino_DebugUtils.h>

void Button::poll()
{
    const unsigned long delta = 10;

    if (!m_isInit)
    {
        pinMode(m_b, m_activeLow ? INPUT_PULLUP : INPUT);
        const bool raw = digitalRead(m_b);
        const bool pressed = (m_activeLow ? (raw == LOW) : (raw == HIGH));
        m_isInit = true;
        m_last = pressed;
        m_state = pressed;
        m_lastReported = pressed;
        time = millis();
        return;
    }

    const bool raw = digitalRead(m_b);
    const bool pressed = (m_activeLow ? (raw == LOW) : (raw == HIGH));

    if (pressed != m_last)
    {
        m_last = pressed;
        time = millis();
        return;
    }

    if ((m_state != m_last) && ((millis() - time) > delta))
    {
        m_state = m_last;
    }
}

bool Button::operator()()
{
    poll();
    const bool edge = m_state && !m_lastReported;
    m_lastReported = m_state;
    return edge;
}
