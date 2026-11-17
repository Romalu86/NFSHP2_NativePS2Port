// Included by the native Windows platform owner, inside namespace hp2.
// All controls edit the same NativePcSettings serialized by Config.cpp.
class NativeConfigurator final {
public:
    NativeConfigurator(NativePcSettings& settings,const NativeProcessLaunch& launch,std::filesystem::path file)
        :settings_(settings),draft_(settings),launch_(launch),file_(std::move(file)){}
    ~NativeConfigurator()
    {
        if(window_ && IsWindow(window_))DestroyWindow(window_);
        if(font_)DeleteObject(font_);
        if(headingFont_)DeleteObject(headingFont_);
        if(class_)UnregisterClassW(L"NFSHP2Configurator",GetModuleHandleW(nullptr));
    }
    bool Run()
    {
        // Only tabs are common controls here. BUTTON/EDIT/STATIC/LISTBOX/
        // COMBOBOX belong to USER32; requesting their v6 registration breaks
        // startup when the executable uses the default ComCtl32 v5 context.
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_TAB_CLASSES};
        Require(InitCommonControlsEx(&controls)!=FALSE,"Could not initialize native settings controls");
        (void)SetProcessDPIAware();
        const auto monitor=NativeChooseMonitor(draft_.monitorDevice);
        const RECT work=monitor.work;
        const auto dc=GetDC(nullptr);Require(dc!=nullptr,"Could not query desktop DPI");
        dpi_=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(nullptr,dc);
        // Fit the complete window, including the actual Windows caption/frame.
        // A fixed pixel allowance can clip the footer on a high-DPI desktop.
        RECT frame{};
        Require(AdjustWindowRectEx(&frame,WindowStyle,FALSE,WS_EX_CONTROLPARENT)!=FALSE,
            "Could not measure native settings window frame");
        const int clientWidth=std::max(1,static_cast<int>((work.right-work.left)-(frame.right-frame.left)-24));
        const int clientHeight=std::max(1,static_cast<int>((work.bottom-work.top)-(frame.bottom-frame.top)-24));
        dpi_=std::max(64,std::min({dpi_,clientWidth*96/800,clientHeight*96/560}));
        font_=CreateFontW(-Scale(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        headingFont_=CreateFontW(-Scale(22),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        Require(font_ && headingFont_,"Could not create native settings fonts");
        WNDCLASSEXW type{};type.cbSize=sizeof(type);type.lpfnWndProc=Procedure;
        type.hInstance=GetModuleHandleW(nullptr);type.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        type.hbrBackground=GetSysColorBrush(COLOR_BTNFACE);type.lpszClassName=L"NFSHP2Configurator";
        NativeSetApplicationIcons(type);
        class_=RegisterClassExW(&type);Require(class_!=0,"Could not register native settings window");
        RECT extent{0,0,Scale(800),Scale(560)};
        Require(AdjustWindowRectEx(&extent,WindowStyle,FALSE,WS_EX_CONTROLPARENT)!=FALSE,
            "Could not calculate native settings window size");
        const int width=extent.right-extent.left,height=extent.bottom-extent.top;
        window_=CreateWindowExW(WS_EX_CONTROLPARENT,type.lpszClassName,L"NFSHP2 - Launcher",
            WindowStyle,work.left+(work.right-work.left-width)/2,
            work.top+(work.bottom-work.top-height)/2,width,height,nullptr,nullptr,type.hInstance,this);
        if(!window_ && creationFailure_)std::rethrow_exception(creationFailure_);
        Require(window_!=nullptr,"Could not create native settings window");
        ShowWindow(window_,SW_SHOW);RedrawControls();SetFocus(GetDlgItem(window_,IDOK));
        MSG message{};
        while(!done_) {
            const auto result=GetMessageW(&message,nullptr,0,0);
            if(result<=0) {if(result<0)throw std::runtime_error("Settings message loop failed");break;}
            if(!IsDialogMessageW(window_,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
        }
        return startGame_;
    }
private:
    enum Id:int {
        Tab=100,Monitor,Adapter,Resolution,Fullscreen,SkipMovies,
        Filter,Antialiasing,Anisotropy,DrawDistance,MirrorDistance,OriginalGraphics,
        RenderScale,AspectRatio,Scaling,Detail,Reflections,Brightness,Contrast,Gamma,
        Debug,Unlock,RacerNitro,TrafficVehicles,DisableCatchUp,CockpitView,
        RestoreCutTraffic,RandomNfsOpponents,OpenProfile,
        Status,Save,Defaults
    };
    static constexpr DWORD WindowStyle=NativeFixedWindowStyle|WS_CLIPCHILDREN|WS_CLIPSIBLINGS;
    int Scale(int value) const {return MulDiv(value,dpi_,96);}
    HWND Control(int page,const wchar_t* type,const wchar_t* label,DWORD style,int x,int y,int width,int height,int id=0)
    {
        // Page widgets remain hidden until their values and selected page are
        // ready. All widgets share this parent, so sibling clipping is needed.
        const DWORD visibility=page<0?WS_VISIBLE:0;
        auto* child=CreateWindowExW(0,type,label,WS_CHILD|WS_CLIPSIBLINGS|visibility|style,Scale(x),Scale(y),Scale(width),Scale(height),
            window_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        if(!child) {
            const auto error=GetLastError();
            throw std::runtime_error("Could not create native settings control (class="+
                std::filesystem::path(type).u8string()+", id="+std::to_string(id)+
                ", Win32="+std::to_string(error)+")");
        }
        SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);
        if(page>=0)pages_[static_cast<std::size_t>(page)].push_back(child);
        return child;
    }
    void Label(int page,const wchar_t* text,int x,int y,int width,int height=24,int id=0)
    {Control(page,L"STATIC",text,SS_LEFT|SS_NOPREFIX,x,y,width,height,id);}
    HWND Combo(int page,int id,int x,int y,int width,std::initializer_list<const wchar_t*> values)
    {
        auto* child=Control(page,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,x,y,width,230,id);
        for(const auto* value:values)SendMessageW(child,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(value));
        return child;
    }
    void Button(int page,int id,const wchar_t* text,int x,int y,int width,int height=28)
    {Control(page,L"BUTTON",text,WS_TABSTOP|BS_PUSHBUTTON,x,y,width,height,id);}
    void CheckBox(int page,int id,const wchar_t* text,int x,int y,int width)
    {Control(page,L"BUTTON",text,WS_TABSTOP|BS_AUTOCHECKBOX,x,y,width,24,id);}
    void Edit(int page,int id,int x,int y,int width)
    {
        const auto child=Control(page,L"EDIT",L"",WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,x,y,width,25,id);
        SendMessageW(child,EM_SETLIMITTEXT,64,0);
    }
    void SetText(int id,const std::wstring& value) const {SetDlgItemTextW(window_,id,value.c_str());}
    std::wstring Text(int id) const
    {
        const auto child=GetDlgItem(window_,id);
        const auto length=std::clamp(GetWindowTextLengthW(child),0,4096);
        std::wstring value(static_cast<std::size_t>(length)+1,L'\0');
        const auto count=GetWindowTextW(child,value.data(),length+1);
        value.resize(static_cast<std::size_t>(std::max(0,count)));return value;
    }
    void CheckValue(int id,bool value) const {SendDlgItemMessageW(window_,id,BM_SETCHECK,value?BST_CHECKED:BST_UNCHECKED,0);}
    bool Checked(int id) const {return SendDlgItemMessageW(window_,id,BM_GETCHECK,0,0)==BST_CHECKED;}
    void Select(int id,int value) const {SendDlgItemMessageW(window_,id,CB_SETCURSEL,static_cast<WPARAM>(value),0);}
    int Selected(int id) const {return static_cast<int>(SendDlgItemMessageW(window_,id,CB_GETCURSEL,0,0));}
    void AddCombo(int id,const std::wstring& value) const {SendDlgItemMessageW(window_,id,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(value.c_str()));}
    void StatusText(const std::wstring& value) const {SetText(Status,value);}
    static std::wstring Number(double value)
    {
        std::wostringstream stream;stream.imbue(std::locale::classic());stream.precision(4);stream<<value;return stream.str();
    }
    static std::wstring DistanceNumber(float value)
    {
        std::wostringstream stream;stream.imbue(std::locale::classic());
        stream.precision(std::numeric_limits<float>::max_digits10);stream<<value;return stream.str();
    }
    static std::wstring AspectLabel(std::pair<int,int> ratio)
    {
        if(!ratio.first)return L"Automatic";
        return std::to_wstring(ratio.first)+L":"+std::to_wstring(ratio.second);
    }
    double Numeric(int id,const char* name,double minimum,double maximum) const
    {
        auto value=Text(id);std::replace(value.begin(),value.end(),L',',L'.');
        std::wistringstream stream(value);stream.imbue(std::locale::classic());double number{};wchar_t extra{};
        if(!(stream>>number) || stream>>extra || !std::isfinite(number) || number<minimum || number>maximum)
            throw std::invalid_argument(std::string(name)+" must be between "+std::to_string(minimum)+" and "+std::to_string(maximum));
        return number;
    }
    int WholeNumber(int id,const char* name,int minimum,int maximum) const
    {
        const auto number=Numeric(id,name,minimum,maximum);
        if(std::floor(number)!=number)throw std::invalid_argument(std::string(name)+" must be a whole number");
        return static_cast<int>(number);
    }
    void CreateControls()
    {
        auto* title=Control(-1,L"STATIC",L"Need for Speed Hot Pursuit 2",SS_LEFT|SS_NOPREFIX,22,20,750,30);
        SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(headingFont_),TRUE);
        auto* tabs=Control(-1,WC_TABCONTROLW,L"",WS_TABSTOP,18,67,764,417,Tab);
        constexpr const wchar_t* titles[]{L"Display",L"Graphics",L"Advanced Settings"};
        for(std::size_t i=0;i<std::size(titles);++i) {
            TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=const_cast<wchar_t*>(titles[i]);
            SendMessageW(tabs,TCM_INSERTITEMW,static_cast<WPARAM>(i),reinterpret_cast<LPARAM>(&item));
        }
        CreateDisplay();CreateGraphics();CreateAdvanced();
        Label(-1,L"",24,489,748,30,Status);
        Button(-1,Defaults,L"Defaults",22,522,139);
        Button(-1,Save,L"Save",432,522,108);
        Button(-1,IDCANCEL,L"Cancel",550,522,100);
        Control(-1,L"BUTTON",launch_.configureOnly?L"Done":L"Play",WS_TABSTOP|BS_DEFPUSHBUTTON,660,522,118,28,IDOK);
        // New child HWNDs start below older siblings. The tab was created
        // first, so explicitly put its background behind every page widget.
        // Clipping alone would otherwise preserve the tab covering the fields.
        Require(SetWindowPos(tabs,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)!=FALSE,
            "Could not arrange native settings pages");
        LoadControls();
        loading_=false;ShowPage(0);
    }
    void CreateDisplay()
    {
        Label(0,L"Monitor",40,128,180);Combo(0,Monitor,230,123,518,{});
        Label(0,L"Video adapter",40,168,180);Combo(0,Adapter,230,163,518,{});
        Label(0,L"Resolution",40,208,180);Combo(0,Resolution,230,203,518,{});
        CheckBox(0,Fullscreen,L"Fullscreen",230,247,248);
        CheckBox(0,SkipMovies,L"Skip intro movies",500,247,248);
    }
    void CreateGraphics()
    {
        Label(1,L"Render Scale",38,123,174);Combo(1,RenderScale,220,118,158,{L"Native",L"1x",L"2x",L"3x",L"4x",L"5x",L"6x",L"7x",L"8x"});
        Label(1,L"Race Aspect Ratio",405,123,164);
        Control(1,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWN|WS_VSCROLL,576,118,172,230,AspectRatio);
        Label(1,L"Scaling",38,164,174);Combo(1,Scaling,220,159,158,{L"Smooth",L"Sharp"});
        Label(1,L"Antialiasing",405,164,164);
        Combo(1,Antialiasing,576,159,172,{L"Off",L"MSAA 2x",L"MSAA 4x",L"MSAA 8x",L"SMAA 2x",L"SMAA 4x"});
        Label(1,L"Texture Filtering",38,205,174);
        Combo(1,Filter,220,200,158,{L"Original",L"Bilinear",L"Trilinear",L"Anisotropic"});
        Label(1,L"Anisotropic Filtering",405,205,164);Combo(1,Anisotropy,576,200,172,{L"2x",L"4x",L"8x",L"16x"});
        Label(1,L"Draw Distance (x)",38,246,174);Edit(1,DrawDistance,220,241,158);
        Label(1,L"Mirror Distance (x)",405,246,164);Edit(1,MirrorDistance,576,241,172);
        Label(1,L"Level of Detail",38,287,174);Combo(1,Detail,220,282,158,{L"Original",L"Maximum"});
        Label(1,L"Reflections",405,287,164);Combo(1,Reflections,576,282,172,{L"Original",L"Advanced",L"High"});
        Label(1,L"Brightness",38,328,174);Edit(1,Brightness,220,323,158);
        Label(1,L"Contrast (%)",405,328,164);Edit(1,Contrast,576,323,172);
        Label(1,L"Gamma",38,369,174);Edit(1,Gamma,220,364,158);
        Button(1,OriginalGraphics,L"Original graphics preset",505,433,243);
    }
    void CreateAdvanced()
    {
        constexpr int page=2;
        CheckBox(page,Debug,L"Enable debug menu and debug cameras",40,123,342);
        CheckBox(page,Unlock,L"Unlock obtainable cars, tracks and events",400,123,348);
        CheckBox(page,RacerNitro,L"Enable player nitro in ordinary races",40,163,342);
        CheckBox(page,RandomNfsOpponents,L"Include NFS Edition in random rivals",400,163,348);
        CheckBox(page,TrafficVehicles,L"Allow driving traffic vehicles",40,203,342);
        CheckBox(page,RestoreCutTraffic,L"Restore unused traffic vehicles",400,203,348);
        CheckBox(page,DisableCatchUp,L"Disable Catch Up in all race modes",40,243,342);
        CheckBox(page,CockpitView,L"Cockpit view for supported cars",400,243,348);
        Label(page,L"Saves and settings",40,312,565);
        Control(page,L"STATIC",launch_.profileDirectory.c_str(),SS_PATHELLIPSIS|SS_NOPREFIX,40,347,560,24);
        Button(page,OpenProfile,L"Open folder",616,342,132);
    }

    struct Loading final {
        bool& value;bool previous;
        explicit Loading(bool& flag):value(flag),previous(flag){value=true;}
        ~Loading(){value=previous;}
    };
    void RefreshMonitorsList()
    {
        Loading guard(loading_);
        monitors_=NativeWindowsMonitors();monitorKeys_.clear();monitorKeys_.push_back("");
        SendDlgItemMessageW(window_,Monitor,CB_RESETCONTENT,0,0);AddCombo(Monitor,L"Primary monitor (automatic)");
        int chosen=draft_.monitorDevice.empty()?0:-1;
        for(const auto& monitor:monitors_) {
            AddCombo(Monitor,monitor.name);monitorKeys_.push_back(monitor.id);
            if(NativeMonitorMatches(monitor,draft_.monitorDevice))chosen=static_cast<int>(monitorKeys_.size())-1;
        }
        if(chosen<0) {
            AddCombo(Monitor,L"Unavailable saved monitor - "+NativeWideText(draft_.monitorDevice));
            monitorKeys_.push_back(draft_.monitorDevice);chosen=static_cast<int>(monitorKeys_.size())-1;
        }
        Select(Monitor,chosen);RefreshResolutionModes();
    }
    void RefreshResolutionModes()
    {
        const auto selected=Selected(Monitor);
        const auto key=selected>=0 && static_cast<std::size_t>(selected)<monitorKeys_.size()?monitorKeys_[static_cast<std::size_t>(selected)]:std::string{};
        const auto monitor=NativeChooseMonitor(key);
        resolutionModes_=NativeWindowsResolutions(monitor);
        if(resolutionModes_.empty()) {
            const auto [width,height]=NativeWindowsMonitorResolution(monitor);
            resolutionModes_.emplace_back(static_cast<int>(std::clamp<DWORD>(width,320,16384)),
                static_cast<int>(std::clamp<DWORD>(height,240,16384)));
        }
        Loading guard(loading_);
        SendDlgItemMessageW(window_,Resolution,CB_RESETCONTENT,0,0);
        auto preferred=std::make_pair(draft_.width,draft_.height);
        if(draft_.fullscreen) {
            preferred=draft_.fullscreenSize;
            if(!preferred.first)preferred={monitor.display.right-monitor.display.left,monitor.display.bottom-monitor.display.top};
        }
        const auto current=std::find(resolutionModes_.begin(),resolutionModes_.end(),preferred);
        for(const auto& [width,height]:resolutionModes_)
            AddCombo(Resolution,std::to_wstring(width)+L" x "+std::to_wstring(height));
        Select(Resolution,static_cast<int>((current==resolutionModes_.end()?resolutionModes_.end()-1:current)-resolutionModes_.begin()));
        const auto aspect=Text(AspectRatio);
        SendDlgItemMessageW(window_,AspectRatio,CB_RESETCONTENT,0,0);
        for(const auto& ratio:nativeAspectChoices(resolutionModes_))AddCombo(AspectRatio,AspectLabel(ratio));
        if(!aspect.empty())SetText(AspectRatio,aspect);
    }
    void ReadResolution(NativePcSettings& settings)
    {
        const auto selected=Selected(Resolution);
        if(selected<0 || static_cast<std::size_t>(selected)>=resolutionModes_.size())throw std::invalid_argument("Select a resolution");
        const auto& size=resolutionModes_[static_cast<std::size_t>(selected)];
        std::tie(settings.width,settings.height)=size;
        settings.fullscreenSize=size;
    }
    void RefreshAdaptersList()
    {
        Loading guard(loading_);
        SendDlgItemMessageW(window_,Adapter,CB_RESETCONTENT,0,0);
        adapterKeys_.assign(1,{});
        AddCombo(Adapter,L"Automatic (prefer discrete graphics)");
        int chosen=draft_.videoAdapter.empty() ? 0:-1;
        std::vector<EcstasyVulkanAdapter> adapters;
        try {adapters=ecstasyVulkanAdapters();}
        catch(const std::exception& error) {
            StatusText(L"Vulkan adapter discovery failed. "+NativeWideText(error.what()));
        }
        for(const auto& adapter:adapters) {
            auto label=NativeWideText(adapter.name);
            if(std::count_if(adapters.begin(),adapters.end(),[&](const auto& item){return item.name==adapter.name;})>1)
                label+=L" ("+NativeWideText(adapter.id.substr(0,8))+L")";
            AddCombo(Adapter,label);
            adapterKeys_.push_back(adapter.id);
            if(adapter.id==draft_.videoAdapter)chosen=static_cast<int>(adapterKeys_.size())-1;
        }
        if(chosen<0) {
            AddCombo(Adapter,L"Unavailable saved adapter");
            adapterKeys_.push_back(draft_.videoAdapter);
            chosen=static_cast<int>(adapterKeys_.size())-1;
        }
        Select(Adapter,chosen);
    }
    void LoadGraphics()
    {
        Loading guard(loading_);
        Select(Filter,static_cast<int>(draft_.textureFilter));
        const auto aa=static_cast<int>(draft_.antialiasing);Select(Antialiasing,aa<2?0:aa-1);
        Select(Anisotropy,draft_.anisotropy==2?0:draft_.anisotropy==4?1:draft_.anisotropy==8?2:3);
        EnableWindow(GetDlgItem(window_,Anisotropy),draft_.textureFilter==NativeTextureFilter::Anisotropic);
        Select(RenderScale,draft_.renderScale);SetText(AspectRatio,AspectLabel(draft_.raceAspect));Select(Scaling,draft_.sharpScaling?1:0);
        Select(Detail,draft_.render.maximumDetail?1:0);Select(Reflections,draft_.render.reflections);
        SetText(Brightness,std::to_wstring(draft_.render.brightness));SetText(Contrast,std::to_wstring(draft_.render.contrast));
        SetText(Gamma,Number(draft_.render.gamma/100.0));
        SetText(DrawDistance,DistanceNumber(draft_.render.drawDistanceScale));SetText(MirrorDistance,DistanceNumber(draft_.render.mirrorDistanceScale));
    }
    void LoadControls()
    {
        Loading guard(loading_);
        RefreshMonitorsList();RefreshAdaptersList();
        CheckValue(Fullscreen,draft_.fullscreen);
        CheckValue(SkipMovies,draft_.skipMovies);
        LoadGraphics();
        CheckValue(Debug,draft_.debugTools);CheckValue(Unlock,draft_.unlockAll);
        CheckValue(RacerNitro,draft_.gameplay.racerNitro);CheckValue(TrafficVehicles,draft_.gameplay.trafficVehicles);
        CheckValue(DisableCatchUp,draft_.gameplay.disableCatchUp);CheckValue(CockpitView,draft_.gameplay.cockpitView);
        CheckValue(RestoreCutTraffic,draft_.gameplay.restoreCutTraffic);CheckValue(RandomNfsOpponents,draft_.gameplay.randomNfsEditionOpponents);
    }
    NativePcSettings ReadControls()
    {
        auto result=draft_;
        ReadResolution(result);
        const auto monitor=Selected(Monitor);
        if(monitor<0 || static_cast<std::size_t>(monitor)>=monitorKeys_.size())throw std::invalid_argument("Select a monitor");
        result.monitorDevice=monitorKeys_[static_cast<std::size_t>(monitor)];
        const auto adapter=Selected(Adapter);
        if(adapter<0 || static_cast<std::size_t>(adapter)>=adapterKeys_.size())throw std::invalid_argument("Select a video adapter");
        result.videoAdapter=adapterKeys_[static_cast<std::size_t>(adapter)];
        result.fullscreen=Checked(Fullscreen);result.skipMovies=Checked(SkipMovies);
        const auto filter=Selected(Filter),aa=Selected(Antialiasing),af=Selected(Anisotropy);
        if(filter<0 || filter>3 || aa<0 || aa>5 || af<0 || af>3)
            throw std::invalid_argument("Select supported graphics options");
        result.textureFilter=static_cast<NativeTextureFilter>(filter);result.antialiasing=static_cast<NativeAntialiasing>(aa==0?0:aa+1);
        result.anisotropy=2<<af;
        result.render.drawDistanceScale=static_cast<float>(Numeric(DrawDistance,"World draw distance",1,4));
        result.render.mirrorDistanceScale=static_cast<float>(Numeric(MirrorDistance,"Rear mirror distance",0.5,4));
        result.renderScale=Selected(RenderScale);result.raceAspect={};
        if(Text(AspectRatio)!=L"Automatic") {
            std::wistringstream aspect(Text(AspectRatio));wchar_t colon{},extra{};
            if(!(aspect>>result.raceAspect.first>>colon>>result.raceAspect.second) || colon!=L':' || aspect>>extra ||
                result.raceAspect.first<=0 || result.raceAspect.second<=0)
                throw std::invalid_argument("Enter an aspect ratio such as 21:9, or select Automatic");
        }
        result.sharpScaling=Selected(Scaling)==1;result.render.maximumDetail=Selected(Detail)==1;
        result.render.reflections=Selected(Reflections);
        result.render.brightness=WholeNumber(Brightness,"Brightness",-50,50);
        result.render.contrast=WholeNumber(Contrast,"Contrast",50,150);
        result.render.gamma=static_cast<int>(std::lround(Numeric(Gamma,"Gamma",0.5,2.0)*100.0));
        result.debugTools=Checked(Debug);result.unlockAll=Checked(Unlock);
        result.gameplay.racerNitro=Checked(RacerNitro);result.gameplay.trafficVehicles=Checked(TrafficVehicles);
        result.gameplay.disableCatchUp=Checked(DisableCatchUp);result.gameplay.cockpitView=Checked(CockpitView);
        result.gameplay.restoreCutTraffic=Checked(RestoreCutTraffic);result.gameplay.randomNfsEditionOpponents=Checked(RandomNfsOpponents);
        validateNativePcSettings(result);return result;
    }
    void RedrawControls()
    {
        UINT flags=RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN;
        if(IsWindowVisible(window_))flags|=RDW_UPDATENOW;
        Require(RedrawWindow(window_,nullptr,nullptr,flags)!=FALSE,"Could not redraw native settings controls");
    }
    void ShowPage(int page)
    {
        if(page<0 || page>=static_cast<int>(pages_.size()))return;
        // Do not leave keyboard focus in a page that is about to be hidden.
        const auto focus=GetFocus();
        if(page!=page_)for(const auto child:pages_[static_cast<std::size_t>(page_)])
            if(focus==child || IsChild(child,focus)) {SetFocus(GetDlgItem(window_,Tab));break;}
        page_=page;
        for(std::size_t i=0;i<pages_.size();++i)if(static_cast<int>(i)!=page)
            for(const auto child:pages_[i])ShowWindow(child,SW_HIDE);
        for(const auto child:pages_[static_cast<std::size_t>(page)])ShowWindow(child,SW_SHOW);
        StatusText(L"");
        RedrawControls();
    }
    void Command(int id,int notification)
    {
        if(loading_)return;
        if(notification==BN_CLICKED && id==IDCANCEL) {
            done_=true;DestroyWindow(window_);return;
        }
        if(id==Filter && notification==CBN_SELCHANGE) {
            EnableWindow(GetDlgItem(window_,Anisotropy),Selected(Filter)==static_cast<int>(NativeTextureFilter::Anisotropic));return;
        }
        if(id==Monitor && notification==CBN_SELCHANGE) {
            const auto chosen=Selected(Monitor);
            if(chosen>=0 && static_cast<std::size_t>(chosen)<monitorKeys_.size())draft_.monitorDevice=monitorKeys_[static_cast<std::size_t>(chosen)];
            RefreshResolutionModes();return;
        }
        if(id==Resolution && notification==CBN_SELCHANGE) {
            ReadResolution(draft_);
            return;
        }
        if(id==Adapter && notification==CBN_SELCHANGE) {
            const auto chosen=Selected(Adapter);
            if(chosen>=0 && static_cast<std::size_t>(chosen)<adapterKeys_.size())
                draft_.videoAdapter=adapterKeys_[static_cast<std::size_t>(chosen)];
            return;
        }
        if(notification!=BN_CLICKED)return;
        if(id==OriginalGraphics) {
            draft_.textureFilter=NativeTextureFilter::Authored;draft_.antialiasing=NativeAntialiasing::Off;
            draft_.anisotropy=16;draft_.render=NativeRenderSettings{};
            draft_.renderScale=0;draft_.raceAspect={};draft_.sharpScaling=false;LoadGraphics();
            StatusText(L"Original graphics preset selected. Press Save or Play to keep it.");return;
        }
        if(id==Defaults) {
            NativePcSettings defaults;
            defaults.masterVolume=draft_.masterVolume;
            defaults.frameLimit=draft_.frameLimit;
            defaults.vsync=draft_.vsync;
            defaults.showFps=draft_.showFps;
            defaults.input=draft_.input;
            draft_=std::move(defaults);
            LoadControls();StatusText(L"Defaults selected. Press Save or Play to save them.");return;
        }
        if(id==OpenProfile) {
            std::filesystem::create_directories(launch_.profileDirectory);
            const auto opened=ShellExecuteW(window_,L"open",launch_.profileDirectory.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
            Require(reinterpret_cast<INT_PTR>(opened)>32,"Could not open the profile folder");return;
        }
        if(id==Save || id==IDOK) {
            const auto chosen=ReadControls();
            saveNativePcSettings(file_,chosen);settings_=chosen;draft_=chosen;StatusText(L"Settings saved.");
            if(id==IDOK) {startGame_=!launch_.configureOnly;done_=true;DestroyWindow(window_);}
        }
    }
    static LRESULT CALLBACK Procedure(HWND window,UINT message,WPARAM first,LPARAM second)
    {
        auto* self=reinterpret_cast<NativeConfigurator*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE) {
            self=static_cast<NativeConfigurator*>(reinterpret_cast<CREATESTRUCTW*>(second)->lpCreateParams);
            self->window_=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if(self)try {
            if(message==WM_CREATE) {self->CreateControls();return 0;}
            if(message==WM_COMMAND) {self->Command(LOWORD(first),HIWORD(first));return 0;}
            if(message==WM_SIZE && first!=SIZE_MINIMIZED && !self->loading_) {self->RedrawControls();return 0;}
            if(message==WM_DISPLAYCHANGE) {self->RefreshMonitorsList();return 0;}
            if(message==DM_GETDEFID)return MAKELRESULT(IDOK,DC_HASDEFID);
            if(message==WM_NOTIFY && reinterpret_cast<NMHDR*>(second)->idFrom==Tab &&
                reinterpret_cast<NMHDR*>(second)->code==TCN_SELCHANGE) {
                self->ShowPage(static_cast<int>(SendDlgItemMessageW(window,Tab,TCM_GETCURSEL,0,0)));return 0;
            }
            if(message==WM_CLOSE) {self->done_=true;DestroyWindow(window);return 0;}
            if(message==WM_DESTROY) {self->window_=nullptr;self->done_=true;return 0;}
        } catch(const std::exception& error) {
            // WM_CREATE must return failure through Win32. Retain its original
            // exception so Run can report it once through the startup logger.
            if(message==WM_CREATE) {
                self->creationFailure_=std::current_exception();
                return -1;
            }
            MessageBoxW(window,NativeWideText(error.what()).c_str(),L"NFSHP2 - Settings",MB_OK|MB_ICONERROR);
            return 0;
        }
        return DefWindowProcW(window,message,first,second);
    }
    NativePcSettings& settings_;NativePcSettings draft_;const NativeProcessLaunch& launch_;std::filesystem::path file_;
    HWND window_{};HFONT font_{},headingFont_{};ATOM class_{};int dpi_{96};
    std::exception_ptr creationFailure_;
    std::array<std::vector<HWND>,3> pages_;
    std::vector<NativeWindowsMonitor> monitors_;std::vector<std::string> monitorKeys_;
    std::vector<std::pair<int,int>> resolutionModes_;
    std::vector<std::string> adapterKeys_;
    int page_{};
    bool loading_{true},done_{},startGame_{};
};

std::unique_ptr<GameResources> OpenNativeGameData(const NativeProcessLaunch& launch)
{
    const auto mode=launch.archiveData?GameResourceSource::ArchiveData:
        LooseGameDirectory(launch.dataSource)?GameResourceSource::LooseDirectory:GameResourceSource::Auto;
    auto files=std::make_unique<GameResources>(launch.dataSource,mode);
    Require(files->TryOpenPhysical("GLOBAL/GLOBALB.LZC") || files->TryOpenPhysical("GLOBAL/GLOBALB.BUN"),
        "The selected source has no NFSHP2 global resources");
    return files;
}

std::unique_ptr<GameResources> FindNativeGameData(const std::filesystem::path& executable,NativeProcessLaunch& launch)
{
    ResolveNativeGameData(executable,launch);
    return OpenNativeGameData(launch);
}

std::filesystem::path PickNativeGameData(HWND owner,int choice)
{
    const bool folder=choice==IDC_NFSHP2_PICK_EXTRACTED;
    const bool iso=choice==IDC_NFSHP2_PICK_ISO;
    const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
    Require(SUCCEEDED(initialized),"Could not initialize the game-data picker");
    struct DialogApartment {~DialogApartment() {CoUninitialize();}} apartment;
    const auto release=[](IUnknown* object) {object->Release();};
    IFileOpenDialog* fileDialog{};
    const auto created=CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&fileDialog));
    const std::unique_ptr<IFileOpenDialog,decltype(release)> dialog(fileDialog,release);
    Require(SUCCEEDED(created),"Could not create the game-data picker");
    FILEOPENDIALOGOPTIONS flags{};
    Require(SUCCEEDED(dialog->GetOptions(&flags)),"Could not read the game-data picker options");
    flags|=FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR;
    flags=folder?(flags&~FOS_FILEMUSTEXIST)|FOS_PICKFOLDERS:flags|FOS_FILEMUSTEXIST;
    Require(SUCCEEDED(dialog->SetOptions(flags)),"Could not configure the game-data picker");
    const auto* title=folder?L"Choose the extracted game folder":iso?L"Choose the original PS2 ISO":L"Choose ZZDATA.BIN";
    Require(SUCCEEDED(dialog->SetTitle(title)),"Could not set the game-data picker title");
    if(!folder) {
        const COMDLG_FILTERSPEC filter=iso?COMDLG_FILTERSPEC{L"PS2 disc image (*.iso)",L"*.iso"}:
            COMDLG_FILTERSPEC{L"Game archive data (*.bin)",L"*.bin"};
        Require(SUCCEEDED(dialog->SetFileTypes(1,&filter)),"Could not set the game-data picker filter");
    }
    const auto shown=dialog->Show(owner);
    if(shown==HRESULT_FROM_WIN32(ERROR_CANCELLED))return {};
    Require(SUCCEEDED(shown),"Could not show the game-data picker");
    IShellItem* item{};
    const auto gotItem=dialog->GetResult(&item);
    const std::unique_ptr<IShellItem,decltype(release)> selected(item,release);
    Require(SUCCEEDED(gotItem),"Could not read the selected game-data source");
    PWSTR path{};
    const auto status=selected->GetDisplayName(SIGDN_FILESYSPATH,&path);
    const std::unique_ptr<wchar_t,decltype(&CoTaskMemFree)> pathOwner(path,CoTaskMemFree);
    Require(SUCCEEDED(status) && path,"Could not read the selected game-data path");
    return std::filesystem::path(path).lexically_normal();
}

std::unique_ptr<GameResources> OpenSelectedGameData(const std::filesystem::path& path,int choice,NativeProcessLaunch& launch)
{
    auto candidate=launch;
    candidate.dataSource=path;
    candidate.archiveData=choice==IDC_NFSHP2_PICK_ARCHIVE;
    if(choice==IDC_NFSHP2_PICK_EXTRACTED)
        Require(LooseGameDirectory(path),"Choose the folder containing the extracted GLOBAL folder");
    if(choice==IDC_NFSHP2_PICK_ISO)
        Require(UpperPathAscii(path.extension().u8string())==".ISO" && OrdinaryFile(path),"Choose the original PS2 ISO file");
    auto files=OpenNativeGameData(candidate);
    launch=std::move(candidate);
    return files;
}

struct NativeGameDataDialog {
    NativeProcessLaunch& launch;
    std::unique_ptr<GameResources> files;

    static INT_PTR CALLBACK Procedure(HWND window,UINT message,WPARAM first,LPARAM second)
    {
        if(message==WM_INITDIALOG) {
            SetWindowLongPtrW(window,DWLP_USER,second);
            SendMessageW(window,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(
                LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDI_NFSHP2_APPLICATION))));
            return TRUE;
        }
        if(message!=WM_COMMAND || HIWORD(first)!=BN_CLICKED)return FALSE;
        const int choice=LOWORD(first);
        if(choice==IDCANCEL) {EndDialog(window,IDCANCEL);return TRUE;}
        if(choice!=IDC_NFSHP2_PICK_ARCHIVE && choice!=IDC_NFSHP2_PICK_EXTRACTED && choice!=IDC_NFSHP2_PICK_ISO)return FALSE;
        auto& state=*reinterpret_cast<NativeGameDataDialog*>(GetWindowLongPtrW(window,DWLP_USER));
        try {
            const auto path=PickNativeGameData(window,choice);
            if(path.empty())return TRUE;
            state.files=OpenSelectedGameData(path,choice,state.launch);
            EndDialog(window,IDOK);
        } catch(const std::exception& error) {
            SetDlgItemTextW(window,IDC_NFSHP2_DATA_STATUS,NativeWideText(error.what()).c_str());
        }
        return TRUE;
    }
};

std::unique_ptr<GameResources> SelectNativeGameData(NativeProcessLaunch& launch)
{
    NativeGameDataDialog state{launch,{}};
    const auto result=DialogBoxParamW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDD_NFSHP2_GAME_DATA),nullptr,
        NativeGameDataDialog::Procedure,reinterpret_cast<LPARAM>(&state));
    Require(result!=-1,"Could not open the game-data selection window");
    return std::move(state.files);
}
