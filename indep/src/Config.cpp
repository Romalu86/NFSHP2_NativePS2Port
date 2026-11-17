#include "Joylog.hpp"
#include "bware/inc/bMath.hpp"
#include "Flags.hpp"
#include "CarSounds.hpp"
#include "Fred.hpp"
#include "EventTree.hpp"
#include "inc/memorycard.hpp"
#include <cstring>
#include <atomic>
#include <charconv>
#include <cmath>
#include <locale>
#include <limits>
#include <numeric>
#include <chrono>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace hp2 {
NativeGameplayOptions applicationNativeGameplay{};
namespace {
constexpr const char* NativeKeyNames[]={"select","left_stick","right_stick","pause",
    "up","right","down","left","l2","r2","l1","r1","triangle","circle",
    "cross","square","steer_left","steer_right","confirm","back"};
std::string TrimPcSetting(const std::string& text)
{
    const auto first=text.find_first_not_of(" \t\r\n");
    if(first==std::string::npos)return {};
    return text.substr(first,text.find_last_not_of(" \t\r\n")-first+1);
}
std::int32_t PcSettingInteger(const std::string& text,const std::string& key)
{
    std::int32_t result{};
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result);
    if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size())
        throw std::invalid_argument("Invalid PC setting '"+key+"': "+text);
    return result;
}
bool PcSettingBoolean(const std::string& text,const std::string& key)
{
    const auto value=PcSettingInteger(text,key);
    if(value!=0 && value!=1)throw std::invalid_argument("PC setting '"+key+"' must be0 or1");
    return value!=0;
}
float PcSettingFloat(const std::string& text,const std::string& key)
{
    float value{};
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !std::isfinite(value))
        throw std::invalid_argument("Invalid numeric PC setting: "+key);
    return value;
}
std::uint16_t PcSettingKey(const std::string& value,const std::string& key)
{
    const auto code=PcSettingInteger(value,key);
    if(code<0 || code>255)throw std::invalid_argument("Invalid virtual key for "+key);
    return static_cast<std::uint16_t>(code);
}

platform::NativeInputBinding PcSettingBinding(const std::string& text,const std::string& key)
{
    const auto first=text.find(','),second=first==std::string::npos?first:text.find(',',first+1);
    if(first==std::string::npos || second==std::string::npos || text.find(',',second+1)!=std::string::npos)
        throw std::invalid_argument("Invalid gamepad binding: "+key);
    const auto kind=PcSettingInteger(text.substr(0,first),key);
    if(kind<0 || kind>static_cast<int>(platform::NativeBindingKind::AxisFullNegative))
        throw std::invalid_argument("Unknown gamepad binding kind: "+key);
    return {static_cast<platform::NativeBindingKind>(kind),
        PcSettingInteger(text.substr(first+1,second-first-1),key),PcSettingInteger(text.substr(second+1),key)};
}
bool ReadNativePlayerSetting(platform::NativeInputSettings& settings,const std::string& key,const std::string& value)
{
    for(std::size_t port=0;port<2;++port) {
        const auto prefix=std::string("p")+std::to_string(port+1)+"_";
        if(key.compare(0,prefix.size(),prefix)!=0)continue;
        const auto field=key.substr(prefix.size());auto& profile=settings.profiles[port];
        if(field=="device_mode") {
            const auto mode=PcSettingInteger(value,key);
            if(mode<0 || mode>static_cast<int>(platform::NativeDeviceMode::Disabled))throw std::invalid_argument("Unknown input device mode");
            settings.deviceMode[port]=static_cast<platform::NativeDeviceMode>(mode);return true;
        }
        if(field=="device_key") {settings.deviceKey[port]=value;return true;}
        if(field=="trigger_threshold") {profile.triggerThreshold=PcSettingInteger(value,key);return true;}
        if(field=="adaptive_triggers") {profile.adaptiveTriggers=PcSettingBoolean(value,key);return true;}
        if(field=="police_radio_speaker") {profile.policeRadioSpeaker=PcSettingBoolean(value,key);return true;}
        if(field=="police_radio_volume") {profile.policeRadioVolume=PcSettingInteger(value,key);return true;}
        if(field=="vibration_scale") {profile.vibrationScale=PcSettingInteger(value,key);return true;}
        for(std::size_t action=0;action<20;++action)if(field==std::string("key_")+NativeKeyNames[action]) {
            const auto code=PcSettingInteger(value,key);
            if(code<0 || code>254)throw std::invalid_argument("Invalid Windows virtual key: "+value);
            (port==0?settings.keys:settings.player2Keys)[action]=static_cast<std::uint16_t>(code);return true;
        }
        for(std::size_t action=0;action<16;++action)if(field=="button_"+std::to_string(action)) {
            profile.buttons[action]=PcSettingBinding(value,key);return true;
        }
        for(std::size_t axis=0;axis<4;++axis) {
            const auto index=std::to_string(axis);
            if(field=="axis_"+index) {profile.axes[axis]=PcSettingInteger(value,key);return true;}
            if(field=="invert_"+index) {profile.invert[axis]=PcSettingBoolean(value,key);return true;}
            if(field=="deadzone_"+index) {profile.deadzone[axis]=PcSettingInteger(value,key);return true;}
            if(field=="saturation_"+index) {profile.saturation[axis]=PcSettingInteger(value,key);return true;}
        }
    }
    return false;
}
void WriteNativeConfiguration(const std::filesystem::path& file,const std::string& text)
{
    const auto directory=(file.parent_path().empty()?std::filesystem::current_path():file.parent_path()).u8string();
    cMC storage(directory.c_str());const auto name=file.filename().u8string();
    if(storage.MCWrite(name.c_str(),text.data(),text.size())!=cMC::Ok)
        throw std::runtime_error("Cannot save readable configuration: "+storage.LastError());
}
}

