#if defined(HP2_NATIVE_CONFIGURATOR_IMPLEMENTATION)
// Native Windows launcher, included at the existing platform boundary.
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
        RestoreCutTraffic,RandomNfsOpponents,StrongerOpponents,RestoredCarContent,OpenProfile,
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
        CheckBox(page,StrongerOpponents,L"More challenging opponents",40,283,342);
        CheckBox(page,RestoredCarContent,L"Restored cars and paints (player / AI)",400,283,348);
        Label(page,L"Saves and settings",40,329,565);
        Control(page,L"STATIC",launch_.profileDirectory.c_str(),SS_PATHELLIPSIS|SS_NOPREFIX,40,354,560,24);
        Button(page,OpenProfile,L"Open folder",616,349,132);
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
        CheckValue(StrongerOpponents,draft_.gameplay.strongerOpponents);
        CheckValue(RestoredCarContent,draft_.gameplay.restoredCarContent);
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
        result.gameplay.strongerOpponents=Checked(StrongerOpponents);
        result.gameplay.restoredCarContent=Checked(RestoredCarContent);
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

#else
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
        else if(key=="stronger_opponents")settings.gameplay.strongerOpponents=PcSettingBoolean(value,key);
        else if(key=="cockpit_view")settings.gameplay.cockpitView=PcSettingBoolean(value,key);
        else if(key=="restore_cut_traffic")settings.gameplay.restoreCutTraffic=PcSettingBoolean(value,key);
        else if(key=="random_nfs_edition_opponents")settings.gameplay.randomNfsEditionOpponents=PcSettingBoolean(value,key);
        else if(key=="restored_car_content")settings.gameplay.restoredCarContent=PcSettingBoolean(value,key);
        else if(key=="restore_prototype_paints" || key=="restored_music_content") {
            // Retire the rejected paint-replacement setting without enabling
            // the different, additive car-content option or rejecting an INI.
            (void)PcSettingBoolean(value,key);
        }
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
        <<"\nrestored_car_content="<<settings.gameplay.restoredCarContent
        <<"\nstronger_opponents="<<settings.gameplay.strongerOpponents
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

#endif // HP2_NATIVE_CONFIGURATOR_IMPLEMENTATION
