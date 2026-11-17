#ifdef HP2_APPLICATION_ENTRY
#include "indep/inc/Flags.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int)
{
    int argc{};auto** argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(!argv) {MessageBoxW(nullptr,L"Could not read the command line.",L"NFSHP2",MB_OK|MB_ICONERROR);return 1;}
    const auto result=hp2::runNativeWindowsApplication(argc,argv);
    LocalFree(argv);return result;
}
#else
#include "indep/inc/Timer.hpp"
#include "indep/inc/Ecstasy.hpp"
#include "indep/inc/Flags.hpp"
#include "bware/inc/bFile.hpp"
#include "bsound/include/NativeWineAudio.hpp"

#include <atomic>
#include <algorithm>
#include <array>
#include <filesystem>
#include <exception>
#include <csignal>
#include <cmath>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include "resources/NativeAppResources.h"
#endif

namespace {
std::atomic<uint32_t> videoBlankCount{0};
}

uint32_t platformVideoBlankCount(void)
{
    return videoBlankCount.load(std::memory_order_acquire);
}

void platformNotifyVideoBlank(void)
{
    // The native source-clock transport publishes measured 50/59.94 Hz
    // intervals into this original VBlank cell. GPU presentation/fences,
    // simulation steps and real-time quantization have separate counters.
    videoBlankCount.fetch_add(1u, std::memory_order_release);
}

namespace hp2 {
namespace {
using NativeFatalSink=void(*)(const char*) noexcept;
std::atomic<NativeFatalSink> nativeFatalSink{nullptr};
}
extern "C" void hp2NativeRecordFailure(const char* reason)
{
    if(const auto sink=nativeFatalSink.load(std::memory_order_acquire))sink(reason);
    else if(reason)std::fputs(reason,stderr);
}
// InitDisplaySystem, PAL1007B8..100804; beta100750's complete body and
// immediate N_SOL/PDR/MAP record identify this original Platform.cpp owner.
// The physical window already exists on PC. Its source video clock still
// follows the disc marker, including the PAL first-launch language policy.
void InitDisplaySystem(const GameResources& files,GameTime& clock)
{
    gameTimeSetVideoMode(&clock,files.TryOpenPhysical("PAL.TAG") ? HP2_VIDEO_PAL : HP2_VIDEO_NTSC);
}

namespace {
namespace fs=std::filesystem;
std::string UpperPathAscii(std::string text)
{
    for(char& c:text)if(c>='a' && c<='z')c=static_cast<char>(c-'a'+'A');
    return text;
}
fs::path ProcessChild(const fs::path& directory,const char* name)
{
    std::error_code error;
    if(!fs::is_directory(directory,error))return {};
    const auto wanted=UpperPathAscii(name);fs::path result;
    fs::directory_iterator current(directory,error),end;
    if(error)throw std::system_error(error,"Read native game-data directory: "+directory.u8string());
    for(;current!=end;current.increment(error)) {
        if(error)throw std::system_error(error,"Read native game-data directory: "+directory.u8string());
        if(UpperPathAscii(current->path().filename().u8string())!=wanted)continue;
        if(!result.empty())throw std::runtime_error("Case-ambiguous native data path: "+directory.u8string()+"/"+name);
        result=current->path();
    }
    if(error)throw std::system_error(error,"Read native game-data directory: "+directory.u8string());
    return result;
}
bool OrdinaryFile(const fs::path& path)
{
    if(path.empty())return false;
    std::error_code error;return fs::is_regular_file(path,error);
}
fs::path ResolveArgumentPath(const fs::path& value,const fs::path& directory)
{
    // D:\\F:\\... was the invalid path in the first user launch. Do not
    // silently strip a drive name and accidentally open another game tree.
    const auto text=value.u8string();
    const auto firstColon=text.find(':');
    if(firstColon!=std::string::npos && text.find(':',firstColon+1)!=std::string::npos)
        throw std::invalid_argument("Invalid native path with more than one drive prefix: "+text);
    return (value.is_absolute()?value:directory/value).lexically_normal();
}
int32_t DisplayInteger(const fs::path& value,const char* option)
{
    const auto text=value.u8string();std::size_t used{};long number{};
    try {number=std::stol(text,&used,10);}catch(const std::exception&) {
        throw std::invalid_argument(std::string("Invalid ")+option+" value: "+text);
    }
    if(used!=text.size() || number<1 || number>16384)
        throw std::invalid_argument(std::string("Invalid ")+option+" value: "+text);
    return static_cast<int32_t>(number);
}
std::vector<fs::path> NativeDataDirectories(const fs::path& executableDirectory)
{
    std::vector<fs::path> directories;
    auto add=[&](const fs::path& path) {
        if(path.empty())return;
        const auto normalized=path.lexically_normal();
        if(std::find(directories.begin(),directories.end(),normalized)==directories.end())
            directories.push_back(normalized);
    };
    // Automatic discovery belongs to this installation only. A shortcut's
    // working directory or a parent project can contain a different build.
    add(executableDirectory);
    for(const auto* name:{"NFSHP2","@ZZDATA","ZZDATA","Data"})
        add(ProcessChild(executableDirectory,name));
    const auto game=ProcessChild(executableDirectory,"NFSHP2");
    if(!game.empty()) {add(ProcessChild(game,"@ZZDATA"));add(ProcessChild(game,"ZZDATA"));}
    return directories;
}
bool LooseGameDirectory(const fs::path& directory)
{
    const auto global=ProcessChild(directory,"GLOBAL");
    if(global.empty())return false;
    return OrdinaryFile(ProcessChild(global,"GLOBALB.LZC")) || OrdinaryFile(ProcessChild(global,"GLOBALB.BUN"));
}
}

std::uint64_t NativeRefreshClock::Observe(std::uint64_t monotonicQpc,
    std::uint64_t qpcFrequency,std::int32_t videoMode)
{
    constexpr std::uint64_t scale=262144; // Exact denominator of float32 0x426FC28F.
    if(!qpcFrequency || qpcFrequency>std::numeric_limits<std::uint64_t>::max()/scale)
        throw std::invalid_argument("Invalid native display QPC frequency");
    const auto add=[](std::uint64_t left,std::uint64_t right) {
        if(right>std::numeric_limits<std::uint64_t>::max()-left)
            throw std::overflow_error("Native display clock overflow");
        return left+right;
    };
    const bool ntsc=videoMode!=0;
    if(!initialized_ || ntsc!=ntsc_ || frequency_!=qpcFrequency || monotonicQpc<lastQpc_) {
        // Rebase only for an actual source-mode/frequency change or a
        // backward native timestamp. Repeated polls keep fractional carry.
        initialized_=true;ntsc_=ntsc;frequency_=qpcFrequency;
        lastQpc_=monotonicQpc;remainder_=0;
        return 0;
    }
    const auto elapsed=monotonicQpc-lastQpc_;
    lastQpc_=monotonicQpc;
    const std::uint64_t denominator=qpcFrequency*scale;
    const std::uint32_t numerator=ntsc ? 15712911u:13107200u;
    // Compute elapsed*numerator/denominator with an exact fractional carry.
    // The 24-bit multiply is split into quotient/remainder additions so a
    // long idle cannot overflow an intermediate QPC product. This remains
    // ordinary native clock conversion, independent of render completion.
    std::uint64_t ticks=0,tail=0;
    const auto whole=elapsed/denominator,part=elapsed%denominator;
    const auto addRemainder=[&](std::uint64_t value) {
        if(tail>=denominator-value) {tail-=denominator-value;return std::uint64_t{1};}
        tail+=value;return std::uint64_t{0};
    };
    for(int bit=23;bit>=0;--bit) {
        ticks=add(ticks,ticks);ticks=add(ticks,addRemainder(tail));
        if(numerator&(std::uint32_t{1}<<bit)) {
            ticks=add(ticks,whole);ticks=add(ticks,addRemainder(part));
        }
    }
    ticks=add(ticks,addRemainder(remainder_));remainder_=tail;
    return ticks;
}

std::uint64_t NativeRefreshClock::QpcUntilNextRefresh() const
{
    if(!initialized_)throw std::logic_error("Native source clock has no QPC baseline");
    constexpr std::uint64_t scale=262144;
    const auto remaining=frequency_*scale-remainder_;
    const std::uint32_t numerator=ntsc_ ? 15712911u:13107200u;
    // Exact ceiling without overflowing remaining+numerator-1. A wait that
    // wakes early must observe QPC again; no elapsed tick is assumed here.
    return remaining/numerator+(remaining%numerator!=0);
}
bool NativeRefreshClock::Advanced(std::uint32_t before,std::uint32_t current) noexcept
{
    // PAL MainLoop12F64C..12F65C: signed32(current-start)>0. Spell the
    // two's-complement test in unsigned arithmetic, including counter wrap.
    const auto difference=current-before;
    return difference!=0 && difference<0x80000000u;
}
void NativeFrameTiming::Reset(Clock::time_point now) noexcept
{
    *this={};initialized_=true;windowStart_=lastObservation_=now;
}

std::array<char,48> nativeFrameRateCaption() noexcept
{
    std::array<char,48> caption{};
    if(applicationShowFrameRate) {
        if(applicationFrameTiming.frameRateAvailable)
            std::snprintf(caption.data(),caption.size(),"FPS: %.0f",std::round(applicationFrameTiming.framesPerSecond));
        else std::snprintf(caption.data(),caption.size(),"FPS: --");
    }
    return caption;
}

bool NativeFrameTiming::Observe(Clock::time_point now,
    const EcstasyVulkanFrameStatistics* renderer,double updateMilliseconds) noexcept
{
    if(!initialized_ || now<lastObservation_) {Reset(now);return false;}
    if(now==lastObservation_)return false;
    lastObservation_=now;
    if(renderer) {
        hasPresentationTarget_=hasPresentationTarget_ || renderer->presentationTarget;
        if(renderer->submittedFrame && renderer->submittedFrame!=lastSubmission_) {
            lastSubmission_=renderer->submittedFrame;
            if(renderer->presentationTarget && renderer->presentAccepted)++presentedFrames_;
            const auto cpu=updateMilliseconds+renderer->gatherMilliseconds+renderer->recordSubmitMilliseconds;
            if(std::isfinite(cpu) && cpu>=0.0) {cpuMilliseconds_+=cpu;++cpuSamples_;}
        }
        if(renderer->gpuTimeAvailable && renderer->completedGpuFrame &&
            renderer->completedGpuFrame!=lastGpuSubmission_ &&
            std::isfinite(renderer->gpuMilliseconds) && renderer->gpuMilliseconds>0.0) {
            lastGpuSubmission_=renderer->completedGpuFrame;
            gpuMilliseconds_+=renderer->gpuMilliseconds;++gpuSamples_;
        }
    }
    const auto elapsed=std::chrono::duration<double>(now-windowStart_).count();
    if(elapsed<0.5)return false;
    published_={};
    published_.presentedFrames=presentedFrames_;
    published_.cpuSamples=cpuSamples_;published_.gpuSamples=gpuSamples_;
    published_.frameRateAvailable=hasPresentationTarget_ && presentedFrames_!=0;
    published_.cpuTimeAvailable=cpuSamples_!=0;published_.gpuTimeAvailable=gpuSamples_!=0;
    if(published_.frameRateAvailable) {
        published_.framesPerSecond=static_cast<double>(presentedFrames_)/elapsed;
        published_.frameMilliseconds=elapsed*1000.0/static_cast<double>(presentedFrames_);
    }
    if(cpuSamples_)published_.cpuMilliseconds=cpuMilliseconds_/static_cast<double>(cpuSamples_);
    if(gpuSamples_)published_.gpuMilliseconds=gpuMilliseconds_/static_cast<double>(gpuSamples_);
    windowStart_=now;presentedFrames_=cpuSamples_=gpuSamples_=0;
    cpuMilliseconds_=gpuMilliseconds_=0.0;hasPresentationTarget_=false;
    return true;
}

void NativeSimulationClock::Reset(Clock::time_point now) noexcept
{
    deadline_=now;remainder_=0;initialized_=true;
}
bool NativeSimulationClock::UpdateDue(Clock::time_point now) const noexcept
{
    return initialized_ && now>=deadline_;
}
void NativeSimulationClock::CompleteStep(std::uint32_t elapsedQuanta)
{
    if(!initialized_ || !elapsedQuanta || elapsedQuanta>255u)
        throw std::invalid_argument("Native logical update has an invalid recorded timestep");
    constexpr std::uint64_t nanosecondsPerSecond=1000000000u;
    const auto scaled=static_cast<std::uint64_t>(elapsedQuanta)*nanosecondsPerSecond+remainder_;
    const auto elapsed=scaled/3600u;remainder_=static_cast<std::uint32_t>(scaled%3600u);
    deadline_+=std::chrono::duration_cast<Clock::duration>(std::chrono::nanoseconds(elapsed));
}

bool NativeApplicationHost::WaitForSourceRefresh(std::uint32_t)
{
    return PollEvents();
}

void NativePresentationClock::SetLimit(std::int32_t framesPerSecond)
{
    if(framesPerSecond<0 || framesPerSecond>1000)throw std::invalid_argument("Presentation limit must be 0..1000 FPS");
    limit_=framesPerSecond;remainder_=0;deadline_={};
}
void NativePresentationClock::Reset(Clock::time_point now) noexcept
{
    deadline_=now;remainder_=0;
}
bool NativePresentationClock::Due(Clock::time_point now) const noexcept
{
    return limit_==0 || now>=deadline_;
}
void NativePresentationClock::Presented(Clock::time_point started) noexcept
{
    if(!limit_)return;
    const auto rate=static_cast<std::uint32_t>(limit_);
    const std::uint64_t scaled=1000000000ull+remainder_;
    remainder_=static_cast<std::uint32_t>(scaled%rate);
    const auto interval=std::chrono::duration_cast<Clock::duration>(std::chrono::nanoseconds(scaled/rate));
    // A late frame nearer the next slot must not be followed by a short burst.
    if(started-deadline_>=interval/2)deadline_=started;
    deadline_+=interval;
}
NativePresentationClock::Clock::time_point NativePresentationClock::Deadline() const noexcept
{
    return limit_==0?Clock::time_point::max():deadline_;
}

std::filesystem::path nativeExecutablePath()
{
#ifdef _WIN32
    std::vector<wchar_t> buffer(512);
    for(;;) {
        const DWORD count=GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));
        if(!count)throw std::system_error(static_cast<int>(GetLastError()),std::system_category(),"Locate NFSHP2.exe");
        if(count<buffer.size())return std::filesystem::path(std::wstring(buffer.data(),count));
        if(buffer.size()>=32768)throw std::runtime_error("Executable path exceeds Windows path limit");
        buffer.resize(std::min<std::size_t>(32768,buffer.size()*2));
    }
