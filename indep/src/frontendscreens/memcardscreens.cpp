#include "FMenus.hpp"
#include "Fred.hpp"
#include "Language.hpp"
#include "Flags.hpp"
#include "inc/dbFiles.hpp"
#include "bware/inc/bWare.hpp"
#include <algorithm>
#include <cstring>
#include <utility>

namespace hp2::frontend {
namespace {
constexpr const char* ProfileFilename="profile.json";
constexpr const char* ProfileBackupFilename="profile.backup.json";
constexpr const char* ActiveProfileFilename="active-profile.txt";
constexpr std::array<const char*,4> ProfileFiles{{ProfileFilename,"profile-2.json","profile-3.json","profile-4.json"}};
constexpr std::array<const char*,4> ProfileBackups{{ProfileBackupFilename,"profile-2.backup.json",
    "profile-3.backup.json","profile-4.backup.json"}};
constexpr const char* LegacyProfileFilename="settings.db";
std::string ReadPreferredProfile(cMC& files)
{
    std::vector<std::uint8_t> selected;
    if(files.MCReadFile(ActiveProfileFilename,selected,80)!=cMC::Ok)return {};
    if(selected.size()==1 && selected.front()>='1' && selected.front()<='4')
        return std::string("legacy:")+static_cast<char>(selected.front());
    return std::string(selected.begin(),selected.end());
}
int LegacyProfileIndex(const std::string& profile)
{
    if(profile.size()==8 && profile.compare(0,7,"legacy:")==0 && profile[7]>='1' && profile[7]<='4')
        return profile[7]-'1';
    return -1;
}
std::filesystem::path ProfileDirectory(const std::filesystem::path& root,const std::string& profile)
{
    return LegacyProfileIndex(profile)<0?root/"Profiles"/std::filesystem::u8path(profile):root;
}
std::filesystem::path ProfileControlsFile(const std::filesystem::path& root,const std::string& profile)
{
    const int slot=LegacyProfileIndex(profile);
    return ProfileDirectory(root,profile)/(slot<0?"controls.ini":"controls-"+std::to_string(slot+1)+".ini");
}
constexpr std::uint32_t ManualScreen=0x9829bfabu;
constexpr std::uint32_t TransferScreen=0xfa2ac5c6u;
constexpr std::array<std::int32_t,4> LoadSaveHelp{{0xc3546,0xc3547,0xc3548,0}};
MemcardScreens* boundMemcards{};
// PAL245640..2457C0 SavegameData owns the detached transfer, while the
// original live FRED owns progress. PC files now carry named UTF-8 JSON;
// the legacy binary image is accepted only by the migration reader.
class SavegameData final {
public:
    explicit SavegameData(FrontendPlayerManager& frontend):frontend_(frontend) {}
    void* SetupForSave() {text_=frontend_.SerializeNativeProfileText();return text_.data();}
    std::int32_t Size() const {return static_cast<std::int32_t>(text_.size());}
private:
    FrontendPlayerManager& frontend_;
    std::string text_;
};
}

struct MemcardScreens::Storage {
    FrontendPlayerManager& frontend;
    std::unique_ptr<cDBFileSys> files;
    cMC selection;
    SavegameData data;
    std::unique_ptr<cMC> legacyFiles;
    MainMenuScreenServices* menus{};
    std::function<void()> completion;
    std::string error,popupMessage,defaults,activeProfile,preferredProfile;
    std::int32_t legacySlot{};
    bool offerProfiles{};
    enum class Operation {None,Save,Load,Autosave};
    Operation operation{Operation::None};
    Operation retryOperation{Operation::None};
    bool awaitingCallback{},showingError{},returnAfterTick{},creatingBootProfile{},blockedByLoadFailure{};
    LoadResult lastLoad{LoadResult::Absent};
    Storage(FrontendPlayerManager& owner,const char* directory,const char* legacyDirectory)
        :frontend(owner),files(std::make_unique<cDBFileSys>(directory)),selection(directory),data(owner)
    {
        if(legacyDirectory && *legacyDirectory)legacyFiles=std::make_unique<cMC>(legacyDirectory);
        preferredProfile=ReadPreferredProfile(selection);
    }
    const char* Filename() const noexcept {return legacySlot<0?ProfileFilename:ProfileFiles[legacySlot];}
    const char* BackupFilename() const noexcept {return legacySlot<0?ProfileBackupFilename:ProfileBackups[legacySlot];}
    std::filesystem::path Directory(const std::string& profile) const {
        return ProfileDirectory(selection.Directory(),profile);
    }
    static int LegacyIndex(const std::string& profile) {
        return LegacyProfileIndex(profile);
    }
    std::filesystem::path Controls(const std::string& profile) const {
        return ProfileControlsFile(selection.Directory(),profile);
    }
    void ApplyLoadedSettings();
    bool PrepareSave() {
        if(blockedByLoadFailure) {
            error="The previous profile load failed. Restore or correct that file and load it successfully before saving; existing progress was retained.";
            return false;
        }
        try {
            std::vector<std::uint8_t> previous;const auto read=files->ReadFile(Filename(),previous);
            if(read!=cMC::Ok && read!=cMC::NotFound) {
                error=files->LastErrorMessage();
                return false;
            }
            if(read==cMC::Ok && !frontend.ValidateNativeProfileText(previous.data(),previous.size(),error)) {
                error=std::string(Filename())+" was retained because it failed validation: "+error;
                return false;
            }
            // Serialize before changing either durable file. The I/O owner
            // borrows this detached string until its one completion callback.
            void* transfer=data.SetupForSave();
            files->SetBackupFile(read==cMC::Ok?BackupFilename():nullptr,std::move(previous));
            files->SetFileData(Filename(),transfer,data.Size());return true;
        } catch(const std::exception& e) {
            error=e.what();return false;
        }
    }
    static void Completed(void* argument) {static_cast<Storage*>(argument)->awaitingCallback=false;}
    void SetCaption() {
        if(menus && menus->ui && menus->ui->resources)
            menus->ui->resources->SetLanguageID(ManualScreen,0x52e039f5u,frontend.autoSave?0x249f3:0x249f2);
    }
    void ShowError() {
        if(!menus || !menus->screens || !menus->ui || showingError)return;
        popupMessage=std::string(retryOperation==Operation::Load ? "Cannot load the native profile. " :
            "Cannot write the native profile. ")+error+"\nRetry?";
        FlailerPopupConfig popup;popup.sourceNode=menus->screens->active;popup.screenKey=0x037f3133;
        popup.buttonCount=2;popup.flags=3;popup.message=popupMessage.c_str();
        popup.buttonLanguageIds={{0x38277,0x38278,0}};popup.buttonResults={{1,0,0}};
        popup.initialButton=1;popup.joystickPort=-1;
        Require(menus->screens->SummonPopup(popup)>=0,"Profile I/O error has no actual popup owner");showingError=true;
    }
};

MemcardScreens::MemcardScreens(FrontendPlayerManager& frontend,const char* directory,const char* legacyDirectory)
    :storage_(std::make_unique<Storage>(frontend,directory,legacyDirectory)) {}
MemcardScreens::~MemcardScreens()
{
    // A destructor may finish an already authorized atomic write, but never
    // dispatch a game transition through an owner which is being retired.
    storage_->completion={};storage_->files->WaitUntilFunctionCompletes();
    if(boundMemcards==this)boundMemcards=nullptr;
}

MemcardScreens::LoadResult MemcardScreens::LoadProfile()
{
    auto& state=*storage_;state.error.clear();state.retryOperation=Storage::Operation::Load;
    const auto failed=[&](const std::string& reason) {
        state.error=reason;state.blockedByLoadFailure=true;state.lastLoad=LoadResult::Failed;
        return state.lastLoad;
    };
    if(state.files->Busy())return failed("A profile transfer is already active");
    std::vector<std::uint8_t> bytes;auto read=state.files->ReadFile(state.Filename(),bytes);
    if(read==cMC::NotFound) {
        // Only a genuinely absent primary may recover its previous durable
        // text version. An existing malformed file is reported and retained;
        // it must never be mistaken for a fresh profile or silently rolled back.
        read=state.files->ReadFile(state.BackupFilename(),bytes);
        if(read==cMC::Ok) {
            if(!state.frontend.ValidateNativeProfileText(bytes.data(),bytes.size(),state.error))return failed(state.error);
            if(state.files->WriteNewFile(state.Filename(),bytes.data(),bytes.size())!=cMC::Ok)return failed(state.files->LastErrorMessage());
        } else if(read==cMC::NotFound) {
            if(state.legacySlot!=0) {
                state.blockedByLoadFailure=false;state.lastLoad=LoadResult::Absent;return state.lastLoad;
            }
            constexpr auto imageLimit=std::tuple_size_v<FrontendPlayerManager::NativeProfileImage>;
            read=state.files->ReadFile(LegacyProfileFilename,bytes,imageLimit);
            if(read==cMC::NotFound && state.legacyFiles) {
                read=state.legacyFiles->MCReadFile(LegacyProfileFilename,bytes,imageLimit);
                if(read!=cMC::Ok && read!=cMC::NotFound)return failed(state.legacyFiles->LastError());
            }
            if(read==cMC::NotFound) {
                state.blockedByLoadFailure=false;state.lastLoad=LoadResult::Absent;
                return state.lastLoad;
            }
            if(read!=cMC::Ok)return failed(state.files->LastErrorMessage());
            std::string text;
            if(!state.frontend.ConvertLegacyProfileToText(bytes.data(),bytes.size(),text,state.error))return failed(state.error);
            if(state.files->WriteNewFile(state.Filename(),text.data(),text.size())!=cMC::Ok)return failed(state.files->LastErrorMessage());
            bytes.assign(text.begin(),text.end());
        } else return failed(state.files->LastErrorMessage());
    } else if(read!=cMC::Ok)return failed(state.files->LastErrorMessage());
    if(!state.frontend.DeserializeNativeProfileText(bytes.data(),bytes.size(),state.error))return failed(state.error);
    state.blockedByLoadFailure=false;

    state.ApplyLoadedSettings();state.lastLoad=LoadResult::Loaded;return state.lastLoad;
}

void MemcardScreens::Storage::ApplyLoadedSettings()
{
    auto& state=*this;
    if(state.menus && state.menus->ui && state.menus->ui->language &&
       state.menus->ui->language->currentLanguage!=state.frontend.currentLanguage)
        state.menus->ui->language->SetLanguage(state.frontend.currentLanguage);
    if(state.menus && state.menus->sounds) {
        Require(state.menus->raceSetup && state.menus->raceSetup->parameters &&
            state.menus->ui && state.menus->ui->sound,
            "Loaded audio options have no canonical race or speech-volume settings");
        state.frontend.AppendMissingMusicTracks(*state.menus->sounds);
        state.frontend.ApplyAudioOptions(*state.menus->sounds,*state.menus->raceSetup->parameters,*state.menus->ui->sound);
    }
    // Final245D30 load commit restores both saved FRED+178/+17C choices
    // into the existing input router, independently of player inputProfile.
    if(state.menus) {
        Require(state.menus->screens && state.menus->screens->input && state.menus->screens->input->router,
            "Loaded joystick configurations have no canonical input router");
        for(std::int32_t port=0;port<2;++port)
            joystickSetUserConfiguration(state.menus->screens->input->router,port,
                state.frontend.joystickConfigurations[static_cast<std::size_t>(port)]);
    }
    if(state.menus) {
        Require(state.menus->ui && state.menus->ui->clock,
            "Loaded display calibration has no canonical platform clock");
        ApplyDisplayCalibration(state.frontend,*state.menus->ui->clock);
    }
    state.SetCaption();
}


namespace {
bool ValidProfileName(const std::string& name)
{
    if(name.empty() || name.size()>15 || name.front()=='.' || name.front()==' ' ||
        name.back()=='.' || name.back()==' ')return false;
    return std::all_of(name.begin(),name.end(),[](unsigned char c) {
        return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') ||
            c==' ' || c=='-' || c=='_' || c=='.';
    });
}
std::string LowercaseProfile(std::string name)
{
    for(auto& c:name)if(c>='A' && c<='Z')c=static_cast<char>(c-'A'+'a');
    return name;
}
}

const std::string& MemcardScreens::ActiveProfile() const noexcept {return storage_->activeProfile;}
bool MemcardScreens::ChoosingAtStartup() const noexcept
{return storage_->menus && storage_->menus->pcHost && ActiveProfile().empty();}

std::vector<std::string> MemcardScreens::Profiles() const
{
    const auto& state=*storage_;
    const auto& root=state.selection.Directory();
    std::vector<std::string> profiles;
    std::error_code error;
    for(int index=0;index<static_cast<int>(ProfileFiles.size());++index) {
        const auto present=std::filesystem::exists(root/ProfileFiles[index],error) ||
            std::filesystem::exists(root/ProfileBackups[index],error);
        const bool legacy=index==0 && (std::filesystem::exists(root/LegacyProfileFilename,error) ||
            (state.legacyFiles && std::filesystem::exists(state.legacyFiles->Directory()/LegacyProfileFilename,error)));
        if(present || legacy)profiles.push_back("legacy:"+std::to_string(index+1));
    }
    for(std::filesystem::directory_iterator item(root/"Profiles",error),end;!error && item!=end;item.increment(error)) {
        const auto name=item->path().filename().u8string();
        if(ValidProfileName(name) && item->symlink_status(error).type()==std::filesystem::file_type::directory)
            profiles.push_back(name);
    }
    std::sort(profiles.begin(),profiles.end(),[](const std::string& a,const std::string& b) {
        return LowercaseProfile(a)<LowercaseProfile(b);
    });
    const auto& preferred=state.activeProfile.empty()?state.preferredProfile:state.activeProfile;
    const auto first=std::find(profiles.begin(),profiles.end(),preferred);
    if(first!=profiles.end())std::rotate(profiles.begin(),first,first+1);
    return profiles;
}

std::string MemcardScreens::ProfileName(const std::string& profile) const
{
    const auto& state=*storage_;
    const int slot=Storage::LegacyIndex(profile);
    if(slot<0)return profile;
    if(profile==state.activeProfile && state.frontend.players[0])return state.frontend.players[0]->settings.playerName;
    cMC reader(state.selection.Directory().u8string().c_str());
    std::vector<std::uint8_t> data;
    auto result=reader.MCReadFile(ProfileFiles[slot],data);
    if(result==cMC::NotFound)result=reader.MCReadFile(ProfileBackups[slot],data);
    std::string error,name;
    if(result==cMC::Ok && state.frontend.ValidateNativeProfileText(data.data(),data.size(),error,&name) && !name.empty())
        return name;
    return "Player "+std::to_string(slot+1);
}

bool MemcardScreens::CreateProfile(std::string name)
{
    auto& state=*storage_;
    while(!name.empty() && (name.back()==' ' || name.back()=='.'))name.pop_back();
    if(!ValidProfileName(name)) {state.error="Use 1 to 15 letters, digits, spaces, dots or dashes.";return false;}
    const auto folded=LowercaseProfile(name);
    for(const auto& profile:Profiles())
        if(LowercaseProfile(ProfileName(profile))==folded) {state.error="A profile already uses that name.";return false;}
    try {
        std::filesystem::create_directories(state.selection.Directory()/"Profiles");
        if(!std::filesystem::create_directory(state.Directory(name))) {
            state.error="Cannot create the selected profile directory.";return false;
        }
        state.error.clear();return true;
    } catch(const std::exception& error) {state.error=error.what();return false;}
}

bool MemcardScreens::SelectProfile(const std::string& profile)
{
    auto& state=*storage_;
    if(profile.empty())return false;
    if(profile==state.activeProfile)return true;
    if(Busy()) {state.error="A profile transfer is already active";return false;}
    const auto profiles=Profiles();
    if(std::find(profiles.begin(),profiles.end(),profile)==profiles.end() || state.defaults.empty())return false;

    auto* host=state.menus?state.menus->pcHost:nullptr;
    std::optional<NativePcSettings> priorSettings,nextSettings;
    if(host && host->PcSettings()) {
        priorSettings=*host->PcSettings();nextSettings=*priorSettings;
        try {
            if(!state.activeProfile.empty())saveNativeInputSettings(state.Controls(state.activeProfile),priorSettings->input);
            const auto target=state.Controls(profile);
            if(std::filesystem::exists(target))nextSettings->input=loadNativePcSettings(target).input;
            else if(!state.activeProfile.empty() || (!state.preferredProfile.empty() && profile!=state.preferredProfile))
                nextSettings->input=platform::NativeInputSettings{};
        } catch(const std::exception& error) {state.error=error.what();return false;}
    }
    if(!state.activeProfile.empty() && !state.blockedByLoadFailure && !SaveProfile())return false;
    auto replacement=std::make_unique<cDBFileSys>(state.Directory(profile).u8string().c_str());
    const auto previous=state.frontend.SerializeNativeProfileText();
    const auto previousSlot=state.legacySlot;
    const auto previousLoad=state.lastLoad;
    const auto previousBlocked=state.blockedByLoadFailure;
    auto former=std::move(state.files);
    state.files=std::move(replacement);
    state.legacySlot=Storage::LegacyIndex(profile);
    const auto loaded=LoadProfile();
    bool success=loaded==LoadResult::Loaded;
    if(loaded==LoadResult::Absent) {
        success=state.frontend.DeserializeNativeProfileText(state.defaults.data(),state.defaults.size(),state.error);
        if(success) {
            if(state.legacySlot<0) {
                auto& name=state.frontend.players[0]->settings.playerName;
                std::memset(name,0,sizeof(name));
                std::memcpy(name,profile.data(),std::min(profile.size(),sizeof(name)-1));
            }
            state.frontend.autoSave=1;success=SaveProfile();
            if(success)state.ApplyLoadedSettings();
        }
    }
    if(success && state.legacySlot<0 && profile.size()>7) {
        auto& name=state.frontend.players[0]->settings.playerName;
        // Previous versions seeded the driver's name from the first seven
        // letters of the profile directory. Preserve separately edited names.
        if(std::strlen(name)==7 && profile.compare(0,7,name)==0) {
            std::memset(name,0,sizeof(name));
            std::memcpy(name,profile.data(),std::min(profile.size(),sizeof(name)-1));
            if(!SaveProfile())success=false;
        }
    }
    if(success && nextSettings)success=host->SavePcSettings(*nextSettings,state.error);
    if(success) {
        success=state.selection.MCWrite(ActiveProfileFilename,profile.data(),profile.size())==cMC::Ok;
        if(!success)state.error=state.selection.LastError();
    }
    if(!success) {
        auto error=state.error;
        std::string rollbackError;
        if(priorSettings && !host->SavePcSettings(*priorSettings,rollbackError))error+=". "+rollbackError;
        Require(state.frontend.DeserializeNativeProfileText(previous.data(),previous.size(),rollbackError),
            "Cannot restore the validated profile snapshot");
        state.files=std::move(former);
        state.legacySlot=previousSlot;state.lastLoad=previousLoad;state.blockedByLoadFailure=previousBlocked;
        state.ApplyLoadedSettings();state.error=std::move(error);return false;
    }
    state.activeProfile=state.preferredProfile=profile;state.lastLoad=LoadResult::Loaded;
    state.frontend.autoSave=1;
    return true;
}

bool MemcardScreens::DeleteProfile(const std::string& profile)
{
    auto& state=*storage_;
    const auto profiles=Profiles();
    if(Busy() || profile==state.activeProfile ||
        std::find(profiles.begin(),profiles.end(),profile)==profiles.end()) {
        state.error="Select an inactive profile before deleting it.";
        return false;
    }
    namespace fs=std::filesystem;
    const auto& directory=state.selection.Directory();
    const auto suffix=".deleting-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    struct PendingRemoval {fs::path source,staged;bool subtree;};
    std::vector<PendingRemoval> staged;
    const auto move=[&](const fs::path& source,const fs::path& parent,bool subtree=false) {
        const auto status=fs::symlink_status(source);
        if(status.type()==fs::file_type::not_found)return;
        const auto expected=subtree?fs::file_type::directory:fs::file_type::regular;
        if(status.type()!=expected || fs::canonical(source)!=parent/source.filename())
            throw std::runtime_error("Profile path leaves its save directory: "+source.u8string());
        auto pending=parent/(source.filename().u8string()+suffix);
        if(fs::exists(pending))throw std::runtime_error("Profile deletion is already pending");
        staged.push_back({source,std::move(pending),subtree});
        try {fs::rename(source,staged.back().staged);}
        catch(...) {staged.pop_back();throw;}
    };
    const bool removePreferred=profile==state.preferredProfile;
    try {
        const auto root=fs::canonical(directory);
        const int slot=Storage::LegacyIndex(profile);
        if(slot<0)move(state.Directory(profile),root/"Profiles",true);
        else {
            for(const auto* name:{ProfileFiles[slot],ProfileBackups[slot]})move(directory/name,root);
            move(state.Controls(profile),root);
            if(slot==0) {
                move(directory/LegacyProfileFilename,root);
                if(state.legacyFiles) {
                    const auto oldFile=state.legacyFiles->Directory()/LegacyProfileFilename;
                    if(fs::exists(oldFile))move(oldFile,fs::canonical(oldFile.parent_path()));
                }
            }
        }
        if(removePreferred)move(directory/ActiveProfileFilename,root);
    } catch(const std::exception& error) {
        state.error=error.what();
        for(auto it=staged.rbegin();it!=staged.rend();++it) {
            std::error_code rollback;
            fs::rename(it->staged,it->source,rollback);
            if(rollback)state.error+=". "+rollback.message();
        }
        return false;
    }

    if(removePreferred)state.preferredProfile.clear();
    state.error.clear();
    for(const auto& item:staged) {
        std::error_code error;
        if(item.subtree)fs::remove_all(item.staged,error);
        else fs::remove(item.staged,error);
        if(error)state.error="Cannot erase "+item.staged.u8string()+": "+error.message();
    }
    return state.error.empty();
}

bool MemcardScreens::SaveProfileControls(const NativePcSettings& settings)
{
    auto& state=*storage_;
    if(state.activeProfile.empty())return true;
    try {
        saveNativeInputSettings(state.Controls(state.activeProfile),settings.input);
        return true;
    } catch(const std::exception& error) {
        state.error=error.what();return false;
    }
}

bool MemcardScreens::SaveProfile()
{
    auto& state=*storage_;state.error.clear();state.retryOperation=Storage::Operation::Save;
    if(state.files->Busy()){state.error="A profile transfer is already active";return false;}
    if(!state.PrepareSave())return false;
    Require(state.files->SetState(HP2_BMC_FS_SAVE),"Profile saver cannot schedule its idle file owner");
    state.files->Service();
    if(state.files->lastError!=cMC::Ok) {
        state.error=state.files->LastErrorMessage();return false;
    }

    state.SetCaption();return true;
}

bool MemcardScreens::RequestAutosave(std::function<void()> completed)
{
    auto& state=*storage_;
    if(state.files->Busy() || state.operation!=Storage::Operation::None)return false;
    state.error.clear();state.completion=std::move(completed);state.operation=Storage::Operation::Autosave;
    state.retryOperation=Storage::Operation::Autosave;
    if(!state.PrepareSave())return true;
    state.awaitingCallback=true;
    if(!state.files->SetState(HP2_BMC_FS_SAVE,Storage::Completed,&state)) {
        state.awaitingCallback=false;state.error="The file owner rejected the autosave transaction";
    }
    return true;
}

void MemcardScreens::Service()
{
    auto& state=*storage_;
    if(state.files->Busy())state.files->Service();
    if(state.operation!=Storage::Operation::Autosave || state.awaitingCallback)return;
    if(!state.error.empty() || state.files->lastError!=cMC::Ok) {
        if(state.error.empty()) {
            state.error=state.files->LastErrorMessage();
        }
        state.ShowError();return;
    }
    // Completion is detached before the original transition. It may retire
    // a screen or this entire application owner; no member is read afterward.

    auto completion=std::move(state.completion);state.operation=Storage::Operation::None;
    if(completion)completion();
}

bool MemcardScreens::Busy() const noexcept
{return storage_->files->Busy() || storage_->operation!=Storage::Operation::None;}
const std::string& MemcardScreens::LastError() const noexcept {return storage_->error;}

void MemcardScreens::HandleEvent(ScreenMessage message,ScreenCallbackData& payload)
{
    auto& state=*storage_;
    Require(state.menus && state.menus->screens,"Memcard screen has no actual frontend screen owner");
    auto& screens=*state.menus->screens;
    const auto* name=screens.active && screens.active->configuration?screens.active->configuration->name:nullptr;
    Require(name,"Memcard callback has no original screen configuration");
    const bool boot=!std::strcmp(name,"BootAutoload"),automatic=!std::strcmp(name,"Autosave");
    // Explicit native-PC first-run policy: only a genuinely absent profile
    // enables autosave. Existing preferences and invalid files are retained.
    const auto loadBootProfile=[this,&state] {
        const auto loaded=LoadProfile();state.returnAfterTick=loaded==LoadResult::Loaded;
        if(loaded==LoadResult::Failed) {state.ShowError();return;}
        if(loaded==LoadResult::Absent) {
            state.creatingBootProfile=true;state.frontend.autoSave=1;
            Require(RequestAutosave([this] {
                storage_->creatingBootProfile=false;storage_->returnAfterTick=true;
            }),"First-run profile cannot schedule its actual file owner");
        }
    };
    if(message==ScreenMessage::ScreenActivate) {
        if(boot) {
            if(state.defaults.empty())state.defaults=state.frontend.SerializeNativeProfileText();
            if(ChoosingAtStartup())state.offerProfiles=true;
            else loadBootProfile();
        } else if(automatic && !Busy()) {
            Require(RequestAutosave([this] {
                auto* menus=storage_->menus;
                Require(menus && menus->screens->MoveToPreviousScreen(1,false,-1),"Autosave has no previous original screen");
            }),"Autosave screen could not schedule its transfer");
        } else state.SetCaption();
        return;
    }
    if(message==ScreenMessage::Tick) {
        if(boot && state.offerProfiles) {
            state.offerProfiles=false;
            OpenStartupProfileMenu(*state.menus);
            return;
        }
        if(state.returnAfterTick) {
            state.returnAfterTick=false;
            Require(screens.MoveToPreviousScreen(1,false,-1),"Boot autoload has no next original startup screen");return;
        }
        Service();return;
    }
    if(message==ScreenMessage::PopupDismiss && state.showingError) {
        state.showingError=false;
        if(payload.value) {
            if(boot && !state.creatingBootProfile)loadBootProfile();
            else if(automatic || (boot && state.creatingBootProfile)) {
                auto continuation=std::move(state.completion);state.operation=Storage::Operation::None;
                Require(RequestAutosave(std::move(continuation)),"Retry cannot schedule its actual file owner");
            } else if(state.retryOperation==Storage::Operation::Load) {
                const auto loaded=LoadProfile();
                if(loaded==LoadResult::Absent)state.error="No saved native profile exists";
                if(loaded!=LoadResult::Loaded)state.ShowError();
            } else if(!SaveProfile())state.ShowError();
        } else {
            // Explicit user cancellation carries no save success and cannot
            // run a deferred coordinator action. Return to the prior screen.
            state.completion={};state.operation=Storage::Operation::None;
            if(state.creatingBootProfile) {state.creatingBootProfile=false;state.frontend.autoSave=0;}
            if(automatic || boot)Require(screens.MoveToPreviousScreen(1,false,-1),"Cancelled profile transfer has no prior screen");
        }
        return;
    }
    if(message==ScreenMessage::LeaveScreen && Busy()){payload.value=0;payload.sound=0;return;}
    if(automatic || boot)return;
    if(message==ScreenMessage::Select) {
        if(payload.key==0x52e039f3u) {if(!SaveProfile())state.ShowError();}
        else if(payload.key==0x52e039f4u) {
            const auto loaded=LoadProfile();
            if(loaded==LoadResult::Absent)state.error="No saved native profile exists";
            if(loaded!=LoadResult::Loaded)state.ShowError();
        }
    } else if((message==ScreenMessage::Left || message==ScreenMessage::Right) && payload.key==0x52e039f5u) {
        state.frontend.autoSave=!state.frontend.autoSave;state.SetCaption();payload.sound=2;
        if(state.frontend.autoSave && !SaveProfile())state.ShowError();
    }
}

void BindMemcardScreens(MemcardScreens& owner,MainMenuScreenServices& services)
{
    Require(!boundMemcards || boundMemcards==&owner,"Another MemcardScreens owner is already bound");
    Require(services.frontend==&owner.storage_->frontend && services.ui && services.screens,
        "Memcard screen binding is not the live application profile/UI");
    owner.storage_->menus=&services;boundMemcards=&owner;
}
void UnbindMemcardScreens() {boundMemcards=nullptr;}

bool RequestProfileAutosave(std::function<void()> completed,std::function<bool()> ownerAlive)
{
    Require(boundMemcards,"Autosave has no actual native profile/file owner");
    auto* owner=boundMemcards;auto* menus=owner->storage_->menus;
    Require(menus && menus->screens,"Autosave continuation has no actual original screen stack");
    const auto queued=owner->RequestAutosave([menus,completed=std::move(completed),ownerAlive=std::move(ownerAlive)]() mutable {
        // An authorised write may finish after its phase retired. Guard the
        // old screen stack before any access, not just the final continuation.
        if(ownerAlive && !ownerAlive())return;
        const auto* node=menus->screens->active;
        if(node && node->configuration && !std::strcmp(node->configuration->name,"Autosave"))
            Require(menus->screens->MoveToPreviousScreen(1,false,-1),"Completed autosave has no retained previous screen");
        if(completed)completed();
    });
    if(queued)menus->screens->ChangeToScreen("Autosave",true);
    return queued;
}

std::array<FlailerScreenConfig,3>& MemcardScreenConfigurations()
{
    // Actual PAL247DE8 constructor arguments, callbacks and final strings.
    // PC I/O completion replaces the Sony polling states, never fake success.
    static std::array<FlailerScreenConfig,3> configurations{{
        {"Phils Load Save",MemcardScreenCallback,{{static_cast<std::int32_t>(ManualScreen),0,0,0}},{{-1,-1,-1,-1}},1,LoadSaveHelp.data()},
        {"BootAutoload",MemcardScreenCallback,{{static_cast<std::int32_t>(TransferScreen),0,0,0}},{{-1,-1,-1,-1}},1,nullptr},
        {"Autosave",MemcardScreenCallback,{{static_cast<std::int32_t>(TransferScreen),0,0,0}},{{-1,-1,-1,-1}},1,nullptr}
    }};
    return configurations;
}
void MemcardScreenCallback(ScreenMessage message,ScreenCallbackData* data)
{
    Require(boundMemcards && data,"Memcard callback has no retained process owner/payload");
    boundMemcards->HandleEvent(message,*data);
}
} // namespace hp2::frontend
