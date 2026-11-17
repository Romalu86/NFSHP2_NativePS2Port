#include "FMenus.hpp"
#include "Flags.hpp"
#include "Language.hpp"
#include <utility>
#include "FEAnim.hpp"
#include "Fred.hpp"
#include "Timer.hpp"
#include "bware/inc/bWare.hpp"
#include "bware/inc/bMath.hpp"
#include "bware/inc/bWare.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace hp2::frontend {
namespace {
// Final PAL31D750/31D780. Fourteen characters and a terminator per row.
constexpr char DefaultMap[3][15]={"abcdefghijklmn","opqrstuvwxyz!.","0123456789# ()"};
constexpr char UpperMap[3][15]={"ABCDEFGHIJKLMN","OPQRSTUVWXYZ!.","0123456789# ()"};
constexpr std::int32_t NameHelp[]={800073,800074,803010,800075,0};
constexpr std::int32_t KeyboardHelp[]={803001,803007,803008,803004,803006,803005,0};
constexpr std::uint32_t NameObjects[]={0x71450a81u,0x71572302u};
constexpr std::uint32_t GenderObjects[]={0xd6f9d954u,0xc33bae35u};
MainMenuScreenServices* nameServices{};

void CopyNameBytes(char* destination,const char* source,std::size_t limit)
{
    // bStrNCpy2535A0: stop after copying the first NUL, do not pad the tail.
    for (std::size_t i=0;i<limit;++i) {
        destination[i]=source[i];
        if (!source[i])break;
    }
}
void SnapCurve(tCubic1D& curve)
{
    curve.mode=0;curve.value=curve.desiredValue;curve.derivative=curve.desiredDerivative;
}
float ObjectHeight(FlailerUiState& ui,FlailerObjectChunk& object)
{
    if (object.kind==1)return ui.resources->ScaledTextHeight(static_cast<FlailerTextChunk&>(object));
    if (object.kind==2)return ui.resources->ScaledBitmapHeight(static_cast<FlailerBitmapChunk&>(object));
    return 0.0f;
}
void ObjectColor(FlailerObjectChunk* object,std::uint32_t color)
{
    if (!object)return;
    if (object->kind==1)static_cast<FlailerTextChunk*>(object)->SetColor(color);
    else if (object->kind==2)static_cast<FlailerBitmapChunk*>(object)->SetColor(color);
}

class tKeyboard final {
public:
    tKeyboard(MainMenuScreenServices& services,std::uint32_t screen)
        : services_(services),screen_(screen),pulse_(0.0f,128.0f,10.0f)
    {
        for (auto& axis:cursor_.axes)tCubic1DInitialize(&axis);
        tCubic1DInitialize(&slide_);
        if (auto* anchor=Ui().resources->FindObject(screen_,0x35b2f256u))
            Ui().resources->GetTopLeft(*anchor,savedX_,savedY_,Ui().language);
        SetVisible(player_,0);
        for (auto& axis:cursor_.axes) {axis.duration=0.10000000149011612f;axis.continuous=0;}
    }
    void SetVisible(std::int32_t player,std::int32_t visible)
    {
        auto& resources=*Ui().resources;
        resources.SetGroupVisibility(screen_,0x670b6fddu,visible);
        if (player==0 || player==1) {
            resources.SetGroupVisibility(screen_,player==0?0x7b95e55cu:0x7deb0dfdu,visible);
            resources.SetObjectVisibility(screen_,NameObjects[player],visible==1?0:1);
        }
        resources.SetObjectVisibility(screen_,0xc01086b5u,visible);
        if(extendedName_) {
            resources.SetGroupVisibility(screen_,player==0?0x7b95e55cu:0x7deb0dfdu,0);
            for(int index=0;index<limit_;++index)
                resources.SetObjectVisibility(screen_,ProfileCell(index),visible);
        }
        if (!visible)resources.MoveGroupTo(screen_,0x670b6fddu,0x35b2f256u,
            static_cast<float>(convertFloatToInt32(savedX_)),static_cast<float>(convertFloatToInt32(savedY_)));
    }
    void Activate(std::int32_t player,bool newProfile=false)
    {
        Require(player==0 || player==1,"Name keyboard has no authored player slot");
        player_=player;profileName_=newProfile;extendedName_=newProfile || services_.pcHost;
        limit_=extendedName_?PLAYER_NAME_CAPACITY-1:7;
        if(extendedName_) {
            auto& resources=*Ui().resources;
            const auto* first=static_cast<const FlailerTextChunk*>(resources.FindObject(screen_,hashString(player_==0?"P1C1":"P2C1"),1));
            Require(first,"Profile editor lacks its first authored character");
            for(int index=0;index<limit_;++index) {
                auto& cell=resources.CloneNativeText(screen_,*first,ProfileCell(index));
                cell.parentKey=0;cell.SetTopLeftX(285.0f+15.0f*static_cast<float>(index));
                cell.SetTopLeftY(profileName_?profileRowY_:first->topLeftY);
            }
        }
        SetVisible(player_,1);nameObject_=NameObjects[player_];
        UpdateCursor();for (auto& axis:cursor_.axes)SnapCurve(axis);
        editPosition_=0;
        slide_.desiredValue=650.0f;slide_.duration=0.5f;slide_.continuous=0;
        SnapCurve(slide_);slide_.desiredValue=savedX_;
        if (slide_.value!=slide_.desiredValue)slide_.mode=2;
        Tick();map_=DefaultMap;edit_.fill(0);
        if(!profileName_)CopyNameBytes(edit_.data(),Profile().playerName,static_cast<std::size_t>(limit_));
        SyncEditWithDisplay();RedrawCharSet();
    }
    void ActivateProfile(float rowY,bool gridAbove)
    {
        profileRowY_=rowY+2.0f;
        profileGridBaseY_=savedY_;
        if(gridAbove)savedY_=66.0f;
        Activate(0,true);
    }
    void SetGridVisible(bool visible) {Ui().resources->SetGroupVisibility(screen_,0x670b6fddu,visible);}
    void Unactivate()
    {
        if(profileName_)savedY_=profileGridBaseY_;
        SetVisible(player_,0);
    }
    float GetColumnX(std::int32_t column) {return GetCenter("CHAR_0%X",column,true);}
    float GetRowY(std::int32_t row) {return GetCenter("CHAR_%X0",row,false);}
    void UpdateCursor()
    {
        bool wrapped=false;
        if (column_<0) {column_=13;wrapped=true;}
        if (column_>13) {column_=0;wrapped=true;}
        if (row_<0) {row_=2;wrapped=true;}
        if (row_>2) {row_=0;wrapped=true;}
        const float desired[]={GetColumnX(column_),GetRowY(row_)};
        tCubic2DSetValDesired(&cursor_,desired);
        if (wrapped)for (auto& axis:cursor_.axes)SnapCurve(axis);
    }
    void Tick()
    {
        auto& ui=Ui();
        if (slide_.mode) {UpdateCursor();for (auto& axis:cursor_.axes)SnapCurve(axis);}
        tCubic2DUpdate(&cursor_,ui.clock->realTimeElapsed);
        if (auto* cursor=ui.resources->FindObject(screen_,0x249db7b7u)) {
            // Both original SetCenter wrappers1446E0/144760 use GetHeight.
            const float half=static_cast<float>(convertFloatToInt32(ObjectHeight(ui,*cursor)))*0.5f;
            cursor->SetTopLeftX(static_cast<float>(convertFloatToInt32(cursor_.axes[0].value))-half);
            cursor->SetTopLeftY(static_cast<float>(convertFloatToInt32(cursor_.axes[1].value))-half);
        }
        tCubic1DUpdate(&slide_,ui.clock->realTimeElapsed,0.0f,0.0f);
        ui.resources->MoveGroupTo(screen_,0x670b6fddu,0x35b2f256u,
            static_cast<float>(convertFloatToInt32(slide_.value)),static_cast<float>(convertFloatToInt32(savedY_)));
        pulse_.Step(ui.clock->realTimeElapsed);
        if (auto* caret=ui.resources->FindObject(screen_,0xc01086b5u))caret->SetAlpha(pulse_.GetValInt());
    }
    void MoveEditText(std::int32_t direction)
    {
        editPosition_=std::clamp(editPosition_+direction,0,extendedName_?limit_:limit_-1);SyncEditWithDisplay();
    }
    char GetChar(std::int32_t row,std::int32_t column) const
    {
        Require(row>=0 && row<3 && column>=0 && column<14,"Name keyboard cursor is outside its actual character map");
        return map_[row][column];
    }
    bool SelectChar(ScreenCallbackData& data)
    {
        const auto character=GetChar(row_,column_);
        if (character==')') {data.sound=29;return false;}
        if (character=='(') {data.sound=26;DeleteCharacter();return true;}
        EnterCharacter(character);
        data.sound=25;return true;
    }
    void EnterCharacter(char character)
    {
        const auto size=std::strlen(edit_.data());
        if(extendedName_) {
            if(size>=static_cast<std::size_t>(limit_))return;
            const auto start=std::min(size,static_cast<std::size_t>(editPosition_));
            std::memmove(edit_.data()+start+1,edit_.data()+start,size-start+1);
            edit_[start]=character;
            editPosition_=static_cast<int>(start)+1;
            SyncEditWithDisplay();return;
        }
        for(auto i=size;i<static_cast<std::size_t>(editPosition_);++i)edit_[i]=' ';
        edit_[static_cast<std::size_t>(editPosition_)]=character;
        Ui().resources->Printf(screen_,nameObject_,"%s",edit_.data());
        MoveEditText(1);
    }
    void DeleteCharacter(bool backspace=false)
    {
        if(extendedName_) {
            const auto size=static_cast<int>(std::strlen(edit_.data()));
            editPosition_=std::min(editPosition_,size);
            if(backspace && editPosition_>0)--editPosition_;
            else if(backspace)return;
            if(editPosition_<size)
                std::memmove(edit_.data()+editPosition_,edit_.data()+editPosition_+1,static_cast<std::size_t>(size-editPosition_));
            SyncEditWithDisplay();return;
        }
        if(backspace && editPosition_>0)--editPosition_;
        for(auto i=editPosition_;i<limit_-1;++i)edit_[static_cast<std::size_t>(i)]=edit_[static_cast<std::size_t>(i+1)];
        edit_[static_cast<std::size_t>(limit_-1)]=0;
        edit_[static_cast<std::size_t>(limit_)]=0;
        Ui().resources->Printf(screen_,nameObject_,"%s",edit_.data());
        SyncEditWithDisplay();if(!backspace)MoveEditText(-1);
    }
    void SyncEditWithDisplay()
    {
        auto& ui=Ui();
        const auto caretIndex=std::min(editPosition_,limit_-1);
        for (std::int32_t i=0;i<limit_;++i) {
            char objectName[16];std::snprintf(objectName,sizeof(objectName),"P%dC%d",player_+1,i+1);
            const auto key=extendedName_?ProfileCell(i):hashString(objectName);
            ui.resources->Printf(screen_,key,"%c",edit_[static_cast<std::size_t>(i)]);
            auto* object=ui.resources->FindObject(screen_,key);
            ObjectColor(object,i==caretIndex?0x80004080u:0x80808080u);
            if (i==caretIndex && object) {
                float x,y;ui.resources->GetTopLeft(*object,x,y,ui.language);
                if (auto* caret=ui.resources->FindObject(screen_,0xc01086b5u)) {
                    caret->SetTopLeftX(x);
                    caret->SetTopLeftY(y+static_cast<float>(convertFloatToInt32(ObjectHeight(ui,*object)))+2.0f);
                }
            }
        }
    }
    void EditWithKeyboard(char character)
    {
        switch(static_cast<NativeMenuTextKey>(character)) {
        case NativeMenuTextKey::Left:MoveEditText(-1);break;
        case NativeMenuTextKey::Right:MoveEditText(1);break;
        case NativeMenuTextKey::Home:MoveEditText(-limit_);break;
        case NativeMenuTextKey::End:MoveEditText(static_cast<int>(std::strlen(edit_.data()))-editPosition_);break;
        case NativeMenuTextKey::Delete:DeleteCharacter();break;
        default:if(character==8)DeleteCharacter(true);else EnterCharacter(character);break;
        }
    }
    void SwitchCharSet() {map_=map_==DefaultMap?UpperMap:DefaultMap;RedrawCharSet();}
    void RedrawCharSet()
    {
        for (std::int32_t row=0;row<3;++row)for (std::int32_t column=0;column<14;++column) {
            char name[20];std::snprintf(name,sizeof(name),"CHAR_%X%X",row,column);
            Ui().resources->Printf(screen_,hashString(name),"%c",GetChar(row,column));
        }
    }
    bool PlayerRight(std::int32_t port) const {return (player_==0 || player_==1) && player_==port;}
    bool HandleEvent(ScreenMessage message,ScreenCallbackData& data)
    {
        // Closed PAL2BF270 table: ScreenEvent13/15 move the edit caret;
        // LeaveScreen commits the same seven-character buffer and consumes Back.
        if (message==ScreenMessage::Tick) {Tick();return true;}
        if (message==ScreenMessage::LeaveScreen) {data.value=0;data.sound=29;Commit();return false;}
        if (message!=ScreenMessage::Select && message!=ScreenMessage::Left && message!=ScreenMessage::Right &&
            message!=ScreenMessage::Previous && message!=ScreenMessage::Next && message!=ScreenMessage::ItemEvent &&
            message!=ScreenMessage::ScreenEvent13 && message!=ScreenMessage::ScreenEvent15)return true;
        if (!PlayerRight(data.joystickPort)) {data.sound=0;return true;}
        switch (message) {
        case ScreenMessage::Left:--column_;data.sound=28;data.value=1;UpdateCursor();return true;
        case ScreenMessage::Right:++column_;data.sound=28;data.value=1;UpdateCursor();return true;
        case ScreenMessage::Previous:--row_;Ui().sound->Play(28);data.value=1;UpdateCursor();return true;
        case ScreenMessage::Next:++row_;Ui().sound->Play(28);data.value=1;UpdateCursor();return true;
        case ScreenMessage::ScreenEvent13:MoveEditText(-1);data.sound=1;return true;
        case ScreenMessage::ScreenEvent15:MoveEditText(1);data.sound=2;return true;
        case ScreenMessage::ItemEvent:SwitchCharSet();data.sound=27;return true;
        case ScreenMessage::Select:if (SelectChar(data))return true;Commit();return false;
        default:return true;
        }
    }
    bool Pointer(ScreenCallbackData& data)
    {
        const float x=static_cast<float>(data.key),y=static_cast<float>(data.value);
        for(int row=0;row<3;++row)for(int column=0;column<14;++column) {
            if(std::abs(x-GetColumnX(column))>14.0f || std::abs(y-GetRowY(row))>14.0f)continue;
            column_=column;row_=row;UpdateCursor();
            if(data.sound && !SelectChar(data)) {
                if(!profileName_)Commit();
                return false;
            }
            return true;
        }
        return true;
    }
    std::string Name() const {return edit_.data();}
    void Commit()
    {
        if(!profileName_) {
            CopyNameBytes(Profile().playerName,edit_.data(),PLAYER_NAME_CAPACITY);
            Ui().resources->SetFittedText(screen_,nameObject_,Ui().language,Profile().playerName);
        }
        Unactivate();
    }
private:
    static std::uint32_t ProfileCell(int index) {return hashString("PC_NAME_1")+static_cast<std::uint32_t>(index);}
    FlailerUiState& Ui() const {return *services_.ui;}
    PlayerSettings& Profile() const {return services_.frontend->players[static_cast<std::size_t>(player_)]->settings;}
    float GetCenter(const char* format,std::int32_t number,bool horizontal)
    {
        char name[20];std::snprintf(name,sizeof(name),format,number);
        std::int32_t left,top,right,bottom;
        Require(Ui().resources->GetBoundingBox(screen_,hashString(name),left,top,right,bottom,Ui().language),
            "Name keyboard lacks its authored row/column anchor");
        return static_cast<float>(horizontal ? left+(right-left)/2 : top+(bottom-top)/2);
    }
    MainMenuScreenServices& services_;
    std::uint32_t screen_;
    std::int32_t player_{},row_{},column_{},editPosition_{};
    std::uint32_t nameObject_{NameObjects[0]};
    tCubic2D cursor_{};
    tCubic1D slide_{};
    std::array<char,16> edit_{};
    std::int32_t limit_{7};
    bool profileName_{},extendedName_{};
    float savedX_{},savedY_{},profileRowY_{},profileGridBaseY_{};
    const char (*map_)[15]{DefaultMap};
    FlailerSineWave pulse_;
};

struct tNameEntryData {
    std::array<std::array<char,PLAYER_NAME_CAPACITY>,2> names{};
    std::array<std::uint8_t,2> genders{};
    void Init(const FrontendPlayerManager& frontend)
    {
        for (std::size_t i=0;i<2;++i) {
            CopyNameBytes(names[i].data(),frontend.players[i]->settings.playerName,names[i].size());
            // Final profile+64B, cPlayerSettings+7 (PreferredGender in the
            // individually retained owner schema). Existing native field.
            genders[i]=frontend.players[i]->settings.authoredTimeDisplayOption;
        }
    }
    bool Different(const FrontendPlayerManager& frontend) const
    {
        for (std::size_t i=0;i<2;++i)
            if (std::strncmp(names[i].data(),frontend.players[i]->settings.playerName,names[i].size())!=0)return true;
        for (std::size_t i=0;i<2;++i)
            if (genders[i]!=frontend.players[i]->settings.authoredTimeDisplayOption)return true;
        return false;
    }
};

class NameEntry final {
public:
    NameEntry(MainMenuScreenServices& services,std::uint32_t screen)
        : services_(services),screen_(screen),keyboard_(services,screen),
          highlight_(*services.ui,screen,0xab671c9au,services.ui->animationSettings.highlightColor),
          selection_(*services.ui,screen,0x1042eac3u,-1),
          arrows_(*services.ui,screen,0xd1cda594u,0x9345e4a7u),
          blurb_(*services.ui,screen,0x75980cf0u,0x75980cf0u,0x70a404ecu)
    {
        keyboard_.SetVisible(0,0);keyboard_.SetVisible(1,0);
        for (std::size_t i=0;i<2;++i)
            services_.ui->resources->SetFittedText(screen_,NameObjects[i],services_.ui->language,
                services_.frontend->players[i]->settings.playerName);
        RedrawGenders();snapshot_.Init(*services_.frontend);
    }
    ~NameEntry()
    {
        if(services_.pcHost)services_.pcHost->SetMenuTextInput(false);
    }
    bool MaybeActivateKeyboard(ScreenCallbackData& data)
    {
        const std::int32_t player=data.key==0x52e039f5u?1:0;
        if (data.joystickPort!=player)return false;
        keyboard_.Activate(player);editing_=true;services_.screens->SetHelpOverride(KeyboardHelp);
        if(services_.pcHost)services_.pcHost->SetMenuTextInput(true);
        return true;
    }
    void ChangeGender(ScreenMessage message,ScreenCallbackData& data)
    {
        const std::size_t player=data.key==0x52e039f6u?1u:0u;
        const bool allowed=data.joystickPort==static_cast<std::int32_t>(player);
        if (allowed) {
            auto& gender=services_.frontend->players[player]->settings.authoredTimeDisplayOption;
            gender=gender==0?1:0;
        }
        RedrawGenders();
        if (!allowed) {data.sound=0;return;}
        if (message==ScreenMessage::Left)data.sound=1;
        if (message==ScreenMessage::Right)data.sound=2;
        selection_.SetCurrentMenuItem(GenderObjects[player]);
    }
    void RedrawGenders()
    {
        // PAL retains the first ID if the second malformed value is neither
        // zero nor one; ordinary mutable profile values are zero/one.
        std::uint32_t id=0;
        for (std::size_t i=0;i<2;++i) {
            const auto gender=services_.frontend->players[i]->settings.authoredTimeDisplayOption;
            if (gender==0)id=160004;
            else if (gender==1)id=160005;
            else if (!i)id=0;
            if (auto* object=services_.ui->resources->FindObject(screen_,GenderObjects[i],1))
                static_cast<FlailerTextChunk*>(object)->SetLanguageID(id);
        }
    }
    void HandleEvent(ScreenMessage message,ScreenCallbackData& data)
    {
        if (message==ScreenMessage::Tick) {highlight_.Animate();selection_.Animate();blurb_.Animate();}
        if(editing_) {
            if(message==ScreenMessage::Pointer) {
                const bool editing=keyboard_.Pointer(data);
                data.key=0xffffffffu;
                if(!editing) {
                    editing_=false;services_.screens->SetHelpOverride(nullptr);
                    if(services_.pcHost)services_.pcHost->SetMenuTextInput(false);
                }
                return;
            }
            if(message==ScreenMessage::Tick && services_.pcHost) {
                for(const char character:services_.pcHost->TakeMenuTextInput()) {
                    if(character!=13 && character!=27) {keyboard_.EditWithKeyboard(character);continue;}
                    keyboard_.Commit();
                    editing_=false;services_.screens->SetHelpOverride(nullptr);
                    services_.pcHost->SetMenuTextInput(false);return;
                }
            }
            if(!keyboard_.HandleEvent(message,data)) {
                editing_=false;services_.screens->SetHelpOverride(nullptr);
                if(services_.pcHost)services_.pcHost->SetMenuTextInput(false);
            }
            return;
        }
        arrows_.HandleEvent(message,data);
        if (message==ScreenMessage::LeaveScreen) {
            if (snapshot_.Different(*services_.frontend) && services_.frontend->autoSave)
                services_.screens->stack.Push("Autosave");
        } else if (message==ScreenMessage::Select) {
            if (data.key==0x52e039f3u || data.key==0x52e039f5u) {
                if (!MaybeActivateKeyboard(data))data.sound=0;
            } else if (data.key==0x52e039f4u || data.key==0x52e039f6u)data.sound=0;
        } else if (message==ScreenMessage::Left || message==ScreenMessage::Right) {
            if (data.key==0x52e039f4u || data.key==0x52e039f6u)ChangeGender(message,data);
        } else if (message==ScreenMessage::Highlight) {
            highlight_.SetCurrentMenuItem(data.key);
            if (data.key>=0x52e039f3u && data.key<=0x52e039f6u) {
                const auto index=data.key-0x52e039f3u;
                const auto target=(index&1u)?GenderObjects[index/2]:0u;
                selection_.SetCurrentMenuItem(target);arrows_.SetCurrentItem(target);
                blurb_.NewText(static_cast<std::int32_t>(150800u+index));
            }
        }
    }
private:
    MainMenuScreenServices& services_;
    std::uint32_t screen_;
    tKeyboard keyboard_;
    tNameEntryData snapshot_;
    FlailerHighlightAnimation highlight_,selection_;
    FlailerLeftRightArrowScroll arrows_;
    FlailerAnimatedBlurb blurb_;
    bool editing_{};
};
std::unique_ptr<NameEntry> nameOwner;
}