#elif defined(__linux__)
    return std::filesystem::read_symlink("/proc/self/exe");
#else
    throw std::runtime_error("Executable-directory lookup is unavailable on this host");
#endif
}

namespace {
NativeProcessLaunch ParseNativeProcessLaunch(const std::filesystem::path& executable,
    const std::filesystem::path& workingDirectory,const std::vector<std::filesystem::path>& arguments)
{
    const auto work=std::filesystem::absolute(workingDirectory).lexically_normal();
    const auto directory=ResolveArgumentPath(executable,work).parent_path();
    NativeProcessLaunch result;result.profileDirectory=directory/"Saves";
    bool sourceSelected=false;
    for(std::size_t i=0;i<arguments.size();++i) {
        const auto& argument=arguments[i];
        auto next=[&]() -> const std::filesystem::path& {
            if(++i>=arguments.size())throw std::invalid_argument("Missing value after "+argument.u8string());
            return arguments[i];
        };
        if(argument=="--data" || argument=="--zzdata") {
            if(sourceSelected)throw std::invalid_argument("Use one data source: --data or --zzdata");
            sourceSelected=true;result.archiveData=argument=="--zzdata";
            if(result.archiveData && (i+1==arguments.size() || arguments[i+1].u8string().rfind("--",0)==0))continue;
            result.dataSource=ResolveArgumentPath(next(),work);
        } else if(argument=="--profile") {result.profileDirectory=ResolveArgumentPath(next(),work);result.profileSpecified=true;}
        else if(argument=="--config") {result.configuration=ResolveArgumentPath(next(),work);result.configurationSpecified=true;}
        else if(argument=="--width") {result.width=DisplayInteger(next(),"--width");result.changeDisplay=true;}
        else if(argument=="--height") {result.height=DisplayInteger(next(),"--height");result.changeDisplay=true;}
        else if(argument=="--fullscreen" || argument=="--windowed") {
            result.fullscreen=argument=="--fullscreen";result.fullscreenSpecified=true;result.changeDisplay=true;
        } else if(argument=="--vsync" || argument=="--no-vsync") {
            result.vsync=argument=="--vsync";result.vsyncSpecified=true;result.changeDisplay=true;
        } else if(argument=="--show-fps" || argument=="--hide-fps") {
            result.showFps=argument=="--show-fps";
        } else if(argument=="--debug" || argument=="--no-debug") {
            result.debugTools=argument=="--debug";result.debugSpecified=true;
        } else if(argument=="--unlock-all" || argument=="--no-unlock-all") {
            result.unlockAll=argument=="--unlock-all";result.unlockSpecified=true;
        } else if(argument=="--filter") {
            const auto value=next().u8string();
            if(value=="authored")result.textureFilter=0;
            else if(value=="bilinear")result.textureFilter=1;
            else if(value=="trilinear")result.textureFilter=2;
            else if(value=="anisotropic")result.textureFilter=3;
            else throw std::invalid_argument("--filter must be authored, bilinear, trilinear or anisotropic");
        } else if(argument=="--aa") {
            const auto value=next().u8string();
            if(value=="off")result.antialiasing=0;
            else if(value=="msaa2")result.antialiasing=2;
            else if(value=="msaa4")result.antialiasing=3;
            else if(value=="msaa8")result.antialiasing=4;
            else if(value=="smaa2")result.antialiasing=5;
            else if(value=="smaa4")result.antialiasing=6;
            else throw std::invalid_argument("--aa must be off, msaa2, msaa4, msaa8, smaa2 or smaa4");
        } else if(argument=="--anisotropy") {
            const auto value=next().u8string();
            if(value=="2" || value=="4" || value=="8" || value=="16")result.anisotropy=std::stoi(value);
            else throw std::invalid_argument("--anisotropy must be 2, 4, 8 or 16");
        } else if(argument=="--skip-movies" || argument=="--play-movies")result.skipMovies=argument=="--skip-movies";
        else if(argument=="--no-configurator")result.skipConfigurator=true;
        else if(argument=="--configure")result.configureOnly=true;
        else if(argument=="--help" || argument=="-h")result.showHelp=true;
        else throw std::invalid_argument("Unknown native application option: "+argument.u8string());
    }
    if(!result.configurationSpecified)result.configuration=result.profileDirectory/NativeTrackOverrideFilename;
    if(result.showHelp)return result;
    if(result.width && *result.width<320)throw std::invalid_argument("--width must be at least 320");
    if(result.height && *result.height<240)throw std::invalid_argument("--height must be at least 240");
    return result;
}

void ResolveNativeGameData(const std::filesystem::path& executable,NativeProcessLaunch& result)
{
    if(!result.dataSource.empty()) {
        const auto& source=result.dataSource;
        if(result.archiveData) {
            if(!OrdinaryFile(source) || !OrdinaryFile(ProcessChild(source.parent_path(),"ZDIR.BIN")))
                throw std::runtime_error("Selected game archive requires an existing data file and adjacent ZDIR.BIN: "+source.u8string());
        } else if(std::filesystem::is_directory(source)) {
            const auto archivePair=[&](const std::filesystem::path& root) {
                return OrdinaryFile(ProcessChild(root,"ZZDATA.BIN")) && OrdinaryFile(ProcessChild(root,"ZDIR.BIN"));
            };
            if(!LooseGameDirectory(source) && !archivePair(source) && !archivePair(ProcessChild(source,"NFSHP2")))
                throw std::runtime_error("Selected game-data directory has no GLOBAL resources or complete ZZDATA.BIN/ZDIR.BIN pair: "+source.u8string());
        } else if(!OrdinaryFile(source)) {
            throw std::runtime_error("Selected game-data source does not exist or is not an ordinary file/directory: "+source.u8string());
        }
        return;
    }
    const auto directories=NativeDataDirectories(executable.parent_path());
    std::filesystem::path incompletePair;
    // Native PC releases use editable, unpacked resources by default. The
    // original archive is a fallback for installations without extraction,
    // or an explicit --zzdata request. Preserve hashed unknown-file lookup
    // through the extracted __archive/ZDIR.BIN sidecar in GameResources.
    if(!result.archiveData)for(const auto& candidate:directories)if(LooseGameDirectory(candidate)) {
        result.dataSource=candidate;return;
    }
    // Archive selection is still available for an archive-only installation.
    for(const auto& candidate:directories) {
        const auto index=ProcessChild(candidate,"ZDIR.BIN");
        const auto data=ProcessChild(candidate,"ZZDATA.BIN");
        if(OrdinaryFile(index) && OrdinaryFile(data)) {
            result.dataSource=data;result.archiveData=true;return;
        }
        if(incompletePair.empty() && (OrdinaryFile(index) || OrdinaryFile(data)))incompletePair=candidate;
    }
    if(!incompletePair.empty())throw std::runtime_error("Incomplete game archive in "+incompletePair.u8string()+
        ": ZDIR.BIN and ZZDATA.BIN must both be present");
    throw std::runtime_error("Game data not found beside "+executable.u8string()+
        ". Put NFSHP2.exe beside ZDIR.BIN and ZZDATA.BIN or in the extracted @ZZDATA directory. "
        "An explicit source may be selected with --zzdata <ZZDATA.BIN> or --data <directory>.");
}
}

NativeProcessLaunch resolveNativeProcessLaunch(const std::filesystem::path& executable,
    const std::filesystem::path& workingDirectory,const std::vector<std::filesystem::path>& arguments)
{
    auto launch=ParseNativeProcessLaunch(executable,workingDirectory,arguments);
    if(!launch.showHelp && !launch.configureOnly)
        ResolveNativeGameData(ResolveArgumentPath(executable,std::filesystem::absolute(workingDirectory)),launch);
    return launch;
}
}

#if defined(_WIN32) && defined(HP2_ENABLE_VULKAN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <dbghelp.h>
#include <shellapi.h>
#include <shobjidl.h>
#include "indep/inc/Flags.hpp"
#include "indep/inc/CarDriver.hpp"
#include "indep/inc/Ecstasy.hpp"
#include "indep/inc/Fred.hpp"
#include "indep/inc/GameFlow.hpp"
#include "bsound/include/bSound.h"
#include "bware/inc/bFile.hpp"
#include "bware/inc/bWare.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <tuple>

