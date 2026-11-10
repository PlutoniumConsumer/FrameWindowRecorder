#pragma once
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
namespace meter {
class VideoCapture {
public:
    enum class State { Idle, Starting, Capturing, Finalizing, Done, Error };
    ~VideoCapture();
    bool start(HWND window,std::filesystem::path encoder,std::filesystem::path output,std::filesystem::path work);
    void stop();
    State state() const {return m_state.load();}
    std::string message() const;
private:
    std::atomic<State> m_state{State::Idle};
    std::atomic<bool> m_stop{false};
    std::thread m_worker;
    mutable std::mutex m_mutex;
    std::string m_message;
    void message(std::string value);
    void run(HWND,std::filesystem::path,std::filesystem::path,std::filesystem::path);
};
}