struct NativeProfileNameEditor::Storage {
    MainMenuScreenServices& services;
    tKeyboard keyboard;
    std::uint32_t screen{};
    bool grid{};
    Storage(MainMenuScreenServices& owner,std::uint32_t screenKey,float rowY,bool gridAbove)
        :services(owner),keyboard(owner,screenKey),screen(screenKey)
    {
        keyboard.ActivateProfile(rowY,gridAbove);
        SetGrid(owner.pcHost->PromptDevice()!=platform::NativePromptDevice::Keyboard);
        owner.pcHost->SetMenuTextInput(true);
    }
    ~Storage()
    {
        keyboard.Unactivate();
        services.screens->SetHelpOverride(nullptr);
        services.pcHost->SetMenuTextInput(false);
    }
    void SetGrid(bool visible)
    {
        grid=visible;keyboard.SetGridVisible(visible);
        services.screens->SetHelpOverride(visible?KeyboardHelp:nullptr);
    }
};

NativeProfileNameEditor::NativeProfileNameEditor(MainMenuScreenServices& services,std::uint32_t screen,float rowY,bool gridAbove)
    :storage_(std::make_unique<Storage>(services,screen,rowY,gridAbove)) {}
NativeProfileNameEditor::~NativeProfileNameEditor()=default;
bool NativeProfileNameEditor::GridVisible() const noexcept {return storage_->grid;}
std::string NativeProfileNameEditor::Name() const {return storage_->keyboard.Name();}

