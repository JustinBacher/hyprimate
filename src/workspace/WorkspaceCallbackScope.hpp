#pragma once

#include <utility>

namespace hyprimate::workspace {
// Restore borrowed callback state on every exit, including capture exceptions.
template<class T> class ScopedRestore {
  public:
    explicit ScopedRestore(T &value) : m_value(value), m_saved(value) {}
    ~ScopedRestore() { m_value = std::move(m_saved); }
    ScopedRestore(const ScopedRestore &) = delete;
    ScopedRestore(ScopedRestore &&) = delete;
    ScopedRestore &operator=(ScopedRestore &&) = delete;
    ScopedRestore &operator=(const ScopedRestore &) = delete;
  private:
    T &m_value;
    T m_saved;
};

template<class Callback> class CallbackExit {
  public:
    explicit CallbackExit(Callback callback) : m_callback(std::move(callback)) {}
    ~CallbackExit() { m_callback(); }
    CallbackExit(const CallbackExit &) = delete;
    CallbackExit &operator=(const CallbackExit &) = delete;
    CallbackExit(CallbackExit &&) = delete;
    CallbackExit &operator=(CallbackExit &&) = delete;
  private:
    Callback m_callback;
};
} // namespace hyprimate::workspace
