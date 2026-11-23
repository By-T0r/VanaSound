#include <windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include "Ashita.h"
#include "Profile.h"
#include "MixerHooks.h"
#pragma comment(linker,"/EXPORT:expCreatePlugin=_expCreatePlugin@4")
#pragma comment(linker,"/EXPORT:expDestroyPlugin=_expDestroyPlugin@4")
#pragma comment(linker,"/EXPORT:expGetInterfaceVersion=_expGetInterfaceVersion@0")
static_assert(sizeof(void*)==4 && ASHITA_INTERFACE_VERSION==4.30);
HMODULE module=nullptr;
void log_state(const char* state,int code=0) {
    wchar_t path[32768]{}; const DWORD size=GetModuleFileNameW(module,path,32768);
    if(!size || size>=32768) return;
    FILE* file=nullptr;
    const auto name=std::filesystem::path(path).parent_path()/L"VanaSound.log";
    if(!_wfopen_s(&file,name.c_str(),L"ab")) {
        std::fprintf(file,"state=%s code=%d effects=%ld interface=%ld\n",state,code,gain(),ui_gain());
        std::fclose(file);
    }
}
bool parse_gain(const char* text,LONG& result) {
    if(!text || !*text) return false;
    unsigned value=0,count=0;
    for(;*text;++text) { if(*text<'0' || *text>'9' || ++count>3) return false; value=value*10+(*text-'0'); }
    if(value>100) return false; result=static_cast<LONG>(value); return true;
}
std::filesystem::path settings_path(bool ui=false) {
    wchar_t path[32768]{}; const DWORD size=GetModuleFileNameW(module,path,32768);
    return size && size<32768 ? std::filesystem::path(path).parent_path()/(ui?L"VanaSound-ui.cfg":L"VanaSound.cfg") : std::filesystem::path();
}
#include "Settings.h"
class Mixer final: public IPlugin {
    IAshitaCore* core=nullptr; bool owns=false,ready=false,windowOpen=false; bool dirty[2]={false,false};
    void say(const char* text) { if(core) core->GetChatManager()->Writef(207,false,"[VanaSound] %s",text); }
public:
    const char* GetName() const override { return "VanaSound"; }
    const char* GetAuthor() const override { return "By-T0r"; }
    const char* GetDescription() const override { return "Spatial and interface mixer with failed-handle correction."; }
    const char* GetLink() const override { return ""; }
    double GetVersion() const override { return 0.62; }
    uint32_t GetFlags() const override { return static_cast<uint32_t>(Ashita::PluginFlags::UseCommands) | static_cast<uint32_t>(Ashita::PluginFlags::UseDirect3D); }
    bool Initialize(IAshitaCore* c,ILogManager*,uint32_t) override {
        core=c; if(InterlockedCompareExchange(&activeInstance,1,0)) return false;
        owns=true; clientBase=reinterpret_cast<uintptr_t>(GetModuleHandleW(L"FFXiMain.dll")); uintptr_t pool=0;
        if(!validate(clientBase,pool)) {
            log_state("profile_or_fix_rejected"); say("Unable to initialize. Check for incompatible sound plugins and restart."); return false;
        }
        LONG saved=100;
        const auto setting=load_gain(settings_path(),saved);
        if(setting!=SettingResult::Loaded) saved=50;
        InterlockedExchange(&gainPercent,saved);
        log_state("settings_loaded",static_cast<int>(setting));
        if(setting==SettingResult::Invalid || setting==SettingResult::IoError) say("Could not read saved volume; using 50% for this session.");
        LONG savedUi=50;
        const auto uiSetting=load_gain(settings_path(true),savedUi);
        if(uiSetting!=SettingResult::Loaded) savedUi=50;
        InterlockedExchange(&uiGainPercent,savedUi);
        log_state("ui_settings_loaded",static_cast<int>(uiSetting));
        if(uiSetting==SettingResult::Invalid || uiSetting==SettingResult::IoError) say("Could not read UI volume; using 50% for this session.");
        const auto status=enable_hooks(); ready=status==MH_OK;
        log_state(ready?"ready":"hooks_failed",status);
        if(!ready) { say("Hooks rejected. Check VanaSound.log."); return false; }
        char text[192]{}; std::snprintf(text,sizeof(text),"Sound Effects: %ld%% | Interface Sounds: %ld%%. /vsound to open settings.",gain(),ui_gain()); say(text); return true;
    }
    void Release() override {
        if(!owns) return;
        for(int i=0;i<2;++i) if(dirty[i]) persist(i!=0);
        windowOpen=false;
        const auto status=disable_hooks(); log_state("disabled",status);
        ready=false; owns=false; InterlockedExchange(&activeInstance,0);
    }
    ~Mixer() override { Release(); }
    void persist(bool ui) {
        const DWORD error=save_gain(settings_path(ui),ui?ui_gain():gain());
        dirty[ui]=false;
        if(error) { log_state("settings_save_failed",error); say("Could not save settings. Changes apply to this session only."); }
    }
    // Rendering is owned by Ashita's GUI manager; no device resources to allocate.
    bool Direct3DInitialize(IDirect3DDevice8*) override { return true; }
    void Direct3DPresent(const RECT*,const RECT*,HWND,const RGNDATA*) override {
        if(!ready || !windowOpen || !core || !core->GetGuiManager()) return;
        auto* gui=core->GetGuiManager();
        const auto display=gui->GetIO().DisplaySize;
        if(display.x<1 || display.y<1) return;
        // Fit both axes, including small windows and ultrawide displays.
        float scale=display.y/1080.0f;
        if(scale<1) scale=1;
        if(scale>display.x/400.0f) scale=display.x/400.0f;
        if(scale>display.y/280.0f) scale=display.y/280.0f;
        const float width=360*scale;
        gui->PushFont(nullptr,gui->GetStyle().FontSizeBase*scale);
        gui->PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(14*scale,14*scale));
        gui->PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(6*scale,5*scale));
        gui->PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(8*scale,10*scale));
        gui->PushStyleVar(ImGuiStyleVar_GrabMinSize,12*scale);
        gui->PushStyleColor(ImGuiCol_WindowBg,ImVec4(.10f,.10f,.10f,.96f));
        gui->PushStyleColor(ImGuiCol_TitleBgActive,ImVec4(.14f,.14f,.14f,1));
        gui->PushStyleColor(ImGuiCol_FrameBg,ImVec4(.18f,.18f,.18f,1));
        gui->PushStyleColor(ImGuiCol_FrameBgHovered,ImVec4(.28f,.28f,.28f,1));
        gui->PushStyleColor(ImGuiCol_FrameBgActive,ImVec4(.36f,.36f,.36f,1));
        gui->PushStyleColor(ImGuiCol_SliderGrab,ImVec4(.58f,.58f,.58f,1));
        gui->PushStyleColor(ImGuiCol_SliderGrabActive,ImVec4(.78f,.78f,.78f,1));
        gui->PushStyleColor(ImGuiCol_Button,ImVec4(.24f,.24f,.24f,1));
        gui->PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(.34f,.34f,.34f,1));
        gui->PushStyleColor(ImGuiCol_ButtonActive,ImVec4(.44f,.44f,.44f,1));
        gui->SetNextWindowSize(ImVec2(width,0),ImGuiCond_Always);
        gui->SetNextWindowPos(ImVec2(display.x*.5f,display.y*.5f),ImGuiCond_Appearing,ImVec2(.5f,.5f));
        if(gui->Begin("VanaSound",&windowOpen,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoCollapse)) {
            const auto pos=gui->GetWindowPos();
            const auto size=gui->GetWindowSize();
            float x=pos.x,y=pos.y;
            if(x+size.x>display.x) x=display.x-size.x;
            if(y+size.y>display.y) y=display.y-size.y;
            if(x<0) x=0; if(y<0) y=0;
            if(x!=pos.x || y!=pos.y) gui->SetWindowPos(ImVec2(x,y),ImGuiCond_Always);
            for(int channel=0;channel<2;++channel) {
                const bool ui=channel==1;
                gui->TextUnformatted(ui?"Interface Sounds":"Sound Effects");
                gui->SetNextItemWidth(-1);
                int value=ui?ui_gain():gain();
                if(gui->SliderInt(ui?"##interface":"##effects",&value,0,100,"%d%%",ImGuiSliderFlags_NoInput)) {
                    InterlockedExchange(ui?&uiGainPercent:&gainPercent,value); dirty[channel]=true;
                }
                if(dirty[channel] && !gui->IsItemActive()) persist(ui);
            }
            if(gui->Button("Restore Defaults",ImVec2(-1,0))) {
                InterlockedExchange(&gainPercent,50); InterlockedExchange(&uiGainPercent,50);
                persist(false); persist(true);
            }
        }
        gui->End(); gui->PopStyleColor(10); gui->PopStyleVar(4); gui->PopFont();
        if(!windowOpen) for(int i=0;i<2;++i) if(dirty[i]) persist(i!=0);
    }
    bool HandleCommand(int32_t,const char* command,bool) override {
        char normalized[256]{};
        if(command && (!std::strcmp(command,"/vanasound") || !std::strncmp(command,"/vanasound ",11))) {
            const int length=std::snprintf(normalized,sizeof(normalized),"/vsound%s",command+10);
            if(length<0 || length>=static_cast<int>(sizeof(normalized))) return true;
            command=normalized;
        }
        if(!command || (std::strcmp(command,"/vsound") && std::strncmp(command,"/vsound ",8))) return false;
        if(!std::strcmp(command,"/vsound")) {
            windowOpen=!windowOpen;
            if(!windowOpen) for(int i=0;i<2;++i) if(dirty[i]) persist(i!=0);
            return true;
        }
        if(!std::strcmp(command,"/vsound status")) {
            char text[128]{}; std::snprintf(text,sizeof(text),"Sound Effects: %ld%% | Interface Sounds: %ld%%",gain(),ui_gain());
            say(text); return true;
        }
        const bool ui=!std::strncmp(command,"/vsound ui ",11);
        const bool sfx=!std::strncmp(command,"/vsound sfx ",12);
        LONG value=0;
        if((ui || sfx) && parse_gain(command+(ui?11:12),value) && ready) {
            InterlockedExchange(ui?&uiGainPercent:&gainPercent,value); persist(ui); return true;
        }
        say("/vsound | /vsound sfx 0..100 | /vsound ui 0..100 | /vsound status"); return true;
    }

};
BOOL WINAPI DllMain(HMODULE handle,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) { module=handle; DisableThreadLibraryCalls(handle); } return TRUE;
}
extern "C" __declspec(dllexport) IPlugin* __stdcall expCreatePlugin(const char*) { return new Mixer(); }
extern "C" __declspec(dllexport) void __stdcall expDestroyPlugin(void* instance) { delete static_cast<IPlugin*>(instance); }
extern "C" __declspec(dllexport) double __stdcall expGetInterfaceVersion() { return ASHITA_INTERFACE_VERSION; }