namespace hp2 {
namespace {
constexpr DWORD NativeFixedWindowStyle=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;

class NativeSessionLog final {
public:
    ~NativeSessionLog()
    {
        accepting_.store(false,std::memory_order_release);
        if(stop_)SetEvent(stop_);
        if(worker_) {
            if(WaitForSingleObject(worker_,5000)==WAIT_TIMEOUT) {
                abortWrites_.store(true,std::memory_order_release);
                do {(void)CancelSynchronousIo(worker_);}while(WaitForSingleObject(worker_,100)==WAIT_TIMEOUT);
            }
            (void)WaitForSingleObject(worker_,INFINITE);CloseHandle(worker_);
        }
        for(const auto handle:{wake_,stop_})if(handle)CloseHandle(handle);
        if(file_!=INVALID_HANDLE_VALUE)CloseHandle(file_);
    }
    void Open(const std::filesystem::path& directory)
    {
        openedAt_=GetTickCount64();
        current_=directory/L"NFSHP2_log.txt";previous_=directory/L"NFSHP2_log.previous.txt";
        older_=directory/L"NFSHP2_log.previous2.txt";alternative_=directory/L"NFSHP2_log.current.txt";
        queue_.reset(new(std::nothrow) Record[QueueCapacity]);
        if(!queue_) {error_.store(ERROR_NOT_ENOUGH_MEMORY,std::memory_order_relaxed);return;}
        wake_=CreateEventW(nullptr,FALSE,FALSE,nullptr);stop_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!wake_ || !stop_) {error_.store(GetLastError(),std::memory_order_relaxed);return;}
        if(!OpenNextFile())return;
        worker_=CreateThread(nullptr,0,Worker,this,0,nullptr);
        if(!worker_) {error_.store(GetLastError(),std::memory_order_relaxed);return;}
        accepting_.store(true,std::memory_order_release);
        Event("session.start","NFSHP2 native Windows runtime");
        Event("session.file",Name());
        Event("session.log_policy","async; 512 bounded records; 2 MiB per file; current plus two preceding files; no frame events");
    }
    void Event(const char* event,const char* detail=nullptr,bool terminal=false) noexcept
    {
        Record record;record.terminal=terminal;SYSTEMTIME time{};GetSystemTime(&time);
        const auto count=std::snprintf(record.text.data(),record.text.size(),
            "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ pid=%lu tid=%lu session_ms=%llu %s %s\n",
            time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds,
            static_cast<unsigned long>(GetCurrentProcessId()),static_cast<unsigned long>(GetCurrentThreadId()),
            static_cast<unsigned long long>(GetTickCount64()-openedAt_),
            event?event:"event",detail?detail:"");
        if(count<0)return;
        record.size=static_cast<DWORD>(std::min<std::size_t>(static_cast<std::size_t>(count),record.text.size()-1));
        unsigned spaces=0;
        for(DWORD i=0;i<record.size;++i)if(record.text[i]==' ' && ++spaces==4) {record.keyOffset=i+1;break;}
        for(DWORD i=0;i+1<record.size;++i)if(record.text[i]=='\r' || record.text[i]=='\n')record.text[i]=' ';
        if(record.size)record.text[record.size-1]='\n';
        Remember(record.text.data(),record.size);
        if(!accepting_.load(std::memory_order_acquire))return;
        AcquireSRWLockExclusive(&queueLock_);
        if(accepting_.load(std::memory_order_relaxed)) {
            if(count_==QueueCapacity) {
                if(terminal) {terminal_=record;terminalQueued_=true;queued_.store(count_+1,std::memory_order_relaxed);}
                else dropped_.fetch_add(1,std::memory_order_relaxed);
            }
            else {queue_[(head_+count_)%QueueCapacity]=record;++count_;queued_.store(count_+(terminalQueued_?1u:0u),std::memory_order_relaxed);}
        }
        ReleaseSRWLockExclusive(&queueLock_);SetEvent(wake_);
    }
    // Neither this path nor CopyContext acquires the ordinary queue lock.
    void Fatal(const char* reason) noexcept
    {
        if(fatalClaimed_.exchange(true,std::memory_order_acq_rel))return;
        std::snprintf(fatal_.data(),fatal_.size(),"session.fatal %s\n",reason?reason:"unknown");
        fatalPending_.store(true,std::memory_order_release);if(wake_)SetEvent(wake_);
    }
    void CopyContext(char* output,std::size_t capacity) const noexcept
    {
        if(!output || !capacity)return;
        for(unsigned attempt=0;attempt<2;++attempt) {
            const auto& context=context_[publishedContext_.load(std::memory_order_acquire)];
            const auto before=context.version.load(std::memory_order_acquire);
            const auto count=std::min(capacity-1,context.text.size()-1);
            for(std::size_t i=0;i<count;++i)output[i]=context.text[i].load(std::memory_order_relaxed);
            output[count]=0;
            if(!(before&1u) && before==context.version.load(std::memory_order_acquire))return;
        }
        std::snprintf(output,capacity,"concurrent session context update");
    }
    std::uint32_t Pending() const noexcept {return queued_.load(std::memory_order_relaxed);}
    std::uint64_t Dropped() const noexcept {return dropped_.load(std::memory_order_relaxed);}
    DWORD Error() const noexcept {return error_.load(std::memory_order_relaxed);}
    const char* Name() const noexcept {return name_.load(std::memory_order_acquire);}
private:
    static constexpr DWORD MaximumBytes=2u*1024u*1024u;
    static constexpr std::uint32_t QueueCapacity=512;
    struct Record {std::array<char,2048> text{};DWORD size{},keyOffset{};bool terminal{};};
    struct Context {
        Context() noexcept {for(auto& value:text)value.store(0,std::memory_order_relaxed);}
        std::atomic<std::uint32_t> version{0};std::array<std::atomic<char>,1024> text{};
    };
    void Remember(const char* text,DWORD size) noexcept
    {
        if(contextWriting_.test_and_set(std::memory_order_acquire))return;
        const auto next=1u-publishedContext_.load(std::memory_order_relaxed);auto& context=context_[next];
        context.version.fetch_add(1,std::memory_order_acq_rel);
        const auto count=std::min<std::size_t>(size,context.text.size()-1);
        for(std::size_t i=0;i<count;++i)context.text[i].store(text[i],std::memory_order_relaxed);
        for(std::size_t i=count;i<context.text.size();++i)context.text[i].store(0,std::memory_order_relaxed);
        context.version.fetch_add(1,std::memory_order_release);publishedContext_.store(next,std::memory_order_release);
        contextWriting_.clear(std::memory_order_release);
    }
    bool OpenNextFile() noexcept
    {
        if(file_!=INVALID_HANDLE_VALUE) {CloseHandle(file_);file_=INVALID_HANDLE_VALUE;}
        bool retainCurrent=false;
        if(!MoveFileExW(previous_.c_str(),older_.c_str(),MOVEFILE_REPLACE_EXISTING)) {
            const auto error=GetLastError();retainCurrent=error!=ERROR_FILE_NOT_FOUND && error!=ERROR_PATH_NOT_FOUND;
        }
        if(!retainCurrent && !MoveFileExW(current_.c_str(),previous_.c_str(),MOVEFILE_REPLACE_EXISTING)) {
            const auto error=GetLastError();retainCurrent=error!=ERROR_FILE_NOT_FOUND && error!=ERROR_PATH_NOT_FOUND;
        }
        if(retainCurrent && !MoveFileExW(alternative_.c_str(),older_.c_str(),MOVEFILE_REPLACE_EXISTING)) {
            const auto error=GetLastError();
            if(error!=ERROR_FILE_NOT_FOUND && error!=ERROR_PATH_NOT_FOUND) {error_.store(error,std::memory_order_relaxed);return false;}
        }
        const auto& target=retainCurrent?alternative_:current_;
        name_.store(retainCurrent?"NFSHP2_log.current.txt":"NFSHP2_log.txt",std::memory_order_release);
        file_=CreateFileW(target.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file_==INVALID_HANDLE_VALUE) {error_.store(GetLastError(),std::memory_order_relaxed);return false;}
        fileBytes_=0;return true;
    }
    bool Take(Record& record) noexcept
    {
        AcquireSRWLockExclusive(&queueLock_);const bool available=count_!=0 || terminalQueued_;
        if(count_) {record=queue_[head_];head_=(head_+1)%QueueCapacity;--count_;}
        else if(terminalQueued_) {record=terminal_;terminalQueued_=false;}
        queued_.store(count_+(terminalQueued_?1u:0u),std::memory_order_relaxed);
        ReleaseSRWLockExclusive(&queueLock_);return available;
    }
    void Write(const char* text,DWORD bytes,bool terminal=false) noexcept
    {
        if(writeFailed_ || abortWrites_.load(std::memory_order_acquire))return;
        std::array<char,2240> line{};
        const auto count=std::snprintf(line.data(),line.size(),"seq=%llu %.*s",static_cast<unsigned long long>(++writeSequence_),static_cast<int>(bytes),text);
        if(count<=0)return;
        const auto size=static_cast<DWORD>(std::min<std::size_t>(static_cast<std::size_t>(count),line.size()-1));
        if(size>MaximumBytes-fileBytes_ && !OpenNextFile()) {writeFailed_=true;return;}
        DWORD written{};const bool saved=WriteFile(file_,line.data(),size,&written,nullptr)!=FALSE;
        if(!saved || written!=size) {
            error_.store(saved?ERROR_WRITE_FAULT:GetLastError(),std::memory_order_relaxed);writeFailed_=true;return;
        }
        fileBytes_+=written;
        if(terminal && !abortWrites_.load(std::memory_order_acquire) && !FlushFileBuffers(file_))error_.store(GetLastError(),std::memory_order_relaxed);
    }
    static DWORD WINAPI Worker(void* owner) noexcept
    {
        auto& self=*static_cast<NativeSessionLog*>(owner);const HANDLE events[]={self.stop_,self.wake_};
        struct Recent {Record last;std::uint64_t repeats{};bool valid{};};
        std::array<Recent,16> recent{};std::size_t replacement=0;auto lastSummary=GetTickCount64();
        const auto flush=[&](Recent& item) {
            if(!item.repeats)return;
            std::array<char,2144> line{};
            const auto count=std::snprintf(line.data(),line.size(),"%.*s [coalesced_repeats=%llu]\n",
                static_cast<int>(item.last.size?item.last.size-1:0),item.last.text.data(),static_cast<unsigned long long>(item.repeats));
            if(count>0)self.Write(line.data(),static_cast<DWORD>(std::min<std::size_t>(static_cast<std::size_t>(count),line.size()-1)));
            item.repeats=0;
        };
        const auto flushAll=[&]() {for(auto& item:recent)flush(item);};
        const auto process=[&](const Record& record) {
            const auto* key=record.text.data()+record.keyOffset;
            const bool repeatable=!std::strncmp(key,"file.open ",sizeof("file.open ")-1) ||
                !std::strncmp(key,"resource.source ",sizeof("resource.source ")-1);
            if(record.terminal || !repeatable) {
                if(record.terminal)flushAll();
                self.Write(record.text.data(),record.size,record.terminal);return;
            }
            for(auto& item:recent)if(item.valid &&
                !std::strcmp(item.last.text.data()+item.last.keyOffset,record.text.data()+record.keyOffset)) {
                item.last=record;++item.repeats;return;
            }
            auto& item=recent[replacement++%recent.size()];flush(item);item.last=record;item.valid=true;
            self.Write(record.text.data(),record.size);
        };
        std::uint64_t reportedDrops=0;
        for(;;) {
            const bool stopping=WaitForMultipleObjects(2,events,FALSE,self.Pending()?0:5000)==WAIT_OBJECT_0;
            Record record;
            for(unsigned batch=0;batch<128 && self.Take(record);++batch)process(record);
            const auto now=GetTickCount64();if(now-lastSummary>=5000) {flushAll();lastSummary=now;}
            const auto drops=self.dropped_.load(std::memory_order_relaxed);
            if(drops!=reportedDrops) {
                char line[96]{};const auto count=std::snprintf(line,sizeof(line),"session.queue_dropped total=%llu\n",static_cast<unsigned long long>(drops));
                if(count>0)self.Write(line,static_cast<DWORD>(count));
                reportedDrops=drops;
            }
            if(self.fatalPending_.exchange(false,std::memory_order_acq_rel)) {
                flushAll();
                self.Write(self.fatal_.data(),static_cast<DWORD>(std::strlen(self.fatal_.data())),true);
            }
            if(stopping && !self.Pending())break;
        }
        flushAll();
        if(self.file_!=INVALID_HANDLE_VALUE && !self.abortWrites_.load(std::memory_order_acquire))(void)FlushFileBuffers(self.file_);
        return 0;
    }
    HANDLE file_{INVALID_HANDLE_VALUE};HANDLE worker_{},wake_{},stop_{};
    std::filesystem::path current_,previous_,older_,alternative_;
    std::unique_ptr<Record[]> queue_;SRWLOCK queueLock_=SRWLOCK_INIT;
    std::uint32_t head_{},count_{};DWORD fileBytes_{};bool writeFailed_{};Record terminal_;bool terminalQueued_{};
    std::atomic<bool> accepting_{false},fatalClaimed_{false},fatalPending_{false},abortWrites_{false};
    ULONGLONG openedAt_{};std::uint64_t writeSequence_{};
    std::atomic<DWORD> error_{0};std::atomic<std::uint32_t> queued_{0};std::atomic<std::uint64_t> dropped_{0};
    std::array<Context,2> context_{};std::atomic<unsigned> publishedContext_{0};std::atomic_flag contextWriting_=ATOMIC_FLAG_INIT;
    std::array<char,1024> fatal_{};std::atomic<const char*> name_{"NFSHP2_log.txt"};
};


class NativeCrashReporter final {
public:
    NativeCrashReporter() noexcept
    {
        active_=this;previousFilter_=SetUnhandledExceptionFilter(Filter);previousTerminate_=std::set_terminate(Terminate);
        previousAbort_=std::signal(SIGABRT,Abort);nativeFatalSink.store(Terminal,std::memory_order_release);
        ULONG guarantee=64u*1024u;(void)SetThreadStackGuarantee(&guarantee);
        imageBase_=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    }
    ~NativeCrashReporter()
    {
        platform::BindSessionSink(nullptr);sessionActive_.store(nullptr,std::memory_order_release);
        if(stop_)SetEvent(stop_);
        if(worker_) {WaitForSingleObject(worker_,INFINITE);CloseHandle(worker_);}
        nativeFatalSink.store(nullptr,std::memory_order_release);
        SetUnhandledExceptionFilter(previousFilter_);std::set_terminate(previousTerminate_);
        if(previousAbort_!=SIG_ERR)std::signal(SIGABRT,previousAbort_);
        active_=nullptr;
        for(const auto handle:{request_,stop_,complete_})if(handle)CloseHandle(handle);
        if(log_!=INVALID_HANDLE_VALUE)CloseHandle(log_);
        if(dbghelp_)FreeLibrary(dbghelp_);
    }
    void Arm(const std::filesystem::path& directory)
    {
        std::filesystem::create_directories(directory);
        session_.Open(directory);
        sessionActive_.store(&session_,std::memory_order_release);platform::BindSessionSink(SessionEvent);
        textPath_=directory/L"NFSHP2_crash.txt";
        dumpPath_=directory/L"NFSHP2_crash.dmp";
        const auto process=GetCurrentProcessId();
        std::snprintf(pendingTextName_.data(),pendingTextName_.size(),"NFSHP2_crash.%lu.pending.txt",
            static_cast<unsigned long>(process));
        std::snprintf(pendingDumpName_.data(),pendingDumpName_.size(),"NFSHP2_crash.%lu.pending.dmp",
            static_cast<unsigned long>(process));
        pendingTextPath_=directory/pendingTextName_.data();
        pendingDumpPath_=directory/pendingDumpName_.data();
        RemoveEmptyReport(textPath_);RemoveEmptyReport(dumpPath_);
        wchar_t system[MAX_PATH]{};
        const auto length=GetSystemDirectoryW(system,MAX_PATH);
        if(length && length<MAX_PATH)dbghelp_=LoadLibraryW((std::filesystem::path(system)/L"dbghelp.dll").c_str());
        if(dbghelp_) {
            const auto symbol=GetProcAddress(dbghelp_,"MiniDumpWriteDump");
            static_assert(sizeof(symbol)==sizeof(writeDump_));std::memcpy(&writeDump_,&symbol,sizeof(symbol));
        }
        if(!writeDump_)preparationError_=GetLastError()?GetLastError():ERROR_PROC_NOT_FOUND;
        request_=CreateEventW(nullptr,FALSE,FALSE,nullptr);stop_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        complete_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(request_ && stop_ && complete_)worker_=CreateThread(nullptr,0,Worker,this,0,nullptr);
        if(!worker_ && !preparationError_)preparationError_=GetLastError()?GetLastError():ERROR_NOT_ENOUGH_MEMORY;
    }
    void Event(const char* event,const char* detail=nullptr,bool terminal=false) noexcept
    {session_.Event(event,detail,terminal);}
    void ObserveFlow(const frontend::ApplicationFrontendFlow& flow) noexcept
    {
        const std::array<std::int32_t,4> state{{flow.currentState,flow.requestedState,flow.argument,flow.inGameFlag}};
        if(state==lastFlow_)return;
        lastFlow_=state;
        char detail[160]{};
        std::snprintf(detail,sizeof(detail),"current=%d requested=%d argument=%d in_game=%d",
            state[0],state[1],state[2],state[3]);
        Event("game.flow",detail);
    }
private:
    void Text(const char* value) noexcept
    {
        if(log_==INVALID_HANDLE_VALUE || !value)return;
        DWORD size=0;while(size<4096 && value[size])++size;
        for(DWORD offset=0;offset<size;) {
            DWORD written{};
            if(!WriteFile(log_,value+offset,size-offset,&written,nullptr) || !written) {textFailed_=true;return;}
            offset+=written;
        }
    }
    void Number(const char* key,std::uint64_t value,bool hex=false) noexcept
    {
        Text(key);char digits[32]{};std::size_t end=sizeof(digits)-1,index=end;
        const std::uint64_t base=hex?16:10;
        do {digits[--index]="0123456789abcdef"[value%base];value/=base;}while(value && index);
        if(hex)Text("0x");
        Text(digits+index);Text("\n");
    }
    void Report(EXCEPTION_POINTERS* pointers,const char* reason,const char* exceptionMessage=nullptr) noexcept
    {
        if(InterlockedCompareExchange(&reporting_,1,0)!=0)return;
        thread_=GetCurrentThreadId();windowsException_=pointers!=nullptr;
        CopyMessage(reason_,reason);CopyMessage(message_,exceptionMessage);
        if(pointers && pointers->ExceptionRecord) {exception_=*pointers->ExceptionRecord;exception_.ExceptionRecord=nullptr;}
        if(pointers && pointers->ContextRecord)context_=*pointers->ContextRecord;
        else {RtlCaptureContext(&context_);exception_.ExceptionCode=0xe0000071u;exception_.ExceptionAddress=reinterpret_cast<void*>(context_.Rip);}
        exceptionPointers_={&exception_,&context_};
        if(worker_) {SetEvent(request_);(void)WaitForSingleObject(complete_,5000);}
        else WriteReport(false);
    }
    static void RemoveEmptyReport(const std::filesystem::path& path) noexcept
    {
        const auto file=CreateFileW(path.c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return;
        LARGE_INTEGER size{};
        if(GetFileSizeEx(file,&size) && size.QuadPart==0) {
            FILE_DISPOSITION_INFO disposition{TRUE};
            (void)SetFileInformationByHandle(file,FileDispositionInfo,&disposition,sizeof(disposition));
        }
        CloseHandle(file);
    }
    template<std::size_t N>static void CopyMessage(std::array<char,N>& output,const char* value) noexcept
    {
        std::size_t count{};
        if(value)while(count+1<N && value[count]) {output[count]=value[count];++count;}
        output[count]=0;
    }
    static bool ValidDump(HANDLE file) noexcept
    {
        LARGE_INTEGER size{},zero{};std::uint32_t header[8]{};DWORD read{};
        if(!GetFileSizeEx(file,&size) || size.QuadPart<32 || !SetFilePointerEx(file,zero,nullptr,FILE_BEGIN) ||
            !ReadFile(file,header,sizeof(header),&read,nullptr) || read!=sizeof(header))return false;
        // Windows MINIDUMP_SIGNATURE is the little-endian on-disk "MDMP" word.
        // Spell its value without the SDK's implementation-defined multi-character literal.
        return header[0]==0x504D444Du && header[2]>0 &&
            static_cast<std::uint64_t>(header[3])+static_cast<std::uint64_t>(header[2])*12u<=static_cast<std::uint64_t>(size.QuadPart);
    }
    void SaveDump(bool available) noexcept
    {
        if(!available || !writeDump_) {
            Number("minidump.completed=",0);Number("minidump.error=",preparationError_?preparationError_:ERROR_PROC_NOT_FOUND,true);return;
        }
        const auto file=CreateFileW(pendingDumpPath_.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE) {
            const auto error=GetLastError();Number("minidump.completed=",0);Number("minidump.error=",error,true);return;
        }
        MINIDUMP_EXCEPTION_INFORMATION info{thread_,&exceptionPointers_,FALSE};
        const bool written=writeDump_(GetCurrentProcess(),GetCurrentProcessId(),file,MiniDumpNormal,&info,nullptr,nullptr)!=FALSE;
        DWORD error=written?ERROR_SUCCESS:GetLastError();
        bool valid=written && ValidDump(file);
        if(written && !valid)error=ERROR_INVALID_DATA;
        if(valid && !FlushFileBuffers(file)) {error=GetLastError();valid=false;}
        CloseHandle(file);
        bool published=false;
        if(valid) {
            published=MoveFileExW(pendingDumpPath_.c_str(),dumpPath_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
            if(!published)error=GetLastError();
        } else (void)DeleteFileW(pendingDumpPath_.c_str());
        Number("minidump.completed=",published);Number("minidump.valid=",valid);
        if(!published)Number("minidump.error=",error,true);
        if(valid && !published) {Text("minidump.pending_file=");Text(pendingDumpName_.data());Text("\n");}
    }
    void WriteReport(bool dumpAvailable) noexcept
    {
        log_=CreateFileW(pendingTextPath_.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        const auto* reason=reason_.data();const auto* exceptionMessage=message_[0]?message_.data():nullptr;
        Text("NFSHP2 fatal native fault\nreason=");Text(reason);Text("\n");
        if(exceptionMessage) {Text("exception.message=");Text(exceptionMessage);Text("\n");}
        Text(windowsException_?"exception.origin=windows_exception\n":"exception.origin=captured_terminal_state\n");
        Number("exception.code=",exception_.ExceptionCode,true);
        Number("exception.address=",reinterpret_cast<std::uintptr_t>(exception_.ExceptionAddress),true);
        Number("process.image_base=",imageBase_,true);Number("thread.id=",thread_);
        Text("session_log.file=");Text(session_.Name());Text("\n");
        Number("session_log.error=",session_.Error(),true);
        Number("session_log.pending=",session_.Pending());Number("session_log.dropped=",session_.Dropped());
        std::array<char,1024> context{};session_.CopyContext(context.data(),context.size());
        Text("session_log.last_context=");Text(context.data());Text("\n");
        for(DWORD i=0;i<std::min<DWORD>(exception_.NumberParameters,EXCEPTION_MAXIMUM_PARAMETERS);++i) {
            Number("exception.parameter_index=",i);Number("exception.parameter=",exception_.ExceptionInformation[i],true);
        }
        SaveDump(dumpAvailable);
        if(log_!=INVALID_HANDLE_VALUE) {
            if(!FlushFileBuffers(log_))textFailed_=true;
            CloseHandle(log_);log_=INVALID_HANDLE_VALUE;
            if(!textFailed_)(void)MoveFileExW(pendingTextPath_.c_str(),textPath_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
            else (void)DeleteFileW(pendingTextPath_.c_str());
        }
        // Crash text/dump publication has priority over the ordinary log:
        // a slow or editor-held session file cannot delay their creation.
        session_.Fatal(reason_.data());
    }
    static LONG WINAPI Filter(EXCEPTION_POINTERS* pointers) noexcept
    {
        if(active_)active_->Report(pointers,"unhandled_windows_exception");
        return EXCEPTION_EXECUTE_HANDLER;
    }
    static void Terminal(const char* reason) noexcept
    {if(active_)active_->Report(nullptr,reason);}
    [[noreturn]] static void Abort(int) noexcept
    {
        Terminal("C_runtime_abort");
        TerminateProcess(GetCurrentProcess(),0xe0000072u);std::_Exit(134);
    }
    [[noreturn]] static void Terminate() noexcept
    {
        // A failed checked native owner can reach terminate through a noexcept
        // destructor. Preserve the original exception text while its object is
        // still alive; the captured handler RIP alone cannot identify the guard.
        if(active_) {
            if(const auto exception=std::current_exception()) {
                try {std::rethrow_exception(exception);}
                catch(const std::exception& error) {active_->Report(nullptr,"std_terminate",error.what());}
                catch(...) {active_->Report(nullptr,"std_terminate","non-std exception");}
            } else active_->Report(nullptr,"std_terminate");
        }
        TerminateProcess(GetCurrentProcess(),0xe0000071u);std::_Exit(113);
    }
    static DWORD WINAPI Worker(void* owner) noexcept
    {
        auto& self=*static_cast<NativeCrashReporter*>(owner);const HANDLE events[]={self.stop_,self.request_};
        if(WaitForMultipleObjects(2,events,FALSE,INFINITE)!=WAIT_OBJECT_0+1)return 0;
        self.WriteReport(true);SetEvent(self.complete_);return 0;
    }
    static void SessionEvent(const char* event,const char* detail) noexcept
    {if(auto* log=sessionActive_.load(std::memory_order_acquire))log->Event(event,detail);}
    inline static std::atomic<NativeSessionLog*> sessionActive_{nullptr};
    inline static NativeCrashReporter* active_{};
    LPTOP_LEVEL_EXCEPTION_FILTER previousFilter_{};std::terminate_handler previousTerminate_{};
    using SignalHandler=void(*)(int);
    SignalHandler previousAbort_{SIG_DFL};
    HANDLE log_{INVALID_HANDLE_VALUE},request_{},stop_{},complete_{},worker_{};
    HMODULE dbghelp_{};decltype(&MiniDumpWriteDump) writeDump_{};
    volatile LONG reporting_{};DWORD thread_{};std::uintptr_t imageBase_{};
    EXCEPTION_RECORD exception_{};CONTEXT context_{};EXCEPTION_POINTERS exceptionPointers_{};
    std::filesystem::path textPath_,dumpPath_,pendingTextPath_,pendingDumpPath_;
    std::array<char,64> pendingTextName_{},pendingDumpName_{};
    std::array<char,1024> reason_{},message_{};
    DWORD preparationError_{};bool windowsException_{},textFailed_{};
    NativeSessionLog session_;
    std::array<std::int32_t,4> lastFlow_{{INT32_MIN,INT32_MIN,INT32_MIN,INT32_MIN}};
};

std::wstring NativeWideText(const std::string& value)
{
    if(value.empty())return {};
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
    if(count<=0)return L"The message contains invalid UTF-8. See the text log.";
    std::wstring result(static_cast<std::size_t>(count),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),count);
    return result;
}

struct NativeWindowsMonitor {
    std::string id,displayName;
    std::wstring name;
    HMONITOR handle{};
    RECT display{},work{};
    bool primary{};
};
std::vector<NativeWindowsMonitor> NativeWindowsMonitors()
{
    struct Enumeration {std::vector<NativeWindowsMonitor> monitors;std::exception_ptr failure;};
    Enumeration result;
    const auto callback=[](HMONITOR handle,HDC,LPRECT,LPARAM context)->BOOL {
        auto& output=*reinterpret_cast<Enumeration*>(context);
        try {
            MONITORINFOEXW info{};info.cbSize=sizeof(info);
            if(!GetMonitorInfoW(handle,&info))throw std::runtime_error("Could not read Windows monitor information");
            NativeWindowsMonitor item;item.handle=handle;item.display=info.rcMonitor;item.work=info.rcWork;
            item.primary=(info.dwFlags&MONITORINFOF_PRIMARY)!=0;
            item.displayName=std::filesystem::path(info.szDevice).u8string();
            DISPLAY_DEVICEW display{};display.cb=sizeof(display);
            const bool found=EnumDisplayDevicesW(info.szDevice,0,&display,EDD_GET_DEVICE_INTERFACE_NAME)!=FALSE;
            item.id=found && display.DeviceID[0] ? std::filesystem::path(display.DeviceID).u8string():item.displayName;
            item.name=info.szDevice;item.name+=L" - ";item.name+=found && display.DeviceString[0]?display.DeviceString:L"Monitor";
            item.name+=L" ("+std::to_wstring(info.rcMonitor.right-info.rcMonitor.left)+L" x "+
                std::to_wstring(info.rcMonitor.bottom-info.rcMonitor.top)+L")";
            if(item.primary)item.name+=L" [Primary]";
            output.monitors.push_back(std::move(item));return TRUE;
        } catch(...) {output.failure=std::current_exception();return FALSE;}
    };
    const auto enumerated=EnumDisplayMonitors(nullptr,nullptr,callback,reinterpret_cast<LPARAM>(&result));
    if(result.failure)std::rethrow_exception(result.failure);
    Require(enumerated!=FALSE && !result.monitors.empty(),"No active Windows monitor is available");
    std::stable_sort(result.monitors.begin(),result.monitors.end(),[](const auto& left,const auto& right) {
        if(left.primary!=right.primary)return left.primary;
        return left.displayName<right.displayName;
    });
    return result.monitors;
}
bool NativeMonitorMatches(const NativeWindowsMonitor& monitor,const std::string& id)
{return monitor.id==id || monitor.displayName==id;}

NativeWindowsMonitor NativeChooseMonitor(const std::string& id)
{
    auto monitors=NativeWindowsMonitors();
    const auto found=std::find_if(monitors.begin(),monitors.end(),[&](const auto& item){return NativeMonitorMatches(item,id);});
    return found==monitors.end()?monitors.front():*found;
}

std::pair<DWORD,DWORD> NativeWindowsMonitorResolution(const NativeWindowsMonitor& monitor)
{
    const auto device=NativeWideText(monitor.displayName);
    const auto id=NativeWideText(monitor.id);
    const std::pair<DWORD,DWORD> desktop{static_cast<DWORD>(monitor.display.right-monitor.display.left),
        static_cast<DWORD>(monitor.display.bottom-monitor.display.top)};
    // GDI lists source modes, including modes scaled by the GPU. Resolve the
    // active output and query the monitor's preferred target mode separately.
    for(int attempt=0;attempt<3;++attempt) {
        UINT32 pathCount{},modeCount{};
        if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&pathCount,&modeCount)!=ERROR_SUCCESS)break;
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        const auto result=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&pathCount,paths.data(),&modeCount,modes.data(),nullptr);
        if(result==ERROR_INSUFFICIENT_BUFFER)continue;
        if(result!=ERROR_SUCCESS)break;
        std::optional<std::pair<DWORD,DWORD>> linkedOutput;
        for(UINT32 index=0;index<pathCount;++index) {
            const auto& path=paths[index];
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
            source.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof(source),path.sourceInfo.adapterId,path.sourceInfo.id};
            if(DisplayConfigGetDeviceInfo(&source.header)!=ERROR_SUCCESS ||
                _wcsicmp(device.c_str(),source.viewGdiDeviceName)!=0)continue;
            DISPLAYCONFIG_TARGET_PREFERRED_MODE preferred{};
            preferred.header={DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE,sizeof(preferred),path.targetInfo.adapterId,path.targetInfo.id};
            if(DisplayConfigGetDeviceInfo(&preferred.header)!=ERROR_SUCCESS || !preferred.width || !preferred.height)continue;
            std::pair<DWORD,DWORD> resolution{preferred.width,preferred.height};
            const auto rotation=path.targetInfo.rotation;
            if(rotation==DISPLAYCONFIG_ROTATION_ROTATE90 || rotation==DISPLAYCONFIG_ROTATION_ROTATE270)
                std::swap(resolution.first,resolution.second);
            DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
            target.header={DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME,sizeof(target),path.targetInfo.adapterId,path.targetInfo.id};
            if(DisplayConfigGetDeviceInfo(&target.header)==ERROR_SUCCESS &&
                _wcsicmp(id.c_str(),target.monitorDevicePath)==0)return resolution;
            if(!linkedOutput)linkedOutput=resolution;
            else {
                linkedOutput->first=std::min(linkedOutput->first,resolution.first);
                linkedOutput->second=std::min(linkedOutput->second,resolution.second);
            }
        }
        return linkedOutput.value_or(desktop);
    }
    return desktop;
}

