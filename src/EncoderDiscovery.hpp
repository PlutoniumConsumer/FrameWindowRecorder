#pragma once
#include <Windows.h>
#include <filesystem>
#include <vector>
namespace meter {
inline std::filesystem::path discoverEncoder(std::filesystem::path preferred={}) {
    namespace fs=std::filesystem;
    auto valid=[](fs::path const& p){std::error_code e;return !p.empty()&&fs::is_regular_file(p,e);};
    if(valid(preferred))return preferred;
    wchar_t buffer[32768]{};
    auto n=SearchPathW(nullptr,L"ffmpeg.exe",nullptr,32768,buffer,nullptr);
    if(n && n<32768 && valid(buffer))return fs::path(buffer);
    std::vector<fs::path> candidates;
    auto env=[](wchar_t const* key){wchar_t b[32768]{};auto n=GetEnvironmentVariableW(key,b,32768);return n&&n<32768?fs::path(b):fs::path{};};
    GetModuleFileNameW(nullptr,buffer,32768);auto game=fs::path(buffer).parent_path();
    candidates={game/L"ffmpeg.exe",game/L"bin/ffmpeg.exe",game/L"tools/ffmpeg/bin/ffmpeg.exe",L"C:/ffmpeg/bin/ffmpeg.exe"};
    auto local=env(L"LOCALAPPDATA"),user=env(L"USERPROFILE"),programs=env(L"ProgramFiles");
    if(!local.empty())candidates.push_back(local/L"Microsoft/WinGet/Links/ffmpeg.exe");
    if(!programs.empty())candidates.push_back(programs/L"FFmpeg/bin/ffmpeg.exe");
    if(!user.empty())candidates.push_back(user/L"scoop/shims/ffmpeg.exe");
    // Also recognize the preinstalled portable tool in this project's workspace.
    // Only two known directory levels are inspected, never the whole drive.
    auto children=[](fs::path const& root){std::vector<fs::path> result;std::error_code e;fs::directory_iterator it(root,fs::directory_options::skip_permission_denied,e),end;while(!e&&it!=end){if(it->is_directory(e))result.push_back(it->path());it.increment(e);}return result;};
    if(!user.empty())for(auto const& project:children(user/L"Documents/Codex"))for(auto const& build:children(project/L"work/tools/ffmpeg"))candidates.push_back(build/L"bin/ffmpeg.exe");
    // Dated Codex workspaces use an additional project directory.
    if(!user.empty())for(auto const& date:children(user/L"Documents/Codex"))for(auto const& project:children(date))for(auto const& build:children(project/L"work/tools/ffmpeg"))candidates.push_back(build/L"bin/ffmpeg.exe");
    for(auto const& p:candidates)if(valid(p))return p;
    return {};
}
}
