#include "../src/VideoCapture.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
void wait(meter::VideoCapture& v){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(v.state()==meter::VideoCapture::State::Starting&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(10));if(v.state()!=meter::VideoCapture::State::Error)throw std::runtime_error("Invalid/non-game window must produce an error before capture");if(v.message().empty())throw std::runtime_error("Failure must have a visible explanation");}
int main(){try{meter::VideoCapture v;auto root=std::filesystem::temp_directory_path()/"ofm-video-guard-test";if(!v.start(nullptr,L"not-an-encoder.exe",root/L"never-created.mp4",root))return 1;wait(v);if(!v.start(GetDesktopWindow(),L"not-an-encoder.exe",root/L"never-created.mp4",root))return 1;wait(v);v.stop();std::cout<<"Video target guards and worker restart passed; no window/audio was captured\n";return 0;}catch(std::exception const& e){std::cerr<<e.what()<<'\n';return 1;}}