bool NativeWindowsModeFits(const DEVMODEW& mode,std::pair<DWORD,DWORD> extent) noexcept
{
    return mode.dmBitsPerPel>=32 && mode.dmPelsWidth>=320 && mode.dmPelsHeight>=240 &&
        mode.dmPelsWidth<=std::min<DWORD>(16384,extent.first) && mode.dmPelsHeight<=std::min<DWORD>(16384,extent.second);
}

std::vector<DEVMODEW> NativeWindowsDisplayModes(const NativeWindowsMonitor& monitor,std::pair<DWORD,DWORD> extent)
{
    const auto device=NativeWideText(monitor.displayName);
    std::vector<DEVMODEW> modes;
    for(DWORD index=0;;++index) {
        DEVMODEW mode{};mode.dmSize=sizeof(mode);
        if(!EnumDisplaySettingsExW(device.c_str(),index,&mode,0))break;
        if(NativeWindowsModeFits(mode,extent)) {
            mode.dmFields=DM_PELSWIDTH|DM_PELSHEIGHT|DM_BITSPERPEL|DM_DISPLAYFLAGS|DM_DISPLAYFREQUENCY;
            modes.push_back(mode);
        }
    }
    return modes;
}
std::vector<std::pair<int,int>> NativeWindowsResolutions(const NativeWindowsMonitor& monitor)
{
    const auto device=NativeWideText(monitor.displayName);
    const auto extent=NativeWindowsMonitorResolution(monitor);
    std::vector<std::pair<int,int>> sizes;
    // The mode already in use is valid even when the desktop session
    // cannot submit a trial mode change (for example via a remote display).
    DEVMODEW current{};current.dmSize=sizeof(current);
    if(EnumDisplaySettingsExW(device.c_str(),ENUM_CURRENT_SETTINGS,&current,0) &&
        NativeWindowsModeFits(current,extent))
        sizes.emplace_back(static_cast<int>(current.dmPelsWidth),static_cast<int>(current.dmPelsHeight));
    for(auto mode:NativeWindowsDisplayModes(monitor,extent)) {
        const auto size=std::make_pair(static_cast<int>(mode.dmPelsWidth),static_cast<int>(mode.dmPelsHeight));
        if(std::find(sizes.begin(),sizes.end(),size)==sizes.end() &&
            ChangeDisplaySettingsExW(device.c_str(),&mode,nullptr,CDS_TEST,nullptr)==DISP_CHANGE_SUCCESSFUL)
            sizes.push_back(size);
    }
    std::sort(sizes.begin(),sizes.end());
    return sizes;
}

