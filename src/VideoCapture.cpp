#include "VideoCapture.hpp"
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace {
struct Handle {
    HANDLE h=nullptr;
    ~Handle(){if(h && h!=INVALID_HANDLE_VALUE) CloseHandle(h);}
    Handle()=default; explicit Handle(HANDLE value):h(value){}
    Handle(Handle const&)=delete; Handle& operator=(Handle const&)=delete;
};
void require(HRESULT hr,char const* label) {
    if(FAILED(hr)) {std::ostringstream o;o<<label<<" (0x"<<std::hex<<static_cast<unsigned long>(hr)<<")";throw std::runtime_error(o.str());}
}
struct ComScope { HRESULT result=CoInitializeEx(nullptr,COINIT_MULTITHREADED); ~ComScope(){if(SUCCEEDED(result)) CoUninitialize();} };
class Activation final : public IActivateAudioInterfaceCompletionHandler, public IAgileObject {
    std::atomic<ULONG> refs{1};
public:
    Handle done{CreateEventW(nullptr,FALSE,FALSE,nullptr)};
    ComPtr<IAudioClient> client;
    HRESULT result=E_PENDING;
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override {auto r=--refs;if(!r) delete this;return r;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out) return E_POINTER; *out=nullptr;
        if(id==__uuidof(IUnknown) || id==__uuidof(IActivateAudioInterfaceCompletionHandler)) *out=static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
        else if(id==__uuidof(IAgileObject)) *out=static_cast<IAgileObject*>(this);
        else return E_NOINTERFACE;
        AddRef();return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* op) override {
        ComPtr<IUnknown> unknown; HRESULT activated=E_FAIL;
        result=op->GetActivateResult(&activated,&unknown);
        if(SUCCEEDED(result)) result=activated;
        if(SUCCEEDED(result)) result=unknown.As(&client);
        SetEvent(done.h);return S_OK;
    }
};
std::wstring quote(std::wstring const& s) {
    std::wstring out=L"\"";std::size_t slashes=0;
    for(auto c:s) {
        if(c==L'\\'){++slashes;continue;}
        if(c==L'"'){out.append(slashes*2+1,L'\\');out+=c;}
        else {out.append(slashes,L'\\');out+=c;}
        slashes=0;
    }
    out.append(slashes*2,L'\\');return out+L'"';
}
struct Process {
    Handle process, input;
    void start(std::filesystem::path const& exe,std::vector<std::wstring> const& args,std::filesystem::path const& log) {
        SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE}; Handle read;
        if(!CreatePipe(&read.h,&input.h,&sa,0)) throw std::runtime_error("Cannot create encoder control pipe.");
        SetHandleInformation(input.h,HANDLE_FLAG_INHERIT,0);
        Handle file(CreateFileW(log.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&sa,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
        if(file.h==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot write encoder log.");
        STARTUPINFOEXW si{};si.StartupInfo.cb=sizeof(si);si.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
        si.StartupInfo.hStdInput=read.h;si.StartupInfo.hStdOutput=file.h;si.StartupInfo.hStdError=file.h;
        SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);
        std::vector<unsigned char> storage(size);si.lpAttributeList=reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if(!InitializeProcThreadAttributeList(si.lpAttributeList,1,0,&size)) throw std::runtime_error("Encoder process attributes failed.");
        HANDLE allowed[]={read.h,file.h};
        if(!UpdateProcThreadAttribute(si.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,allowed,sizeof(allowed),nullptr,nullptr)) {
            DeleteProcThreadAttributeList(si.lpAttributeList);throw std::runtime_error("Encoder handle isolation failed.");
        }
        std::wstring command=quote(exe.wstring());for(auto const& a:args) command+=L" "+quote(a);
        PROCESS_INFORMATION pi{};
        BOOL ok=CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&si.StartupInfo,&pi);
        DeleteProcThreadAttributeList(si.lpAttributeList);
        if(!ok) throw std::runtime_error("Could not start FFmpeg. Choose a Windows FFmpeg executable.");
        process.h=pi.hProcess;CloseHandle(pi.hThread);
    }
    bool alive(){DWORD code=0;return GetExitCodeProcess(process.h,&code)&&code==STILL_ACTIVE;}
    void quit(){DWORD wrote=0;WriteFile(input.h,"q\n",2,&wrote,nullptr);}
    bool finish(DWORD timeout) {
        if(WaitForSingleObject(process.h,timeout)!=WAIT_OBJECT_0){TerminateProcess(process.h,1);WaitForSingleObject(process.h,5000);return false;}
        DWORD code=1;GetExitCodeProcess(process.h,&code);return code==0;
    }
    ~Process(){if(process.h && alive()){quit();if(!finish(5000)) TerminateProcess(process.h,1);}}
};
void le(std::ostream& f,std::uint32_t value,int bytes){for(int i=0;i<bytes;++i)f.put(static_cast<char>(value>>(8*i)));}
struct Wave {
    std::ofstream f;
    std::uint64_t frames=0;
    explicit Wave(std::filesystem::path const& path):f(path,std::ios::binary|std::ios::trunc) {
        if(!f) throw std::runtime_error("Could not open audio work file.");
        f.write("RIFF",4);le(f,0,4);f.write("WAVEfmt ",8);le(f,16,4);le(f,1,2);le(f,2,2);
        le(f,44100,4);le(f,44100*4,4);le(f,4,2);le(f,16,2);f.write("data",4);le(f,0,4);
    }
    void silence(std::uint64_t count) {
        static char zeros[4096]{};
        while(count){auto n=std::min<std::uint64_t>(count,1024);f.write(zeros,n*4);frames+=n;count-=n;}
    }
    void put(BYTE const* bytes,UINT32 count){f.write(reinterpret_cast<char const*>(bytes),count*4);frames+=count;}
    void close(){auto n=static_cast<std::uint32_t>(frames*4);f.seekp(4);le(f,n+36,4);f.seekp(40);le(f,n,4);f.close();if(!f)throw std::runtime_error("Audio file write failed.");}
};
double epochNow(){FILETIME f{};GetSystemTimePreciseAsFileTime(&f);ULARGE_INTEGER u{};u.LowPart=f.dwLowDateTime;u.HighPart=f.dwHighDateTime;return u.QuadPart/10000000.-11644473600.;}
double qpc100ns(){LARGE_INTEGER t{},frequency{};QueryPerformanceCounter(&t);QueryPerformanceFrequency(&frequency);return static_cast<double>(t.QuadPart)*10000000./frequency.QuadPart;}
double videoEpoch(std::filesystem::path const& log) {
    std::ifstream f(log);std::string line;
    while(std::getline(f,line)) {auto p=line.find("start: ");if(p!=std::string::npos)return std::stod(line.substr(p+7));}
    throw std::runtime_error("FFmpeg did not report its capture timestamp; audio synchronization could not be verified.");
}
}
namespace meter {
VideoCapture::~VideoCapture(){stop();if(m_worker.joinable())m_worker.join();}
void VideoCapture::stop(){m_stop=true;}
void VideoCapture::message(std::string value){std::lock_guard lock(m_mutex);m_message=std::move(value);}
std::string VideoCapture::message() const {std::lock_guard lock(m_mutex);return m_message;}
bool VideoCapture::start(HWND window,std::filesystem::path encoder,std::filesystem::path output,std::filesystem::path work) {
    auto state=m_state.load();if(state==State::Starting||state==State::Capturing||state==State::Finalizing)return false;
    if(m_worker.joinable())m_worker.join();m_stop=false;m_state=State::Starting;message("Starting game audio/video capture");
    m_worker=std::thread(&VideoCapture::run,this,window,std::move(encoder),std::move(output),std::move(work));return true;
}
void VideoCapture::run(HWND window,std::filesystem::path encoder,std::filesystem::path output,std::filesystem::path work) {
    try {
        ComScope com;require(com.result,"Windows audio initialization failed");
        DWORD pid=0;GetWindowThreadProcessId(window,&pid);
        if(pid!=GetCurrentProcessId()||!IsWindowVisible(window))throw std::runtime_error("The visible Geometry Dash window could not be identified.");
        wchar_t title[1024]{};GetWindowTextW(window,title,1024);if(!title[0])throw std::runtime_error("Game window has no capture title.");
        std::filesystem::create_directories(work);
        auto wavePath=work/L"game-audio.wav", videoPath=work/L"game-video.mkv", captureLog=work/L"capture.log";
        AUDIOCLIENT_ACTIVATION_PARAMS params{};params.ActivationType=AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        params.ProcessLoopbackParams.TargetProcessId=GetCurrentProcessId();
        params.ProcessLoopbackParams.ProcessLoopbackMode=PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
        PROPVARIANT variant{};variant.vt=VT_BLOB;variant.blob.cbSize=sizeof(params);variant.blob.pBlobData=reinterpret_cast<BYTE*>(&params);
        ComPtr<Activation> handler;handler.Attach(new Activation);
        if(!handler->done.h)throw std::runtime_error("Cannot create audio activation event.");
        ComPtr<IActivateAudioInterfaceAsyncOperation> op;
        require(ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,__uuidof(IAudioClient),&variant,handler.Get(),&op),"Game-only audio capture requires Windows build 20348 or later");
        if(WaitForSingleObject(handler->done.h,10000)!=WAIT_OBJECT_0)throw std::runtime_error("Game audio activation timed out.");
        require(handler->result,"Game-only audio capture is unavailable on this Windows installation");
        auto client=handler->client;
        WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=2;format.nSamplesPerSec=44100;
        format.wBitsPerSample=16;format.nBlockAlign=4;format.nAvgBytesPerSec=44100*4;
        require(client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_LOOPBACK|AUDCLNT_STREAMFLAGS_EVENTCALLBACK|AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,0,0,&format,nullptr),"Game audio format initialization failed");
        Handle audioEvent(CreateEventW(nullptr,FALSE,FALSE,nullptr));require(client->SetEventHandle(audioEvent.h),"Audio event setup failed");
        ComPtr<IAudioCaptureClient> capture;require(client->GetService(IID_PPV_ARGS(&capture)),"Audio capture service unavailable");
        Wave wave(wavePath);double audioQpc=qpc100ns(), audioEpoch=epochNow();
        require(client->Start(),"Could not start game audio capture");
        struct StopAudio{IAudioClient* p;~StopAudio(){p->Stop();}} stopAudio{client.Get()};
        if(m_stop)throw std::runtime_error("Video export cancelled before capture started.");
        Process encoderProcess;
        encoderProcess.start(encoder,{L"-hide_banner",L"-loglevel",L"info",L"-f",L"gdigrab",L"-framerate",L"60",L"-draw_mouse",L"0",L"-i",std::wstring(L"title=")+title,L"-vf",L"setpts=PTS-STARTPTS,pad=ceil(iw/2)*2:ceil(ih/2)*2",L"-c:v",L"libx264",L"-preset",L"veryfast",L"-crf",L"18",L"-pix_fmt",L"yuv420p",L"-an",L"-y",videoPath.wstring()},captureLog);
        m_state=State::Capturing;message("Capturing game window and game-only audio");
        while(!m_stop && encoderProcess.alive()) {
            if((qpc100ns()-audioQpc)>1200.*10000000.) {m_stop=true;break;}
            WaitForSingleObject(audioEvent.h,20);
            UINT32 available=0;require(capture->GetNextPacketSize(&available),"Audio packet query failed");
            while(available) {
                BYTE* data=nullptr;UINT32 count=0;DWORD flags=0;UINT64 device=0,clock=0;
                require(capture->GetBuffer(&data,&count,&flags,&device,&clock),"Audio packet read failed");
                auto target=(flags&AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)?wave.frames:static_cast<std::uint64_t>(std::max(0.,std::round((static_cast<double>(clock)-audioQpc)*44100./10000000.)));
                if(target>44100ull*1201){capture->ReleaseBuffer(count);throw std::runtime_error("Audio clock discontinuity.");}
                if(target>wave.frames)wave.silence(target-wave.frames);
                auto skip=static_cast<UINT32>(std::min<std::uint64_t>(wave.frames-target,count));
                if(flags&AUDCLNT_BUFFERFLAGS_SILENT)wave.silence(count-skip);else if(count>skip)wave.put(data+skip*4,count-skip);
                require(capture->ReleaseBuffer(count),"Audio packet release failed");
                require(capture->GetNextPacketSize(&available),"Audio packet query failed");
            }
        }
        m_state=State::Finalizing;message("Finalizing MP4 with game audio");
        client->Stop();auto endFrames=static_cast<std::uint64_t>((qpc100ns()-audioQpc)*44100./10000000.);
        if(endFrames>wave.frames)wave.silence(endFrames-wave.frames);wave.close();
        encoderProcess.quit();if(!encoderProcess.finish(30000))throw std::runtime_error("Video encoder failed; see capture.log in the video work folder.");
        double offset=videoEpoch(captureLog)-audioEpoch;
        if(!std::isfinite(offset)||offset<-.1||offset>30.)throw std::runtime_error("Audio/video clocks could not be aligned.");
        std::wostringstream time;time<<std::fixed<<std::setprecision(6)<<std::max(0.,offset);
        Process mux;
        mux.start(encoder,{L"-hide_banner",L"-i",videoPath.wstring(),L"-ss",time.str(),L"-i",wavePath.wstring(),L"-map",L"0:v:0",L"-map",L"1:a:0",L"-c:v",L"copy",L"-c:a",L"aac",L"-b:a",L"192k",L"-shortest",L"-movflags",L"+faststart",L"-y",output.wstring()},work/L"mux.log");
        if(!mux.finish(120000))throw std::runtime_error("MP4/audio finalization failed; recoverable work files were retained.");
        if(!std::filesystem::exists(output)||std::filesystem::file_size(output)==0)throw std::runtime_error("Encoder produced no video.");
        // Remove only these two known work files after a successful export; keep diagnostics.
        std::error_code ignored;std::filesystem::remove(wavePath,ignored);std::filesystem::remove(videoPath,ignored);
        message("MP4 saved: "+output.string());m_state=State::Done;
    }catch(std::exception const& e){message(e.what());m_state=State::Error;}
}
}