void validateNativePcSettings(const NativePcSettings& settings)
{
    if(settings.width<320 || settings.height<240 || settings.width>16384 || settings.height>16384)
        throw std::invalid_argument("PC resolution must be320..16384 by240..16384 pixels");
    const auto [fullWidth,fullHeight]=settings.fullscreenSize;
    if((fullWidth || fullHeight) && (fullWidth<320 || fullHeight<240 || fullWidth>16384 || fullHeight>16384))
        throw std::invalid_argument("Invalid fullscreen resolution");
    if(settings.textureFilter<NativeTextureFilter::Authored || settings.textureFilter>NativeTextureFilter::Anisotropic)
        throw std::invalid_argument("Unknown PC texture filter");
    if(settings.antialiasing<NativeAntialiasing::Off || settings.antialiasing>NativeAntialiasing::SmaaUltra)
        throw std::invalid_argument("Unknown PC antialiasing mode");
    if(settings.anisotropy!=2 && settings.anisotropy!=4 && settings.anisotropy!=8 && settings.anisotropy!=16)
        throw std::invalid_argument("Anisotropic filtering must be 2, 4, 8 or 16");
    if(settings.frameLimit<0 || settings.frameLimit>1000)
        throw std::invalid_argument("Frame limit must be 0 (unlocked) or 1..1000 FPS");
    if(settings.masterVolume<0 || settings.masterVolume>100)
        throw std::invalid_argument("Master volume must be 0..100 percent");
    const auto [aspectWidth,aspectHeight]=settings.raceAspect;
    if(aspectWidth<0 || aspectHeight<0 || aspectWidth>16384 || aspectHeight>16384 ||
        ((aspectWidth==0)!=(aspectHeight==0)) ||
        settings.render.reflections<0 || settings.render.reflections>2 ||
        settings.render.brightness<-50 || settings.render.brightness>50 ||
        settings.render.contrast<50 || settings.render.contrast>150 ||
        settings.render.gamma<50 || settings.render.gamma>200)
        throw std::invalid_argument("Invalid PC picture preference");
    if(settings.renderScale<0 || settings.renderScale>8)
        throw std::invalid_argument("Render scale must be native or 1..8");
    if(settings.monitorDevice.size()>512 || settings.monitorDevice.find_first_of("\r\n\t")!=std::string::npos)
        throw std::invalid_argument("Invalid monitor identifier");
    if(!settings.videoAdapter.empty() && (settings.videoAdapter.size()!=32 ||
        settings.videoAdapter.find_first_not_of("0123456789abcdef")!=std::string::npos))
        throw std::invalid_argument("Invalid Vulkan adapter identifier");
    const auto validRender=validateNativeRenderSettings(settings.render);
    if(validRender.drawDistanceScale!=settings.render.drawDistanceScale ||
       validRender.mirrorDistanceScale!=settings.render.mirrorDistanceScale)
        throw std::invalid_argument("Draw distance must be 1..4 and mirror distance 0.5..4");
    platform::ValidateNativeInputSettings(settings.input);
}