// Module-owned LR_SHARED icons are reused by the launcher and game window.
// The same ICON group is embedded in the executable for Explorer/Task Manager.
void NativeSetApplicationIcons(WNDCLASSEXW& windowClass)
{
    windowClass.hIcon=static_cast<HICON>(LoadImageW(windowClass.hInstance,
        MAKEINTRESOURCEW(IDI_NFSHP2_APPLICATION),IMAGE_ICON,
        GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),LR_SHARED));
    windowClass.hIconSm=static_cast<HICON>(LoadImageW(windowClass.hInstance,
        MAKEINTRESOURCEW(IDI_NFSHP2_APPLICATION),IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED));
}

// Native launcher belongs to the proven Platform.cpp host. The game still
// receives the source pad scanner, FRED settings and original Config cells.
#include "NativeConfigurator.inl"

namespace {
struct WindowsMenuPointer {
    struct Sample {
        POINT point;
        bool pressed{},held{},released{};
    };
    std::optional<POINT> press,release,motion;
    POINT dragPoint{};
    bool captured{};

    void Press(POINT point,bool capture) noexcept
    {
        press=point;release.reset();motion.reset();
        dragPoint=point;captured=capture;
    }
    void Move(POINT point) noexcept
    {
        motion=point;
        if(captured)dragPoint=point;
    }
    void Release(POINT point) noexcept
    {
        if(!captured)return;
        captured=false;release=point;motion.reset();
    }
    std::optional<Sample> Take() noexcept
    {
        if(const auto point=std::exchange(press,std::nullopt)) {
            if(release && release->x==point->x && release->y==point->y)release.reset();
            return Sample{*point,true,captured || release.has_value(),false};
        }
        if(const auto point=std::exchange(release,std::nullopt))return Sample{*point,false,false,true};
        if(const auto point=std::exchange(motion,std::nullopt))return Sample{*point,false,captured,false};
        return std::nullopt;
    }
};
}

class WindowsApplicationHost final:public NativeApplicationHost {
    NativeCrashReporter& session_;
public:
    WindowsApplicationHost(const NativePcSettings& settings,NativeCrashReporter& session,const std::filesystem::path& pcFile,
        const std::filesystem::path& dataDirectory)
        :session_(session),width_(static_cast<std::uint32_t>(settings.width)),height_(static_cast<std::uint32_t>(settings.height)),
        monitorDevice_(settings.monitorDevice),pcSettings_(settings),pcFile_(pcFile)
    {
        instance_=GetModuleHandleW(nullptr);
        (void)SetProcessDPIAware();
        WNDCLASSEXW windowClass{};windowClass.cbSize=sizeof(windowClass);
        windowClass.lpfnWndProc=WindowProcedure;windowClass.hInstance=instance_;
        windowClass.hCursor=LoadCursorW(nullptr,IDC_ARROW);windowClass.lpszClassName=L"NFSHP2NativePAL";
        NativeSetApplicationIcons(windowClass);
        class_=RegisterClassExW(&windowClass);
        Require(class_!=0,"Could not register the NFSHP2 native window class");
        RECT rectangle{0,0,settings.width,settings.height};AdjustWindowRectEx(&rectangle,NativeFixedWindowStyle,FALSE,0);
        window_=CreateWindowExW(0,windowClass.lpszClassName,L"Need for Speed Hot Pursuit 2",
            NativeFixedWindowStyle,CW_USEDEFAULT,CW_USEDEFAULT,rectangle.right-rectangle.left,rectangle.bottom-rectangle.top,
            nullptr,nullptr,instance_,this);
        if(!window_) {UnregisterClassW(windowClass.lpszClassName,instance_);class_=0;throw std::runtime_error("Could not create the NFSHP2 native window");}
        try {
            const auto executableDirectory=nativeExecutablePath().parent_path();
            for(const auto& directory:{executableDirectory,dataDirectory}) {
                for(const auto* name:{L"cursor.cur",L"cursor.ani"}) {
                    fileCursor_=LoadCursorFromFileW((directory/L"data"/L"ui"/name).c_str());
                    if(fileCursor_)break;
                }
                if(fileCursor_)break;
            }
            menuCursor_=fileCursor_?fileCursor_:static_cast<HCURSOR>(LoadImageW(instance_,
                MAKEINTRESOURCEW(IDC_NFSHP2_MENU),IMAGE_CURSOR,0,0,LR_DEFAULTSIZE|LR_SHARED));
            Require(menuCursor_,"Could not load the native menu cursor");
            presentationLimit_.SetLimit(settings.frameLimit);
            presentationLimit_.Reset(FrameTime());
            constexpr DWORD highResolutionTimer=0x00000002u;
            frameTimer_=CreateWaitableTimerExW(nullptr,nullptr,highResolutionTimer,TIMER_MODIFY_STATE|SYNCHRONIZE);
            if(!frameTimer_)frameTimer_=CreateWaitableTimerW(nullptr,FALSE,nullptr);
            ApplyDisplaySettings(settings.width,settings.height,settings.fullscreen,settings.vsync);
            input_=std::make_unique<WindowsInputDevice>(window_,settings.input);
            RefreshClientSize();
            graphics_=CreateGraphics(settings);
            LARGE_INTEGER frequency{};
            Require(QueryPerformanceFrequency(&frequency)!=FALSE && frequency.QuadPart>0,
                "Windows display QPC frequency is unavailable");
            qpcFrequency_=static_cast<std::uint64_t>(frequency.QuadPart);
            ShowWindow(window_,SW_SHOW);UpdateWindow(window_);
        } catch(...) {Close();throw;}
    }
    ~WindowsApplicationHost() override {Close();}
    void RecordSessionEvent(const char* event,const char* detail) noexcept override
    {session_.Event(event,detail);}
    void BindPresentationClock(const GameTime& clock)
    {
        Require(!presentationClock_,"Windows display clock is already bound");
        presentationClock_=&clock;PublishRefreshTiming();
    }
    void BindFullscreenToggle(std::function<void()> action)
    {
        fullscreenToggle_=std::move(action);
    }