NativeProfileNameEditor::Result NativeProfileNameEditor::HandleEvent(ScreenMessage message,ScreenCallbackData& data)
{
    auto& state=*storage_;
    if(message==ScreenMessage::LeaveScreen) {data.value=0;return Result::Cancel;}
    if(message==ScreenMessage::ScreenBack) {data.value=1;return Result::Editing;}
    if(message==ScreenMessage::Pointer) {
        if(data.sound) {
            auto& ui=*state.services.ui;
            for(const auto* name:{"PC_BACK_TEXT","PC_BACK_ICON"}) {
                const auto key=hashString(name);
                const auto* hint=ui.resources->FindObject(state.screen,key,1);
                int left{},top{},right{},bottom{};
                const int x=static_cast<int>(data.key);
                if(hint && hint->visible && ui.resources->GetBoundingBox(state.screen,key,left,top,right,bottom,ui.language) &&
                    x>=left-6 && x<=right+6 && data.value>=top-6 && data.value<=bottom+6) {
                    data.key=0xffffffffu;return Result::Cancel;
                }
            }
        }
        const bool accepted=state.grid && !state.keyboard.Pointer(data);
        data.key=0xffffffffu;
        return accepted?Result::Accept:Result::Editing;
    }
    if(message==ScreenMessage::Tick) {
        state.keyboard.Tick();
        for(const char character:state.services.pcHost->TakeMenuTextInput()) {
            if(character==27)return Result::Cancel;
            if(character==13)return Result::Accept;
            if(state.grid)state.SetGrid(false);
            state.keyboard.EditWithKeyboard(character);
        }
        return Result::Editing;
    }
    if(data.joystickPort!=0)return Result::Editing;
    if(message==ScreenMessage::Left || message==ScreenMessage::Right || message==ScreenMessage::Previous ||
        message==ScreenMessage::Next || message==ScreenMessage::Select || message==ScreenMessage::ItemEvent) {
        if(!state.grid)state.SetGrid(true);
        if(message==ScreenMessage::Select)return state.keyboard.SelectChar(data)?Result::Editing:Result::Accept;
    }
    state.keyboard.HandleEvent(message,data);
    return Result::Editing;
}