std::pair<int,int> nativeFitAspect(std::pair<int,int> extent,std::pair<int,int> ratio) noexcept
{
    const auto [numerator,denominator]=ratio;
    auto& [width,height]=extent;
    if(width<=0 || height<=0 || numerator<=0 || denominator<=0)return extent;
    const auto byWidth=static_cast<std::int64_t>(width)*denominator;
    const auto byHeight=static_cast<std::int64_t>(height)*numerator;
    if(byWidth>byHeight)width=std::max(1,static_cast<int>(byHeight/denominator));
    else height=std::max(1,static_cast<int>(byWidth/numerator));
    return extent;
}

std::vector<std::pair<int,int>> nativeAspectChoices(const std::vector<std::pair<int,int>>& modes)
{
    std::set<std::pair<int,int>> choices{{0,0},{4,3},{5,4},{3,2},{16,10},{16,9},{21,9},{32,9},{9,16}};
    for(const auto& [width,height]:modes)if(width>0 && height>0) {
        const auto divisor=std::gcd(width,height);
        if(width*3==height*4 || width*10==height*16 || width*9==height*21)continue;
        choices.emplace(width/divisor,height/divisor);
    }
    return {choices.begin(),choices.end()};
}

NativePcSettings loadNativePcSettings(const std::filesystem::path& file)
{
    NativePcSettings settings;
    if(!std::filesystem::exists(file))return settings;
    if(!std::filesystem::is_regular_file(file) || std::filesystem::file_size(file)>65536)
        throw std::runtime_error("Invalid PC settings file: "+file.u8string());
    std::ifstream input(file,std::ios::binary);
    if(!input)throw std::runtime_error("Cannot read PC settings: "+file.u8string());
    std::set<std::string> seen;std::string line;std::size_t lineNumber{};
    while(std::getline(input,line)) {
        ++lineNumber;line=TrimPcSetting(line);
        if(line.empty() || line[0]=='#' || line[0]==';')continue;
        const auto equal=line.find('=');
        if(equal==std::string::npos)throw std::runtime_error("Missing '=' in PC settings, line "+std::to_string(lineNumber));
        const auto key=TrimPcSetting(line.substr(0,equal)),value=TrimPcSetting(line.substr(equal+1));
        if(!seen.insert(key).second)throw std::runtime_error("Duplicate PC setting: "+key);
        if(key=="format") {
            const auto format=PcSettingInteger(value,key);
            if(format!=1 && format!=2)throw std::runtime_error("Unsupported PC settings format");
        } else if(key=="width")settings.width=PcSettingInteger(value,key);
        else if(key=="height")settings.height=PcSettingInteger(value,key);
        else if(key=="fullscreen")settings.fullscreen=PcSettingBoolean(value,key);
        else if(key=="fullscreen_width")settings.fullscreenSize.first=PcSettingInteger(value,key);
        else if(key=="fullscreen_height")settings.fullscreenSize.second=PcSettingInteger(value,key);
        else if(key=="vsync")settings.vsync=PcSettingBoolean(value,key);
        else if(key=="show_fps")settings.showFps=PcSettingBoolean(value,key);
        else if(key=="monitor")settings.monitorDevice=value;
        else if(key=="video_adapter")settings.videoAdapter=value;
        else if(key=="frame_limit")settings.frameLimit=PcSettingInteger(value,key);
        else if(key=="render_scale")settings.renderScale=PcSettingInteger(value,key);
        else if(key=="sharp_scaling")settings.sharpScaling=PcSettingBoolean(value,key);
        else if(key=="master_volume")settings.masterVolume=PcSettingInteger(value,key);
        else if(key=="race_aspect") {
            const auto colon=value.find(':');
            if(colon!=std::string::npos)settings.raceAspect={
                PcSettingInteger(value.substr(0,colon),key),PcSettingInteger(value.substr(colon+1),key)};
            else {
                constexpr std::pair<int,int> legacy[]{{0,0},{4,3},{16,10},{16,9}};
                const auto index=PcSettingInteger(value,key);
                if(index<0 || index>=static_cast<int>(std::size(legacy)))throw std::invalid_argument("Invalid race aspect");
                settings.raceAspect=legacy[index];
            }
        }
        else if(key=="maximum_detail")settings.render.maximumDetail=PcSettingBoolean(value,key);
        else if(key=="reflections")settings.render.reflections=PcSettingInteger(value,key);
        else if(key=="brightness")settings.render.brightness=PcSettingInteger(value,key);
        else if(key=="contrast")settings.render.contrast=PcSettingInteger(value,key);
        else if(key=="gamma")settings.render.gamma=PcSettingInteger(value,key);
        else if(key=="draw_distance")settings.render.drawDistanceScale=PcSettingFloat(value,key);
        else if(key=="mirror_distance")settings.render.mirrorDistanceScale=PcSettingFloat(value,key);
        else if(key=="shadow_quality" || key=="reflection_quality" || key=="show_hidden_skins") {
            // Retired launcher choices are accepted only for migration. They
            // no longer have runtime storage or a renderer consumer; saving
            // the settings removes the old keys.
        }
        else if(key=="racer_nitro")settings.gameplay.racerNitro=PcSettingBoolean(value,key);
        else if(key=="traffic_vehicles")settings.gameplay.trafficVehicles=PcSettingBoolean(value,key);
        else if(key=="disable_catch_up")settings.gameplay.disableCatchUp=PcSettingBoolean(value,key);
        else if(key=="cockpit_view")settings.gameplay.cockpitView=PcSettingBoolean(value,key);
        else if(key=="restore_cut_traffic")settings.gameplay.restoreCutTraffic=PcSettingBoolean(value,key);
        else if(key=="random_nfs_edition_opponents")settings.gameplay.randomNfsEditionOpponents=PcSettingBoolean(value,key);
        else if(key=="texture_filter") {
            if(value=="authored")settings.textureFilter=NativeTextureFilter::Authored;
            else if(value=="bilinear")settings.textureFilter=NativeTextureFilter::Bilinear;
            else if(value=="trilinear")settings.textureFilter=NativeTextureFilter::Trilinear;
            else if(value=="anisotropic")settings.textureFilter=NativeTextureFilter::Anisotropic;
            else throw std::invalid_argument("Unknown PC texture filter: "+value);
        } else if(key=="antialiasing") {
            if(value=="off")settings.antialiasing=NativeAntialiasing::Off;
            else if(value=="fxaa")settings.antialiasing=NativeAntialiasing::Fxaa;
            else if(value=="msaa2")settings.antialiasing=NativeAntialiasing::Msaa2;
            else if(value=="msaa4")settings.antialiasing=NativeAntialiasing::Msaa4;
            else if(value=="msaa8")settings.antialiasing=NativeAntialiasing::Msaa8;
            else if(value=="smaa2")settings.antialiasing=NativeAntialiasing::SmaaHigh;
            else if(value=="smaa4")settings.antialiasing=NativeAntialiasing::SmaaUltra;
            else throw std::invalid_argument("Unknown PC antialiasing mode: "+value);
        } else if(key=="anisotropy")settings.anisotropy=PcSettingInteger(value,key);
        else if(key=="controller1")settings.input.controller[0]=PcSettingInteger(value,key);
        else if(key=="controller2")settings.input.controller[1]=PcSettingInteger(value,key);
        else if(key=="key_zone_freeze")settings.input.zoneKeys[0][0]=PcSettingKey(value,key);
        else if(key=="key_zone_zoom")settings.input.zoneKeys[0][1]=PcSettingKey(value,key);
        else if(key=="key_p2_zone_freeze")settings.input.zoneKeys[1][0]=PcSettingKey(value,key);
        else if(key=="key_p2_zone_zoom")settings.input.zoneKeys[1][1]=PcSettingKey(value,key);
        else if(key=="left_deadzone")settings.input.leftDeadzone=PcSettingInteger(value,key);
        else if(key=="right_deadzone")settings.input.rightDeadzone=PcSettingInteger(value,key);
        else if(key=="trigger_deadzone")settings.input.triggerThreshold=PcSettingInteger(value,key);
        else if(key=="vibration")settings.input.vibration=PcSettingBoolean(value,key);
        else if(key=="prompt_style")settings.input.promptStyle=PcSettingInteger(value,key);
        else if(key=="steering_sensitivity")settings.input.steeringSensitivity=PcSettingInteger(value,key);
        else if(key=="invert_steering")settings.input.invertSteering=PcSettingBoolean(value,key);
        else if(key=="invert_look_y")settings.input.invertLookY=PcSettingBoolean(value,key);
        else if(key=="swap_sticks")settings.input.swapSticks=PcSettingBoolean(value,key);
        else if(key=="debug")settings.debugTools=PcSettingBoolean(value,key);
        else if(key=="unlock_all")settings.unlockAll=PcSettingBoolean(value,key);
        else if(key=="skip_movies")settings.skipMovies=PcSettingBoolean(value,key);
        // Discard retired boolean preferences when these settings are saved.
        else if(key=="verbose_logging" || key=="open_free_run_roads" || key=="show_hidden_tracks")
            (void)PcSettingBoolean(value,key);
        else {
            bool found=ReadNativePlayerSetting(settings.input,key,value);
            for(std::size_t index=0;index<settings.input.keys.size();++index)if(key==std::string("key_")+NativeKeyNames[index]) {
                const auto number=PcSettingInteger(value,key);
                if(number<0 || number>254)throw std::invalid_argument("Invalid Windows virtual key: "+value);
                settings.input.keys[index]=static_cast<std::uint16_t>(number);found=true;break;
            }
            if(!found)throw std::invalid_argument("Unknown PC setting: "+key);
        }
    }
    if(input.bad())throw std::runtime_error("PC settings read failed: "+file.u8string());
    validateNativePcSettings(settings);return settings;
}