    bool PollEvents() override
    {
        MSG message{};
        bool wasMinimized=false;
        for(;;) {
            while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                if(message.message==WM_QUIT)return false;
                if(message.hwnd==window_ && HandleFullscreenShortcut(message.message,message.wParam,message.lParam))continue;
                TranslateMessage(&message);
                dispatchInputTime_=input_?input_->MessageTime(message.time):FrameTime();
                dispatchInputMessage_=&message;DispatchMessageW(&message);dispatchInputMessage_=nullptr;
            }
            if(!IsWindow(window_))return false;
            if(!IsIconic(window_))break;
            wasMinimized=true;
            if(!WaitMessage())throw std::runtime_error("Win32 WaitMessage failed while minimized");
        }
        if(wasMinimized) {++presentationTimingEpoch_;presentationLimit_.Reset(FrameTime());}
        if((GetAsyncKeyState(VK_RETURN)&0x8000)==0)fullscreenShortcutKeys_.fill(false);
        input_->Poll(fullscreenShortcutKeys_[0] || fullscreenShortcutKeys_[1]);
        RefreshMenuCursor();
        RefreshClientSize();
        if(displayChanged_ && appliedDisplayRequest_) {
            const auto request=*appliedDisplayRequest_;appliedDisplayRequest_.reset();displayChanged_=false;
            ApplyDisplaySettings(request[0],request[1],request[2]!=0,request[3]!=0);
        }
        if(fullscreenTogglePending_ && fullscreenToggle_) {
            fullscreenTogglePending_=false;
            fullscreenToggle_();
        }
        if(graphics_)graphics_->CollectCompletedSunDepth();
        PublishRefreshTiming();
        return true;
    }
    std::uint64_t PresentationTimingEpoch() const noexcept override {return presentationTimingEpoch_;}
    bool PresentationDue(NativeFrameTiming::Clock::time_point now) const noexcept override
    {return presentationLimit_.Due(now);}
    NativeFrameTiming::Clock::time_point PresentationDeadline() const noexcept override
    {return presentationLimit_.Deadline();}
    void WaitUntil(NativeFrameTiming::Clock::time_point deadline) override
    {
        const auto remaining=std::chrono::duration_cast<std::chrono::nanoseconds>(deadline-FrameTime()).count();
        if(remaining<=0)return;
        if(frameTimer_) {
            LARGE_INTEGER due{};due.QuadPart=-std::max<LONGLONG>(1,(remaining+99)/100);
            Require(SetWaitableTimer(frameTimer_,&due,0,nullptr,nullptr,FALSE)!=FALSE,"Could not arm native frame timer");
            const auto result=MsgWaitForMultipleObjectsEx(1,&frameTimer_,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
            (void)CancelWaitableTimer(frameTimer_);
            Require(result!=WAIT_FAILED,"Native frame timer wait failed");
        } else {
            const auto milliseconds=static_cast<DWORD>(std::min<std::int64_t>((remaining+999999)/1000000,1000));
            Require(MsgWaitForMultipleObjectsEx(0,nullptr,milliseconds,QS_ALLINPUT,MWMO_INPUTAVAILABLE)!=WAIT_FAILED,
                "Native frame message wait failed");
        }
    }
    void BeginInputStep(NativeFrameTiming::Clock::time_point boundary) override
    {input_->BeginStep(boundary);}
    int32_t ReadInput(int32_t port,JoystickControls& destination) override
    {return input_->ReadInput(port,destination);}
    void SetMenuInput(bool menu,bool pause,bool split) noexcept override
    {input_->SetMenuInput(menu,pause,split);}
    void SetReplayInput(bool enabled) noexcept override
    {replayInput_=enabled;input_->SetReplayInput(enabled);}
    bool MenuPointerHeld() const noexcept override
    {return replayInput_?(GetKeyState(VK_LBUTTON)&0x8000)!=0:menuPointerHeld_;}
    void SetMenuPointer(bool enabled) noexcept override
    {
        if(menuPointerEnabled_==enabled)return;
        menuPointerEnabled_=enabled;
        if(!enabled) {
            if(menuPointer_.captured && GetCapture()==window_)ReleaseCapture();
            menuPointer_={};menuPointerHeld_=false;
            pointerMoved_=false;pointerClick_=false;wheelDelta_=0;lastMouseUse_=0;
        }
        RefreshMenuCursor();
    }
    std::pair<int,int> MenuMonitorSize() const noexcept override
    {
        MONITORINFO monitor{};monitor.cbSize=sizeof(monitor);
        if(!GetMonitorInfoW(MonitorFromWindow(window_,MONITOR_DEFAULTTONEAREST),&monitor))return {16384,16384};
        return {monitor.rcMonitor.right-monitor.rcMonitor.left,monitor.rcMonitor.bottom-monitor.rcMonitor.top};
    }
    bool ReadMenuPointer(float& x,float& y,bool& click,int& wheel) noexcept override
    {
        wheel=std::exchange(wheelDelta_,0)/WHEEL_DELTA;
        if(!menuPointerEnabled_ || !clientWidth_ || !clientHeight_)return false;
        const auto sample=replayInput_?std::nullopt:menuPointer_.Take();
        const bool released=sample && sample->released;
        if(GetForegroundWindow()!=window_ && !released)return false;
        POINT point{};
        if(sample) {
            point=sample->point;click=sample->pressed;menuPointerHeld_=sample->held;
        } else {
            click=replayInput_ && std::exchange(pointerClick_,false);
            const bool moved=replayInput_ && std::exchange(pointerMoved_,false);
            if(!moved && !click && !wheel && !(replayInput_ && MenuPointerHeld()))return false;
            if(!GetCursorPos(&point) || !ScreenToClient(window_,&point))return false;
        }
        const auto width=static_cast<float>(clientWidth_),height=static_cast<float>(clientHeight_);
        const auto [canvasWidth,canvasHeight]=LogicalCanvas();
        const float logicalWidth=static_cast<float>(canvasWidth);
        const float logicalHeight=static_cast<float>(canvasHeight);
        const float scale=std::min(width/logicalWidth,height/logicalHeight);
        const std::array<float,4> presentation{{(width-logicalWidth*scale)*0.5f,(height-logicalHeight*scale)*0.5f,
            logicalWidth*scale,logicalHeight*scale}};
        const auto viewport=racePresentation_?presentation:
            ecstasyNativeUiViewport(EcstasyUiPlacement::CenteredCanvas,presentation);
        const bool outside=point.x<viewport[0] || point.y<viewport[1] ||
            point.x>=viewport[0]+viewport[2] || point.y>=viewport[1]+viewport[3];
        if(outside && (click || replayInput_ || !(menuPointerHeld_ || released)))return false;
        x=std::clamp((static_cast<float>(point.x)-viewport[0])*640.0f/viewport[2],0.0f,640.0f);
        y=std::clamp((static_cast<float>(point.y)-viewport[1])*480.0f/viewport[3],0.0f,480.0f);
        input_->UseKeyboardPrompts();
        return true;
    }
    platform::NativePromptDevice PromptDevice() const noexcept override {return input_->PromptDevice();}
    platform::NativePromptDevice GamepadPromptDevice(std::int32_t port) const noexcept override
    {return input_->GamepadPromptDevice(port);}
    const NativePcSettings* PcSettings() const noexcept override {return &pcSettings_;}
    void PreviewDualSenseVolume(std::int32_t port,std::optional<std::int32_t> volume) noexcept override
    {input_->PreviewDualSenseVolume(port,volume);}
    void PreviewMasterVolume(std::optional<std::int32_t> gain) noexcept override {previewMasterVolume_=gain;}
    bool SavePcSettings(const NativePcSettings& settings,std::string& error) override
    {
        const auto previous=pcSettings_;
        const bool rebuild=previous.antialiasing!=settings.antialiasing ||
            previous.textureFilter!=settings.textureFilter || previous.anisotropy!=settings.anisotropy;
        const bool display=previous.width!=settings.width || previous.height!=settings.height ||
            previous.fullscreenSize!=settings.fullscreenSize ||
            previous.fullscreen!=settings.fullscreen || previous.vsync!=settings.vsync;
        const bool resize=previous.raceAspect!=settings.raceAspect || previous.renderScale!=settings.renderScale;
        try {validateNativePcSettings(settings);}
        catch(const std::exception& failure) {error=failure.what();return false;}
        const auto apply=[&](const NativePcSettings& requested) {
            pcSettings_=requested;
            if(rebuild)graphics_.reset();
            if(display)ApplyDisplaySettings(requested.width,requested.height,requested.fullscreen,requested.vsync);
            if(rebuild)graphics_=CreateGraphics(requested);
            else if(resize)ResizeRenderTargets();
            input_->SetSettings(requested.input);
            presentationLimit_.SetLimit(requested.frameLimit);
            graphics_->SetSharpScaling(requested.sharpScaling);
            configureNativeRenderSettings(requested.render);
            applicationShowFrameRate=requested.showFps;
        };
        try {
            apply(settings);
            saveNativePcSettings(pcFile_,settings);
            return true;
        } catch(const std::exception& failure) {
            error=failure.what();
            apply(previous);
            return false;
        }
    }
    bool HasDualSense(std::int32_t port) const noexcept override {return input_->HasDualSense(port);}
    void SetDrivingTriggers(std::int32_t port,std::uint32_t throttle,std::uint32_t brake) noexcept override
    {input_->SetDrivingTriggers(port,throttle,brake);}
    std::string InputDeviceName(std::int32_t port) const override {return input_->DeviceName(port);}
    void BeginBindingCapture(std::int32_t port) noexcept override {input_->BeginBindingCapture(port);}
    std::uint16_t CapturedKey() const noexcept override {return input_->CapturedKey();}
    void SetMenuTextInput(bool enabled) noexcept override {menuTextInput_=enabled;menuTextLength_=0;}
    std::string TakeMenuTextInput() override
    {
        const auto length=std::exchange(menuTextLength_,std::size_t{});
        return std::string(menuText_.data(),length);
    }
    void SetRumble(int32_t port,uint8_t smallMotor,uint8_t large) override {input_->SetRumble(port,smallMotor,large);}
    void SubmitFrame(EcstasyFrame& frame) override
    {
        const auto started=FrameTime();
        graphics_->Render(frame);
        presentationLimit_.Presented(started);
        PublishRefreshTiming();
    }
    bool TrySubmitPreparedFrame() override
    {
        if(!graphics_ || !graphics_->CanRenderPrepared())return false;
        const auto started=FrameTime();
        if(!graphics_->RenderPrepared())return false;
        presentationLimit_.Presented(started);
        PublishRefreshTiming();
        return true;
    }
    void InvalidatePreparedFrame() noexcept override
    {
        if(graphics_)graphics_->InvalidatePrepared();
    }
    bool ReadRenderStatistics(EcstasyVulkanFrameStatistics& result) const noexcept override
    {
        if(!graphics_)return false;
        result=graphics_->LastFrameStatistics();return true;
    }
    WorldAudioResult OpenAudio(bSound& sound) override
    {
        const auto result=sound.OpenWindowsDevice();char detail[128]{};
        std::snprintf(detail,sizeof(detail),"operation=open result=%d route=%s",static_cast<int>(result),
            hp2::audio::NativeAudioIsWine()?"wine_native_pcm":"windows_xapo");
        RecordSessionEvent("audio.device",detail);
        return result;
    }
    WorldAudioResult PumpAudio(bSound& sound) override
    {
        std::int32_t volume=100;
        const auto controller=input_->DualSenseSpeakerContainer(volume);
        sound.SetWindowsRadioController(controller,static_cast<float>(volume)/100.0f);
        auto result=sound.PumpWindowsDevice();
        const auto gain=previewMasterVolume_.value_or(pcSettings_.masterVolume);
        if(result==WORLD_AUDIO_OK && actualMasterVolume_!=gain) {
            result=sound.SetWindowsMasterVolume(static_cast<float>(gain)/100.0f);
            if(result==WORLD_AUDIO_OK)actualMasterVolume_=gain;
        }
        if(result!=audioPumpResult_) {
            audioPumpResult_=result;char detail[128]{};
            std::snprintf(detail,sizeof(detail),"operation=pump result=%d",static_cast<int>(result));RecordSessionEvent("audio.device",detail);
        }
        return result;
    }
    WorldAudioResult CloseAudio(bSound& sound) override
    {
        const auto result=sound.CloseWindowsDevice();char detail[128]{};
        std::snprintf(detail,sizeof(detail),"operation=close result=%d",static_cast<int>(result));RecordSessionEvent("audio.device",detail);
        return result;
    }
    int Width() const override {return LogicalCanvas().first;}
    int Height() const override {return LogicalCanvas().second;}
    std::vector<std::pair<int,int>> MenuDisplayModes() const override
    {return NativeWindowsResolutions(NativeChooseMonitor(monitorDevice_));}
    void SetRacePresentation(bool racing) override
    {
        if(racePresentation_==racing)return;
        racePresentation_=racing;
        ResizeRenderTargets();
    }
    std::size_t AvailableMemory() const override
    {
        MEMORYSTATUSEX status{};status.dwLength=sizeof(status);
        if(!GlobalMemoryStatusEx(&status))throw std::runtime_error("Windows available-memory query failed");
        return static_cast<std::size_t>(std::min<ULONGLONG>(status.ullAvailPhys,std::numeric_limits<std::size_t>::max()));
    }
    void ApplyDisplaySettings(int width,int height,bool fullscreen,bool vsync) override
    {
        Require(width>=320 && height>=240 && width<=16384 && height<=16384,"Invalid native display resolution");
        const auto fullSize=fullscreen?pcSettings_.fullscreenSize:std::pair<int,int>{};
        const std::array<int,6> request{{width,height,fullscreen?1:0,vsync?1:0,fullSize.first,fullSize.second}};
        if(appliedDisplayRequest_ && *appliedDisplayRequest_==request)return;
        // Only the frame decoration changes. The existing visibility and
        // minimized/maximized state belong to the live HWND, not the saved
        // display preferences. Replacing the complete style here used to
        // clear WS_VISIBLE immediately after the constructor's ShowWindow.
        SetLastError(0);const auto currentStyle=GetWindowLongPtrW(window_,GWL_STYLE);
        Require(currentStyle!=0 || GetLastError()==0,"Windows display style query failed");
        const DWORD decorations=WS_POPUP|WS_OVERLAPPEDWINDOW;
        const DWORD style=(static_cast<DWORD>(currentStyle)&~decorations)|
            (fullscreen ? WS_POPUP:NativeFixedWindowStyle);
        auto monitor=NativeChooseMonitor(monitorDevice_);
        SetFullscreenMode(monitor,fullSize);
        monitor=NativeChooseMonitor(monitorDevice_);
        if(!appliedDisplayRequest_)
            session_.Event("renderer.monitor",monitor.displayName.c_str());
        RECT rectangle{0,0,width,height};int x=CW_USEDEFAULT,y=CW_USEDEFAULT;
        if(fullscreen) {
            rectangle=monitor.display;x=rectangle.left;y=rectangle.top;
        } else {
            Require(AdjustWindowRectEx(&rectangle,style,FALSE,0)!=FALSE,"Windows client-size calculation failed");
            x=monitor.work.left+std::max(0L,(monitor.work.right-monitor.work.left-(rectangle.right-rectangle.left))/2);
            y=monitor.work.top+std::max(0L,(monitor.work.bottom-monitor.work.top-(rectangle.bottom-rectangle.top))/2);
        }
        SetLastError(0);const auto previous=SetWindowLongPtrW(window_,GWL_STYLE,style);
        Require(previous!=0 || GetLastError()==0,"Windows display style change failed");
        geometryChange_=true;
        const auto changed=SetWindowPos(window_,nullptr,x,y,rectangle.right-rectangle.left,rectangle.bottom-rectangle.top,
            SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_NOZORDER);
        geometryChange_=false;Require(changed!=FALSE,"Windows display size change failed");
        RECT actual{};Require(GetWindowRect(window_,&actual)!=FALSE,"Windows outer rectangle is unavailable");
        lockedOuterWidth_=actual.right-actual.left;lockedOuterHeight_=actual.bottom-actual.top;
        RefreshClientSize();
        width_=clientWidth_;height_=clientHeight_;
        if(graphics_) {
            graphics_->ConfigurePresentation(vsync);
            ResizeRenderTargets();
        }
        appliedDisplayRequest_=request;
    }
private:
    void SetFullscreenMode(const NativeWindowsMonitor& monitor,std::pair<int,int> requested)
    {
        const auto device=NativeWideText(monitor.displayName);
        if(activeDisplayMode_ && (requested.first==0 || displayModeDevice_!=device)) {
            const auto result=ChangeDisplaySettingsExW(displayModeDevice_.c_str(),&desktopMode_,nullptr,0,nullptr);
            Require(result==DISP_CHANGE_SUCCESSFUL,"Windows could not restore the desktop display mode");
            activeDisplayMode_=false;
        }
        if(requested.first==0)return;
        DEVMODEW current{};current.dmSize=sizeof(current);
        Require(EnumDisplaySettingsExW(device.c_str(),ENUM_CURRENT_SETTINGS,&current,0)!=FALSE,
            "Cannot read the desktop display mode");
        if(requested==std::make_pair(static_cast<int>(current.dmPelsWidth),static_cast<int>(current.dmPelsHeight)))return;
        const auto modes=NativeWindowsDisplayModes(monitor,NativeWindowsMonitorResolution(monitor));
        const auto desktopRate=static_cast<int>(activeDisplayMode_?desktopMode_.dmDisplayFrequency:current.dmDisplayFrequency);
        const auto score=[desktopRate](const DEVMODEW& mode) {
            return std::make_pair((mode.dmDisplayFlags&DM_INTERLACED)!=0,
                std::abs(static_cast<int>(mode.dmDisplayFrequency)-desktopRate));
        };
        std::optional<DEVMODEW> best;
        for(auto mode:modes) {
            if(mode.dmPelsWidth!=static_cast<DWORD>(requested.first) || mode.dmPelsHeight!=static_cast<DWORD>(requested.second))continue;
            if((!best || score(mode)<score(*best)) &&
                ChangeDisplaySettingsExW(device.c_str(),&mode,nullptr,CDS_TEST,nullptr)==DISP_CHANGE_SUCCESSFUL)
                best=mode;
        }
        Require(best.has_value(),"The selected monitor does not support the requested fullscreen resolution");
        auto mode=*best;
        if(!activeDisplayMode_) {
            desktopMode_=current;displayModeDevice_=device;
            desktopMode_.dmFields=mode.dmFields;
        }
        Require(ChangeDisplaySettingsExW(device.c_str(),&mode,nullptr,CDS_FULLSCREEN,nullptr)==DISP_CHANGE_SUCCESSFUL,
            "Windows could not apply the requested fullscreen mode");
        activeDisplayMode_=true;
    }
    std::unique_ptr<EcstasyVulkanDevice> CreateGraphics(const NativePcSettings& settings)
    {
        EcstasyVulkanOptions options;
        const auto [width,height]=LogicalCanvas();
        options.width=RenderExtent(width,height);options.height=RenderExtent(height,height);
        options.logicalWidth=static_cast<std::uint32_t>(width);options.logicalHeight=static_cast<std::uint32_t>(height);
        options.nativeInstance=instance_;options.nativeWindow=window_;
        options.vsync=settings.vsync;options.fixedRenderExtent=true;
        options.textureFilter=static_cast<EcstasyTextureFilter>(settings.textureFilter);
        options.antialiasing=static_cast<EcstasyAntialiasing>(settings.antialiasing);
        options.anisotropy=static_cast<std::uint32_t>(settings.anisotropy);
        options.adapterId=settings.videoAdapter;
        auto result=std::make_unique<EcstasyVulkanDevice>(options);
        result->SetSharpScaling(settings.sharpScaling);
        return result;
    }
    std::pair<int,int> LogicalCanvas() const noexcept
    {
        return nativeFitAspect({static_cast<int>(width_),static_cast<int>(height_)},
            racePresentation_?pcSettings_.raceAspect:std::pair<int,int>{});
    }
    void ResizeRenderTargets()
    {
        if(!graphics_)return;
        const auto [width,height]=LogicalCanvas();
        graphics_->Resize(RenderExtent(width,height),RenderExtent(height,height));
        graphics_->SetLogicalSize(static_cast<std::uint32_t>(width),static_cast<std::uint32_t>(height));
    }
    std::uint32_t RenderExtent(int dimension,int logicalHeight) const noexcept
    {
        if(!pcSettings_.renderScale)return static_cast<std::uint32_t>(dimension);
        const auto pixels=static_cast<std::uint64_t>(dimension)*448u*static_cast<unsigned>(pcSettings_.renderScale)/
            static_cast<unsigned>(logicalHeight);
        return static_cast<std::uint32_t>(std::clamp(pixels,std::uint64_t{1},std::uint64_t{16384}));
    }
    bool HandleFullscreenShortcut(UINT message,WPARAM key,LPARAM keyFlags) noexcept
    {
        if(key!=VK_RETURN)return false;
        constexpr ULONG_PTR extendedKey=1u<<24;
        constexpr ULONG_PTR altContext=1u<<29;
        constexpr ULONG_PTR previouslyDown=1u<<30;
        const auto flags=static_cast<ULONG_PTR>(keyFlags);
        auto& consumed=fullscreenShortcutKeys_[(flags&extendedKey)!=0 ? 1 : 0];
        if(message==WM_KEYUP || message==WM_SYSKEYUP) {
            const bool wasConsumed=consumed;
            consumed=false;
            return wasConsumed;
        }
        if(message==WM_SYSCHAR)return consumed;
        if(message!=WM_KEYDOWN && message!=WM_SYSKEYDOWN)return false;
        if(flags&previouslyDown)return consumed;
        consumed=message==WM_SYSKEYDOWN && (flags&altContext)!=0 &&
            GetFocus()==window_ && GetForegroundWindow()==window_ && (GetKeyState(VK_CONTROL)&0x8000)==0;
        if(consumed) {
            fullscreenTogglePending_=!fullscreenTogglePending_;
            suppressAltMenu_=true;
        }
        return consumed;
    }
    void PublishRefreshTiming()
    {
        Require(presentationClock_,"Windows display clock has no live GameTime owner");
        LARGE_INTEGER counter{};
        Require(QueryPerformanceCounter(&counter)!=FALSE && counter.QuadPart>=0,
            "Windows source QPC observation is unavailable");
        lastSourceQpc_=static_cast<std::uint64_t>(counter.QuadPart);
        const auto elapsed=refreshClock_.Observe(lastSourceQpc_,qpcFrequency_,
            gameTimeGetVideoMode(presentationClock_));
        // Same measured source interval, two distinct original cells:
        // PSX2VBlank100428 platform counter and eVblCallback111F58 graphics
        // counter. Neither counter reports a GPU or compositor completion.
        for(std::uint64_t index=0;index<elapsed;++index) {
            graphicsNotifyPresentationComplete();platformNotifyVideoBlank();
        }
    }
    bool MenuCursorVisible() const noexcept
    {
        return menuPointerEnabled_ && lastMouseUse_ && GetTickCount64()-lastMouseUse_<3000u &&
            (!input_ || input_->PromptDevice()==platform::NativePromptDevice::Keyboard);
    }
    void RefreshMenuCursor() noexcept
    {
        const bool visible=MenuCursorVisible();
        if(cursorVisible_==visible)return;
        cursorVisible_=visible;
        POINT point{};RECT client{};
        if(GetForegroundWindow()==window_ && GetCursorPos(&point) && WindowFromPoint(point)==window_ &&
            ScreenToClient(window_,&point) && GetClientRect(window_,&client) && PtInRect(&client,point))
            SetCursor(visible?menuCursor_:nullptr);
    }
    void OnMouseUse() noexcept
    {
        lastMouseUse_=GetTickCount64();
        input_->UseKeyboardPrompts();
        RefreshMenuCursor();
    }
    static LRESULT CALLBACK WindowProcedure(HWND window,UINT message,WPARAM first,LPARAM second)
    {
        auto* self=reinterpret_cast<WindowsApplicationHost*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE) {
            self=static_cast<WindowsApplicationHost*>(reinterpret_cast<CREATESTRUCTW*>(second)->lpCreateParams);
            SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if(message==WM_SYSCOMMAND && ((first&0xfff0u)==SC_SIZE || (first&0xfff0u)==SC_MAXIMIZE))return 0;
        if(self && self->input_) {
            if(message==WM_KEYDOWN && (static_cast<std::uintptr_t>(second)&(std::uintptr_t{1}<<30))==0) {
                self->lastMouseUse_=0;
                self->RefreshMenuCursor();
            }
            if(self->menuTextInput_) {
                if(message==WM_CHAR) {
                    if((first==8 || first==13 || (first>=32 && first<=126)) &&
                        self->menuTextLength_<self->menuText_.size())
                        self->menuText_[self->menuTextLength_++]=static_cast<char>(first);
                    self->input_->UseKeyboardPrompts();
                    return 0;
                }
                if(message==WM_KEYDOWN) {
                    char edit{};
                    switch(first) {
                    case VK_LEFT:edit=static_cast<char>(NativeMenuTextKey::Left);break;
                    case VK_RIGHT:edit=static_cast<char>(NativeMenuTextKey::Right);break;
                    case VK_HOME:edit=static_cast<char>(NativeMenuTextKey::Home);break;
                    case VK_END:edit=static_cast<char>(NativeMenuTextKey::End);break;
                    case VK_DELETE:edit=static_cast<char>(NativeMenuTextKey::Delete);break;
                    case VK_ESCAPE:
                        if((static_cast<std::uintptr_t>(second)&(std::uintptr_t{1}<<30))==0)edit=27;
                        break;
                    }
                    if(edit && self->menuTextLength_<self->menuText_.size()) {
                        self->menuText_[self->menuTextLength_++]=edit;
                        self->input_->UseKeyboardPrompts();
                    }
                    return 0;
                }
            }
            if(message==WM_MOUSEMOVE && self->lastMousePosition_!=second) {
                if(self->menuPointerEnabled_) {
                    if(self->replayInput_)self->pointerMoved_=true;
                    else self->menuPointer_.Move({GET_X_LPARAM(second),GET_Y_LPARAM(second)});
                    if(self->lastMousePosition_)self->OnMouseUse();
                }
                self->lastMousePosition_=second;
            }
            if(message==WM_LBUTTONDOWN && self->menuPointerEnabled_) {
                self->OnMouseUse();
                if(self->replayInput_)self->pointerClick_=true;
                else {
                    SetCapture(window);
                    self->menuPointer_.Press({GET_X_LPARAM(second),GET_Y_LPARAM(second)},GetCapture()==window);
                }
            }
            if((message==WM_LBUTTONUP || message==WM_CANCELMODE || message==WM_KILLFOCUS) &&
               self->menuPointer_.captured) {
                const POINT point=message==WM_LBUTTONUP?POINT{GET_X_LPARAM(second),GET_Y_LPARAM(second)}:
                    self->menuPointer_.dragPoint;
                self->menuPointer_.Release(point);
                if(GetCapture()==window)ReleaseCapture();
            }
            if(message==WM_CAPTURECHANGED)self->menuPointer_.Release(self->menuPointer_.dragPoint);
            if(message==WM_MOUSEWHEEL && self->menuPointerEnabled_) {
                self->OnMouseUse();
                self->wheelDelta_=std::clamp(self->wheelDelta_+GET_WHEEL_DELTA_WPARAM(first),-1200,1200);
                return 0;
            }
            if(message==WM_SETCURSOR && LOWORD(second)==HTCLIENT && GetForegroundWindow()==window) {
                SetCursor(self->MenuCursorVisible()?self->menuCursor_:nullptr);
                return TRUE;
            }
            // A controller event pump can dispatch sent Win32 messages too.
            if(self->HandleFullscreenShortcut(message,first,second))return 0;
            const bool suppressMenu=(message==WM_SYSKEYUP || message==WM_KEYUP) && first==VK_MENU && self->suppressAltMenu_;
            if(suppressMenu || message==WM_KILLFOCUS || (message==WM_ACTIVATEAPP && !first))self->suppressAltMenu_=false;
            // Posted input retains MSG.time through a blocked GPU submission.
            // Sent/nested focus messages have no matching queued MSG record.
            const auto* queued=self->dispatchInputMessage_;
            const bool matches=queued && queued->hwnd==window && queued->message==message &&
                queued->wParam==first && queued->lParam==second;
            const auto time=matches?self->dispatchInputTime_:self->FrameTime();
            if(self->input_->HandleWindowInput(message,first,second,time) || suppressMenu)return 0;
        }
        if(self && !self->geometryChange_ && self->lockedOuterWidth_ && self->lockedOuterHeight_) {
            if(message==WM_GETMINMAXINFO) {
                auto& limits=*reinterpret_cast<MINMAXINFO*>(second);
                limits.ptMinTrackSize={self->lockedOuterWidth_,self->lockedOuterHeight_};
                limits.ptMaxTrackSize=limits.ptMinTrackSize;return 0;
            }
            if(message==WM_WINDOWPOSCHANGING && !IsIconic(window)) {
                auto& position=*reinterpret_cast<WINDOWPOS*>(second);
                if(!(position.flags&SWP_NOSIZE)) {position.cx=self->lockedOuterWidth_;position.cy=self->lockedOuterHeight_;}
            }
        }
        if(self && message==WM_DISPLAYCHANGE)self->displayChanged_=true;
        if(message==WM_CLOSE) {DestroyWindow(window);return 0;}
        if(message==WM_DESTROY) {PostQuitMessage(0);return 0;}
        return DefWindowProcW(window,message,first,second);
    }
    void RefreshClientSize()
    {
        RECT rectangle{};Require(GetClientRect(window_,&rectangle)!=FALSE,"Windows client rectangle is unavailable");
        clientWidth_=static_cast<std::uint32_t>(std::max(0L,rectangle.right-rectangle.left));
        clientHeight_=static_cast<std::uint32_t>(std::max(0L,rectangle.bottom-rectangle.top));
    }
    void Close() noexcept
    {
        graphics_.reset();input_.reset();
        if(frameTimer_) {CancelWaitableTimer(frameTimer_);CloseHandle(frameTimer_);frameTimer_=nullptr;}
        if(window_ && IsWindow(window_))DestroyWindow(window_);
        window_=nullptr;
        if(activeDisplayMode_) {
            if(ChangeDisplaySettingsExW(displayModeDevice_.c_str(),&desktopMode_,nullptr,0,nullptr)!=DISP_CHANGE_SUCCESSFUL)
                ChangeDisplaySettingsExW(displayModeDevice_.c_str(),nullptr,nullptr,0,nullptr);
            activeDisplayMode_=false;
        }
        if(fileCursor_) {DestroyCursor(fileCursor_);fileCursor_=nullptr;}
        if(class_)UnregisterClassW(L"NFSHP2NativePAL",instance_);
        class_=0;
    }
    HINSTANCE instance_{};HWND window_{};ATOM class_{};
    std::uint32_t width_{},height_{};
    std::uint32_t clientWidth_{},clientHeight_{};
    std::string monitorDevice_;
    NativePresentationClock presentationLimit_;
    HANDLE frameTimer_{};
    LONG lockedOuterWidth_{},lockedOuterHeight_{};
    bool geometryChange_{},displayChanged_{};
    HCURSOR menuCursor_{},fileCursor_{};
    ULONGLONG lastMouseUse_{};
    std::optional<LPARAM> lastMousePosition_;
    bool menuPointerEnabled_{},pointerMoved_{},pointerClick_{},cursorVisible_{},replayInput_{};
    WindowsMenuPointer menuPointer_;
    bool menuPointerHeld_{};
    int wheelDelta_{},actualMasterVolume_{-1};
    std::optional<std::int32_t> previewMasterVolume_;
    bool menuTextInput_{};
    std::array<char,64> menuText_{};
    std::size_t menuTextLength_{};
    std::uint64_t presentationTimingEpoch_{};
    const GameTime* presentationClock_{}; // Borrowed until ApplicationRuntime finishes.
    NativeRefreshClock refreshClock_;
    std::uint64_t qpcFrequency_{},lastSourceQpc_{};
    const MSG* dispatchInputMessage_{};
    NativeFrameTiming::Clock::time_point dispatchInputTime_{};
    std::function<void()> fullscreenToggle_;
    std::array<bool,2> fullscreenShortcutKeys_{};
    bool fullscreenTogglePending_{};
    bool suppressAltMenu_{};
    // Last successful OS request, not a second saved gameplay/settings owner.
    std::optional<std::array<int,6>> appliedDisplayRequest_;
    DEVMODEW desktopMode_{};
    std::wstring displayModeDevice_;
    bool activeDisplayMode_{};
    std::unique_ptr<WindowsInputDevice> input_;
    bool racePresentation_{};
    NativePcSettings pcSettings_;
    std::filesystem::path pcFile_;
    std::unique_ptr<EcstasyVulkanDevice> graphics_;
    WorldAudioResult audioPumpResult_{WORLD_AUDIO_OK};
};
}
int runNativeWindowsApplication(int argc,const wchar_t* const* argv)
{
    NativeCrashReporter crash;
    std::filesystem::path failureDirectory;
    const char* operation="resolve_launch";
    bool primaryFailureRecorded=false,failureReportSaved=false;
    const auto savePrimaryFailure=[&](const char* message) noexcept {
        primaryFailureRecorded=true;
        crash.Event("session.failure.primary",message,true);
        try {
            std::filesystem::create_directories(failureDirectory);
            std::ofstream report(failureDirectory/"NFSHP2_error.txt",std::ios::binary|std::ios::trunc);
            if(report) {
                report<<"NFSHP2\noperation="
                    <<operation<<"\nerror="<<message<<'\n';
                report.flush();failureReportSaved=static_cast<bool>(report);
            }
        } catch(...) {failureReportSaved=false;}
    };
    try {
        const auto executable=nativeExecutablePath();
        failureDirectory=executable.parent_path()/"Saves";
        std::vector<std::filesystem::path> arguments;
        for(int index=1;index<argc;++index)arguments.emplace_back(argv[index]);
        auto launch=ParseNativeProcessLaunch(executable,std::filesystem::current_path(),arguments);
        failureDirectory=launch.profileDirectory;
        const bool openConfigurator=launch.showHelp || launch.configureOnly || !launch.skipConfigurator;
        crash.Arm(launch.profileDirectory);
        crash.Event("launch.executable",executable.u8string().c_str());
        crash.Event("launch.saves",launch.profileDirectory.u8string().c_str());
        operation="configure";
        crash.Event("startup.phase",operation);
        migrateNativeLaunchFiles(launch,executable);
        const auto pcFile=launch.profileDirectory/NativeSettingsFilename;
        NativePcSettings settings;
        try {settings=loadNativePcSettings(pcFile);}
        catch(const std::exception& error) {
            if(!openConfigurator)throw;
            MessageBoxW(nullptr,(NativeWideText(error.what())+L"\n\nDefaults are shown. The file is replaced only when you save.").c_str(),
                L"NFSHP2 - PC Settings",MB_OK|MB_ICONWARNING);
        }
        applyNativeProcessOverrides(settings,launch);
        if(settings.antialiasing==NativeAntialiasing::Fxaa)settings.antialiasing=NativeAntialiasing::Off;
        std::unique_ptr<GameResources> files;
        if(!launch.configureOnly) {
            operation="open_resources";
            if(!launch.showHelp)try {files=FindNativeGameData(executable,launch);}
            catch(const std::runtime_error& error) {
                if(!openConfigurator)throw;
                crash.Event("launch.source_required",error.what());
            }
            if(!files) {
                files=SelectNativeGameData(launch);
                if(!files) {crash.Event("session.end","game-data selection cancelled",true);return 0;}
            }
        }
        if(openConfigurator) {
            operation="configure";
            NativeConfigurator configurator(settings,launch,pcFile);
            if(!configurator.Run()) {crash.Event("session.end","configurator closed",true);return 0;}
        } else {
            const auto monitor=NativeChooseMonitor(settings.monitorDevice);
            const auto [nativeWidth,nativeHeight]=NativeWindowsMonitorResolution(monitor);
            const bool oversizedWindow=!launch.width && !launch.height &&
                (static_cast<DWORD>(settings.width)>nativeWidth || static_cast<DWORD>(settings.height)>nativeHeight);
            if(oversizedWindow || settings.fullscreenSize.first) {
                const auto modes=NativeWindowsResolutions(monitor);
                if(std::find(modes.begin(),modes.end(),settings.fullscreenSize)==modes.end())settings.fullscreenSize={};
                if(oversizedWindow) {
                    const auto size=modes.empty()?std::make_pair(static_cast<int>(std::clamp<DWORD>(nativeWidth,320,16384)),
                        static_cast<int>(std::clamp<DWORD>(nativeHeight,240,16384))):modes.back();
                    settings.width=size.first;settings.height=size.second;
                }
            }
        }
        crash.Event("launch.resources",launch.dataSource.u8string().c_str());
        if(!std::filesystem::exists(pcFile))saveNativePcSettings(pcFile,settings);
        char graphicsOptions[512]{};
        std::snprintf(graphicsOptions,sizeof(graphicsOptions),"width=%d height=%d fullscreen=%d vsync=%d aa=%d "
            "filter=%d anisotropy=%d debug=%d unlock_all=%d skip_movies=%d controllers=%d,%d vibration=%d show_fps=%d "
            "traffic_vehicles=%d disable_catch_up=%d cockpit_view=%d frame_limit=%d",
            settings.width,settings.height,settings.fullscreen?1:0,settings.vsync?1:0,static_cast<int>(settings.antialiasing),
            static_cast<int>(settings.textureFilter),settings.anisotropy,settings.debugTools?1:0,settings.unlockAll?1:0,
            settings.skipMovies?1:0,settings.input.controller[0],settings.input.controller[1],settings.input.vibration?1:0,settings.showFps?1:0,
            settings.gameplay.trafficVehicles?1:0,settings.gameplay.disableCatchUp?1:0,settings.gameplay.cockpitView?1:0,
            settings.frameLimit);
        crash.Event("graphics.options",graphicsOptions);
        applicationConfigureTesting(settings.debugTools,settings.unlockAll);
        applicationShowFrameRate=settings.showFps;
        applicationNativeGameplay=settings.gameplay;
        configureNativeRenderSettings(settings.render);
        operation="create_windows_devices";
        crash.Event("startup.phase",operation);
        const auto dataDirectory=std::filesystem::is_directory(launch.dataSource)?launch.dataSource:launch.dataSource.parent_path();
        WindowsApplicationHost host(settings,crash,pcFile,dataDirectory);ApplicationRuntime application(*files,host,launch.profileDirectory,
            launch.profileSpecified?std::filesystem::path{}:executable.parent_path());
        try {
            host.BindPresentationClock(application.Clock());
            operation="initialize_application";
            crash.Event("startup.phase",operation);
            application.Initialize(launch.configuration);
            applicationConfigureTesting(settings.debugTools,settings.unlockAll);
            applicationShowFrameRate=settings.showFps;
            applicationLaunchOptions.skipMovies=settings.skipMovies?1:0;
            application.SetDisplayOptions(settings.width,settings.height,settings.fullscreen,settings.vsync);
            host.BindFullscreenToggle([&] {
                const auto display=application.Frontend().nativeDisplay;
                const bool fullscreen=!display.fullscreen;
                auto saved=*host.PcSettings();
                saved.fullscreen=fullscreen;
                std::string error;
                if(host.SavePcSettings(saved,error))
                    application.SetDisplayOptions(display.width,display.height,fullscreen,display.vsync);
                else crash.Event("display.mode_save_failed",error.c_str());
            });
            operation="run_application";
            crash.Event("startup.phase",operation);
            for(;;) {
                crash.ObserveFlow(application.Flow());
                if(!application.Tick())break;
            }
            operation="shutdown";
            crash.Event("startup.phase",operation);
            application.Shutdown();
        } catch(...) {
            // Persist the primary fault before Application or host destruction
            // can encounter a second failure in the resource retirement graph.
            const auto primary=std::current_exception();
            try {std::rethrow_exception(primary);}
            catch(const std::exception& error) {savePrimaryFailure(error.what());}
            catch(...) {savePrimaryFailure("Non-standard native application exception");}
            try {application.Shutdown();}
            catch(const std::exception& error) {crash.Event("session.failure.cleanup",error.what(),true);}
            catch(...) {crash.Event("session.failure.cleanup","Non-standard cleanup exception",true);}
            std::rethrow_exception(primary);
        }
        crash.Event("session.end","normal",true);
        return 0;
    } catch(const std::exception& error) {
        if(!primaryFailureRecorded)savePrimaryFailure(error.what());
        auto message=NativeWideText(error.what());
        if(!failureReportSaved)message+=L"\n\nCould not save NFSHP2_error.txt.";
        MessageBoxW(nullptr,message.c_str(),L"NFSHP2 — startup/runtime failure",MB_OK|MB_ICONERROR);
        return 1;
    } catch(...) {
        if(!primaryFailureRecorded)savePrimaryFailure("Non-standard native application exception");
        MessageBoxW(nullptr,L"Non-standard native application exception. See NFSHP2_error.txt.",
            L"NFSHP2 — startup/runtime failure",MB_OK|MB_ICONERROR);
        return 1;
    }
}
}
#endif
#endif // HP2_APPLICATION_ENTRY
