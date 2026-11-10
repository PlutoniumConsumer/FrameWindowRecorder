#include "../src/EncoderDiscovery.hpp"
#include <iostream>
#include <fstream>
int main(){
    namespace fs=std::filesystem;
    auto root=fs::temp_directory_path()/L"frame-window-encoder-test";
    fs::create_directories(root);
    auto file=root/L"encoder-\u03A9.exe";
    {std::ofstream f(file);f<<"test fixture";}
    if(meter::discoverEncoder(file)!=file)return 1;
    auto portable=meter::discoverEncoder(root/L"missing.exe");
    if(portable.empty() || !fs::is_regular_file(portable) || portable==root)return 2;
    if(meter::discoverEncoder(root)==root)return 3;
    fs::remove(file);fs::remove(root);
    std::cout<<"Unicode override and stale/directory fallback passed. Automatically found: "<<portable.string()<<'\n';
}