namespace {
void AppendInputSettings(std::ostream& output,const platform::NativeInputSettings& settings)
{
    output<<"controller1="<<settings.controller[0]<<"\ncontroller2="<<settings.controller[1]
        <<"\nkey_zone_freeze="<<settings.zoneKeys[0][0]<<"\nkey_zone_zoom="<<settings.zoneKeys[0][1]
        <<"\nkey_p2_zone_freeze="<<settings.zoneKeys[1][0]<<"\nkey_p2_zone_zoom="<<settings.zoneKeys[1][1]
        <<"\nleft_deadzone="<<settings.leftDeadzone<<"\nright_deadzone="<<settings.rightDeadzone
        <<"\ntrigger_deadzone="<<settings.triggerThreshold
        <<"\nprompt_style="<<settings.promptStyle<<"\nsteering_sensitivity="<<settings.steeringSensitivity
        <<"\nvibration="<<settings.vibration<<"\ninvert_steering="<<settings.invertSteering
        <<"\ninvert_look_y="<<settings.invertLookY<<"\nswap_sticks="<<settings.swapSticks
    <<'\n';
    for(std::size_t index=0;index<settings.keys.size();++index)
        output<<"key_"<<NativeKeyNames[index]<<'='<<settings.keys[index]<<'\n';
    for(std::size_t port=0;port<2;++port) {
        const auto prefix=std::string("p")+std::to_string(port+1)+"_";
        const auto& profile=settings.profiles[port];
        output<<prefix<<"device_mode="<<static_cast<int>(settings.deviceMode[port])<<'\n'
            <<prefix<<"device_key="<<settings.deviceKey[port]<<'\n'
            <<prefix<<"trigger_threshold="<<profile.triggerThreshold<<'\n'
            <<prefix<<"vibration_scale="<<profile.vibrationScale<<'\n'
            <<prefix<<"adaptive_triggers="<<profile.adaptiveTriggers<<'\n'
            <<prefix<<"police_radio_speaker="<<profile.policeRadioSpeaker<<'\n'
            <<prefix<<"police_radio_volume="<<profile.policeRadioVolume<<'\n';
        if(port==1)for(std::size_t action=0;action<20;++action)
            output<<prefix<<"key_"<<NativeKeyNames[action]<<'='<<settings.player2Keys[action]<<'\n';
        for(std::size_t action=0;action<16;++action) {
            const auto& binding=profile.buttons[action];
            output<<prefix<<"button_"<<action<<'='<<static_cast<int>(binding.kind)<<','<<binding.index<<','<<binding.direction<<'\n';
        }
        for(std::size_t axis=0;axis<4;++axis)
            output<<prefix<<"axis_"<<axis<<'='<<profile.axes[axis]<<'\n'
                <<prefix<<"invert_"<<axis<<'='<<profile.invert[axis]<<'\n'
                <<prefix<<"deadzone_"<<axis<<'='<<profile.deadzone[axis]<<'\n'
                <<prefix<<"saturation_"<<axis<<'='<<profile.saturation[axis]<<'\n';
    }
}
}

