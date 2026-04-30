#pragma once

namespace tpanar_ns {

/**
 * A simple linear smoother for parameters to avoid audio clicks (zipper noise).
 */
class LinearSmoother {
public:
    LinearSmoother(float initial_value = 0.0f)
        : m_current(initial_value), m_target(initial_value), m_step(0.0f) {}

    void set_target(float target, size_t frames) {
        m_target = target;
        if (frames > 0) {
            m_step = (m_target - m_current) / (float)frames;
        } else {
            m_current = m_target;
            m_step = 0.0f;
        }
    }

    float next() {
        if (m_current != m_target) {
            m_current += m_step;
            // Handle overshoot
            if ((m_step > 0.0f && m_current > m_target) || (m_step < 0.0f && m_current < m_target)) {
                m_current = m_target;
                m_step = 0.0f;
            }
        }
        return m_current;
    }

    float current() const { return m_current; }

private:
    float m_current;
    float m_target;
    float m_step;
};

} // namespace tpanar_ns