void BindNameEntryScreen(MainMenuScreenServices& services)
{
    Require(!nameOwner && services.ui && services.ui->resources && services.ui->clock && services.ui->sound &&
        services.ui->language && services.screens && services.screens->ui==services.ui && services.frontend &&
        services.frontend->players[0] && services.frontend->players[1],
        "NameEntry bindings do not share the actual UI/profile owners");
    nameServices=&services;
}
void UnbindNameEntryScreen()
{
    nameOwner.reset();
    if (nameServices && nameServices->screens->overrideHelpItems==KeyboardHelp)
        nameServices->screens->SetHelpOverride(nullptr);
    nameServices=nullptr;
}
void NameEntryScreenCallback(ScreenMessage message,ScreenCallbackData* data)
{
    Require(nameServices && data,"NameEntry callback has no actual bindings/data");
    if (message==ScreenMessage::ScreenActivate) {
        Require(!nameOwner,"NameEntry received duplicate activation");
        nameOwner=std::make_unique<NameEntry>(*nameServices,data->key);
    } else if (message==ScreenMessage::ScreenDeactivate) {
        nameOwner.reset();
        if (nameServices->screens->overrideHelpItems==KeyboardHelp)nameServices->screens->SetHelpOverride(nullptr);
    } else {
        Require(nameOwner!=nullptr,"NameEntry callback precedes activation");
        nameOwner->HandleEvent(message,*data);
    }
}
FlailerScreenConfig fNameEntry{"NameEntry",NameEntryScreenCallback,
    {{static_cast<std::int32_t>(0x617a4e0au),0,0,0}},{{-1,-1,-1,-1}},1,NameHelp};
}