void saveNativeInputSettings(const std::filesystem::path& file,const platform::NativeInputSettings& settings)
{
    platform::ValidateNativeInputSettings(settings);
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output<<"# NFSHP2 profile controls.\nformat=2\n";
    AppendInputSettings(output,settings);
    WriteNativeConfiguration(file,output.str());
}

void saveNativePcSettings(const std::filesystem::path& file,const NativePcSettings& settings)
{
    validateNativePcSettings(settings);
    std::ostringstream output;
    constexpr const char* filters[]={"authored","bilinear","trilinear","anisotropic"};
    constexpr const char* antialiasing[]={"off","fxaa","msaa2","msaa4","msaa8","smaa2","smaa4"};
    output.imbue(std::locale::classic());
    output.precision(std::numeric_limits<float>::max_digits10);
    output<<"# NFSHP2 PC settings. Progress is stored separately in profile.json.\nformat=2\n"
        <<"width="<<settings.width<<"\nheight="<<settings.height
        <<"\nfullscreen="<<settings.fullscreen<<"\nvsync="<<settings.vsync
        <<"\nshow_fps="<<settings.showFps
        <<"\nmonitor="<<settings.monitorDevice<<"\nvideo_adapter="<<settings.videoAdapter<<"\nframe_limit="<<settings.frameLimit
        <<"\nrender_scale="<<settings.renderScale<<"\nsharp_scaling="<<settings.sharpScaling
        <<"\nmaster_volume="<<settings.masterVolume
        <<"\nfullscreen_width="<<settings.fullscreenSize.first<<"\nfullscreen_height="<<settings.fullscreenSize.second
        <<"\nrace_aspect="<<settings.raceAspect.first<<':'<<settings.raceAspect.second
        <<"\nmaximum_detail="<<settings.render.maximumDetail
        <<"\nreflections="<<settings.render.reflections<<"\nbrightness="<<settings.render.brightness
        <<"\ncontrast="<<settings.render.contrast<<"\ngamma="<<settings.render.gamma
        <<"\ndraw_distance="<<settings.render.drawDistanceScale<<"\nmirror_distance="<<settings.render.mirrorDistanceScale
        <<"\nracer_nitro="<<settings.gameplay.racerNitro<<"\ntraffic_vehicles="<<settings.gameplay.trafficVehicles
        <<"\nrestore_cut_traffic="<<settings.gameplay.restoreCutTraffic
        <<"\nrandom_nfs_edition_opponents="<<settings.gameplay.randomNfsEditionOpponents
        <<"\ndisable_catch_up="<<settings.gameplay.disableCatchUp<<"\ncockpit_view="<<settings.gameplay.cockpitView
        <<"\ntexture_filter="<<filters[static_cast<std::size_t>(settings.textureFilter)]
        <<"\nantialiasing="<<antialiasing[static_cast<std::size_t>(settings.antialiasing)]
        <<"\nanisotropy="<<settings.anisotropy
        <<"\ndebug="<<settings.debugTools<<"\nunlock_all="<<settings.unlockAll<<"\nskip_movies="<<settings.skipMovies
        <<'\n';
    AppendInputSettings(output,settings.input);
    if(!output)throw std::runtime_error("PC settings formatting failed");
    WriteNativeConfiguration(file,output.str());
}

