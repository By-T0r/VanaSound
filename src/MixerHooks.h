#pragma once
#include <intrin.h>
#include "../vendor/minhook/include/MinHook.h"

uintptr_t clientBase=0;
volatile LONG gainPercent=50,uiGainPercent=50,activeInstance=0;
using RequestFn=int (__thiscall*)(void*,void*,int);
using UiFn=int (__thiscall*)(void*,int,int);
UiFn originalUi=nullptr;
void* uiTarget=nullptr;
const uint8_t uiPrefix[]={0x83,0xEC,0x10,0x0F,0xBF,0x44,0x24,0x14};
uint8_t installedUi[8]{};
constexpr std::array<uint32_t,14> uiCallSites={0x37B82,0x37BE2,0x37C42,0x37CA2,0x37D02,0x37D62,0x37DC2,0x37E22,0x37E82,0x37EE2,0x37F42,0x37FA2,0x38002,0x38062};
LONG ui_gain() { return InterlockedCompareExchange(&uiGainPercent,0,0); }
using VolumeFn=void (__cdecl*)(int,int,int);
RequestFn originalRequest=nullptr;
VolumeFn originalVolume=nullptr;
void* requestTarget=nullptr; void* volumeTarget=nullptr;
bool hooksCreated=false;
uint8_t requestPrefix[8]{},volumePrefix[8]{0x8B,0x54,0x24,0x04,0x85,0xD2,0x7C,0x2E};
uint8_t installedRequest[8]{},installedVolume[8]{};
LONG gain() { return InterlockedCompareExchange(&gainPercent,0,0); }
int scaled(int volume,LONG percent) {
    if(percent==100) return volume; // Exact native passthrough, including unusual arguments.
    const int bounded=volume<0 ? 0 : volume>127 ? 127 : volume;
    return (bounded*percent+50)/100;
}
uintptr_t caller_rva(void* address) { return reinterpret_cast<uintptr_t>(address)-clientBase; }
int __fastcall ui_hook(void* resource,void*,int pan,int volume) {
    const auto caller=caller_rva(_ReturnAddress());
    for(auto site:uiCallSites) if(caller==site+5) {
        const int adjusted=scaled(volume,ui_gain());
        volume=adjusted; break;
    }
    return originalUi(resource,pan,volume);
}
int __fastcall request_hook(void* resource,void*,void* pan,int volume) {
    const auto caller=caller_rva(_ReturnAddress());
    if(caller==0x36404 || caller==0x37019) {
        const int adjusted=scaled(volume,gain());
        volume=adjusted;
    }
    const int result=originalRequest(resource,pan,volume);
    return caller==0x36404 && result==-1 ? 0 : result;
}
void __cdecl volume_hook(int token,int volume,int duration) {
    if(caller_rva(_ReturnAddress())==0x36715 && token>0) {
        const int adjusted=scaled(volume,gain());
        volume=adjusted;
    }
    originalVolume(token,volume,duration);
}
bool bytes_equal(void* target,const uint8_t* expected) {
    uint8_t actual[8]{};
    return read(reinterpret_cast<uintptr_t>(target),actual) && !std::memcmp(actual,expected,8);
}
bool direct_call(uintptr_t instruction,uintptr_t target) {
    uint8_t bytes[5]{}; uint32_t offset=0;
    if(!read(instruction,bytes) || bytes[0]!=0xE8) return false;
    std::memcpy(&offset,bytes+1,4); return instruction+5+offset==target;
}
MH_STATUS disable_hooks() {
    InterlockedExchange(&gainPercent,100); InterlockedExchange(&uiGainPercent,100);
    if(!hooksCreated) return MH_OK;
    MH_QueueDisableHook(uiTarget); MH_QueueDisableHook(requestTarget); MH_QueueDisableHook(volumeTarget);
    return MH_ApplyQueued();
}
MH_STATUS enable_hooks() {
    void* ui=reinterpret_cast<void*>(clientBase+0x36F00);
    void* request=reinterpret_cast<void*>(clientBase+0x36F50);
    void* volume=reinterpret_cast<void*>(clientBase+0x38470);
    requestPrefix[0]=0xA0; const uint32_t flags=static_cast<uint32_t>(clientBase+profile->soundEnabled);
    std::memcpy(requestPrefix+1,&flags,4); requestPrefix[5]=0x56; requestPrefix[6]=0x84; requestPrefix[7]=0xC0;
    if(!hooksCreated) {
        for(auto site:uiCallSites) if(!direct_call(clientBase+site,reinterpret_cast<uintptr_t>(ui))) return MH_ERROR_UNSUPPORTED_FUNCTION;
        if(!bytes_equal(ui,uiPrefix) || !bytes_equal(request,requestPrefix) || !bytes_equal(volume,volumePrefix) ||
           !direct_call(clientBase+0x363FF,reinterpret_cast<uintptr_t>(request)) ||
           !direct_call(clientBase+0x37014,reinterpret_cast<uintptr_t>(request)) ||
           !direct_call(clientBase+0x36710,reinterpret_cast<uintptr_t>(volume))) return MH_ERROR_UNSUPPORTED_FUNCTION;
        auto status=MH_Initialize(); if(status!=MH_OK) return status;
        status=MH_CreateHook(request,reinterpret_cast<void*>(request_hook),reinterpret_cast<void**>(&originalRequest));
        if(status!=MH_OK) { MH_Uninitialize(); return status; }
        status=MH_CreateHook(volume,reinterpret_cast<void*>(volume_hook),reinterpret_cast<void**>(&originalVolume));
        if(status!=MH_OK) { MH_RemoveHook(request); MH_Uninitialize(); return status; }
        status=MH_CreateHook(ui,reinterpret_cast<void*>(ui_hook),reinterpret_cast<void**>(&originalUi));
        if(status!=MH_OK) { MH_RemoveHook(request); MH_RemoveHook(volume); MH_Uninitialize(); return status; }
        HMODULE pinned=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(request_hook),&pinned)) {
            MH_RemoveHook(ui); MH_RemoveHook(request); MH_RemoveHook(volume); MH_Uninitialize(); return MH_ERROR_MEMORY_ALLOC;
        }
        uiTarget=ui; requestTarget=request; volumeTarget=volume; hooksCreated=true;
    } else if(ui!=uiTarget || (!bytes_equal(ui,uiPrefix) && !bytes_equal(ui,installedUi)) || request!=requestTarget || volume!=volumeTarget ||
        (!bytes_equal(request,requestPrefix) && !bytes_equal(request,installedRequest)) ||
        (!bytes_equal(volume,volumePrefix) && !bytes_equal(volume,installedVolume))) return MH_ERROR_UNSUPPORTED_FUNCTION;
    MH_QueueEnableHook(ui); MH_QueueEnableHook(request); MH_QueueEnableHook(volume);
    const auto status=MH_ApplyQueued();
    if(status!=MH_OK) { disable_hooks(); return status; }
    read(reinterpret_cast<uintptr_t>(ui),installedUi); read(reinterpret_cast<uintptr_t>(request),installedRequest); read(reinterpret_cast<uintptr_t>(volume),installedVolume);
    return MH_OK;
}