std::int32_t loadNativeTrackConfiguration(const std::filesystem::path& file)
{
    if(!std::filesystem::exists(file))return -1;
    if(!std::filesystem::is_regular_file(file) || std::filesystem::file_size(file)>4096)
        throw std::runtime_error("Invalid track configuration: "+file.u8string());
    std::ifstream input(file,std::ios::binary);if(!input)throw std::runtime_error("Cannot open track configuration");
    // Legacy files are read-only migration inputs and explicit old test inputs.
    if(file.extension()==".bin" || file.extension()==".BIN") {
        std::uint8_t bytes[4]{};
        if(!input.read(reinterpret_cast<char*>(bytes),4))throw std::runtime_error("Legacy track configuration is shorter than four bytes");
        const auto value=std::uint32_t{bytes[0]}|(std::uint32_t{bytes[1]}<<8)|(std::uint32_t{bytes[2]}<<16)|(std::uint32_t{bytes[3]}<<24);
        return static_cast<std::int32_t>(value);
    }
    std::set<std::string> keys;std::string line;std::int32_t track=-1;
    while(std::getline(input,line)) {
        line=TrimPcSetting(line);if(line.empty() || line[0]=='#' || line[0]==';')continue;
        const auto equal=line.find('=');if(equal==std::string::npos)throw std::runtime_error("Track configuration needs key=value");
        const auto key=TrimPcSetting(line.substr(0,equal));const auto value=TrimPcSetting(line.substr(equal+1));
        if(!keys.insert(key).second)throw std::runtime_error("Duplicate track configuration key: "+key);
        if(key=="format") {if(PcSettingInteger(value,key)!=1)throw std::runtime_error("Unsupported track configuration format");}
        else if(key=="track_selection")track=PcSettingInteger(value,key);
        else throw std::runtime_error("Unknown track configuration key: "+key);
    }
    if(input.bad() || !keys.count("format") || !keys.count("track_selection"))throw std::runtime_error("Incomplete track configuration");
    return track;
}
void saveNativeTrackConfiguration(const std::filesystem::path& file,std::int32_t track)
{
    WriteNativeConfiguration(file,"# Original boot track selection. -1 uses the normal menu.\nformat=1\ntrack_selection="+
        std::to_string(track)+"\n");
}
void migrateNativeLaunchFiles(const NativeProcessLaunch& launch,const std::filesystem::path& executable)
{
    const auto& directory=launch.profileDirectory;
    const auto settings=directory/NativeSettingsFilename;
    const auto oldSettings=directory/"NFSHP2_pc.ini";
    if(!std::filesystem::exists(settings)) {
        if(std::filesystem::is_regular_file(oldSettings))std::filesystem::rename(oldSettings,settings);
        else if(!launch.profileSpecified) {
            const auto legacy=executable.parent_path()/"NFSHP2_pc.ini";
            if(std::filesystem::is_regular_file(legacy))
                saveNativePcSettings(settings,loadNativePcSettings(legacy));
        }
    }

    if(!launch.configurationSpecified && !std::filesystem::exists(launch.configuration)) {
        const auto oldTrack=directory/"config.ini";
        if(std::filesystem::is_regular_file(oldTrack)) {
            if(loadNativeTrackConfiguration(oldTrack)==-1)std::filesystem::remove(oldTrack);
            else std::filesystem::rename(oldTrack,launch.configuration);
        } else {
            auto legacy=directory/"config.bin";
            if(!std::filesystem::exists(legacy) && !launch.profileSpecified)legacy=executable.parent_path()/"config.bin";
            const auto selected=loadNativeTrackConfiguration(legacy);
            if(selected!=-1)saveNativeTrackConfiguration(launch.configuration,selected);
        }
    }

    const auto oldSource=directory/"NFSHP2_data.txt";
    std::error_code error;
    if(std::filesystem::is_regular_file(oldSource,error) && std::filesystem::file_size(oldSource,error)<=131072 && !error) {
        std::ifstream input(oldSource,std::ios::binary);
        std::string mode,path;
        std::getline(input,mode);
        std::getline(input,path);
        mode=TrimPcSetting(mode);
        path=TrimPcSetting(path);
        const bool drivePath=path.size()>2 && path[1]==':' && (path[2]=='/' || path[2]=='\\');
        const bool networkPath=path.compare(0,2,"\\\\")==0 || path.compare(0,2,"//")==0;
        const bool launchCache=(mode=="archive" || mode=="data") && (drivePath || networkPath);
        input.close();
        if(launchCache)std::filesystem::remove(oldSource,error);
    }
}

void applyNativeProcessOverrides(NativePcSettings& settings,const NativeProcessLaunch& launch)
{
    if(launch.width)settings.width=*launch.width;
    if(launch.height)settings.height=*launch.height;
    if(launch.fullscreenSpecified)settings.fullscreen=launch.fullscreen;
    if(launch.vsyncSpecified)settings.vsync=launch.vsync;
    if(launch.debugSpecified)settings.debugTools=launch.debugTools;
    if(launch.unlockSpecified)settings.unlockAll=launch.unlockAll;
    if(launch.textureFilter)settings.textureFilter=static_cast<NativeTextureFilter>(*launch.textureFilter);
    if(launch.antialiasing)settings.antialiasing=static_cast<NativeAntialiasing>(*launch.antialiasing);
    if(launch.anisotropy)settings.anisotropy=*launch.anisotropy;
    if(launch.skipMovies)settings.skipMovies=*launch.skipMovies;
    if(launch.showFps)settings.showFps=*launch.showFps;
    validateNativePcSettings(settings);
}
}

// Actual PAL Config.obj defaults, individually paired through the complete
// Load/Save readers and writers. No mutable RaceParameters values live here.
ApplicationLaunchOptions applicationLaunchOptions={
    0,1.0f,-1,1,
    1,0,8,1,0,1,1,1,0,
    0.0f,0.5f,0,1,1,{37,37},{1,1},
    0,0,0,30.0f,120.0f,0,1,1,0,0,0
};

bool applicationDebugToolsEnabled=false;
bool applicationShowFrameRate=true;
bool applicationUnlockAllForTesting=false;
bool applicationDebugExitRequested=false;
int32_t applicationScreenPrintf=0;
void applicationConfigureTesting(bool debugTools,bool unlockAll)
{
    applicationDebugToolsEnabled=debugTools;
    applicationUnlockAllForTesting=unlockAll;
    applicationDebugExitRequested=false;
    // Reuse the source Config and EventTree globals. The unlock override is
    // queried without writing completed events or fabricated reward records.
    applicationLaunchOptions.debugCamerasEnabled=debugTools?1:0;
    applicationLaunchOptions.allCarsAvailable=0;
    hp2::frontend::eventStatusForceComplete=unlockAll?1:0;
}

JoylogConfigItems applicationJoylogConfigBindings()
{
    auto& flags=applicationLaunchOptions;
    JoylogConfigItems view{};
    int32_t* cells[31]={&flags.skipFrontend,&flags.startupPlayerCars,&flags.startupAICars,&flags.startupLaps,
      &flags.startupRaceType,&flags.startupMaxCops,nullptr,&flags.startupTickets,&flags.startupRoadBlocks,
      &flags.startupHelicopter,&flags.startupBeACop,nullptr,nullptr,&flags.startupDifficulty,
      &flags.startupJumpCamera,&flags.startupZoneCamera,&flags.startupPlayerModel[0],&flags.startupPlayerModel[1],
      &flags.startupPlayerSkin[0],&flags.startupPlayerSkin[1],&flags.allCarsAvailable,&hp2::frontend::frontendMaximumOpponents,
      &flags.tunePreRaceSequence,&flags.demoTrackDirection,nullptr,nullptr,&flags.debugCamerasEnabled,
      &carSoundCurrentPolicy()->audioSystemEnabled,&flags.engineSoundEnabled,&flags.musicEnabled,&flags.skipMovies};
    for (unsigned i=0;i<31;++i)view.integers[i]=cells[i];
    view.numericOption=&flags.timeUntilCopsAppear;
    view.floatBits[11]=&flags.startupTrafficDensity;view.floatBits[12]=&flags.startupOncomingTraffic;
    view.floatBits[24]=&flags.beginDemoSeconds;view.floatBits[25]=&flags.demoDurationSeconds;
    return view;
}

bool joylogConfigBindingsValid(const JoylogConfigItems& items) noexcept
{
    if (!items.numericOption)return false;
    for (unsigned i=0;i<31;++i)if (i!=6) {
        if ((items.integers[i]!=nullptr)==(items.floatBits[i]!=nullptr))return false;
        if (items.floatBits[i] && i!=11 && i!=12 && i!=24 && i!=25)return false;
    }
    return true;
}

// PAL12FAF0..12FD34; beta12FC40. LoadConfigItems reads the same 31
// application cells that SaveConfigItems writes, followed by its marker.
bool joylogLoadConfigItems(ReplayIntegerLog& log,JoylogConfigItems& items)
{
    if (!joylogConfigBindingsValid(items) || !log.stream || !log.replaying)return false;
    for (unsigned i=0;i<31;++i) {
        const uint32_t value=(i==11 || i==12 || i==24 || i==25)
            ? replayIntegerLogReadUnsigned(&log,32,0) : replayIntegerLogRead(&log,32,0);
        if (i==6)*items.numericOption=static_cast<float>(value);
        else if (items.floatBits[i])std::memcpy(items.floatBits[i],&value,4);
        else *items.integers[i]=static_cast<int32_t>(value);
    }
    (void)replayIntegerLogRead(&log,32,0);
    return log.readFailures==0;
}

// PAL12FD38..12FF58; beta12FE88. There is no second stored config copy.
bool joylogSaveConfigItems(ReplayIntegerLog& log,const JoylogConfigItems& items)
{
    if (!joylogConfigBindingsValid(items))return false;
    for (unsigned i=0;i<31;++i) {
        uint32_t value;
        if (i==6)value=static_cast<uint32_t>(convertFloatToInt32(*items.numericOption));
        else if (items.floatBits[i])std::memcpy(&value,items.floatBits[i],4);
        else value=static_cast<uint32_t>(*items.integers[i]);
        replayIntegerLogWrite(&log,value,32,0);
    }
    replayIntegerLogWrite(&log,0x12341111u,32,0);
    return !log.stream || log.stream->result==BIT_STREAM_OK;
}
