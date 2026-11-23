#include "../src/VanaSound.cpp"
#include <cassert>
#include <vector>

int recordedVolume=0,recordedDuration=0,recordedToken=0;
void put32(uint8_t* p,uint32_t value) { std::memcpy(p,&value,4); }
void call_at(uint8_t* code,size_t site,size_t target) {
    code[site]=0xE8; put32(code+site+1,static_cast<uint32_t>(target-site-5));
}
struct Running {
    RequestFn request; void* object; volatile LONG stop=0,calls=0,failures=0;
    static DWORD WINAPI run(void* p) {
        auto& r=*static_cast<Running*>(p);
        while(!InterlockedCompareExchange(&r.stop,0,0)) {
            if(r.request(r.object,nullptr,100)!=123) InterlockedIncrement(&r.failures);
            InterlockedIncrement(&r.calls); Sleep(0);
        } return 0;
    }
};
int main(int argc,char** argv) {
    assert(argc==3); module=GetModuleHandleW(nullptr);
    LONG value=0;
    auto settings=std::filesystem::path(argv[1]).parent_path()/(L"settings-test-"+std::to_wstring(GetCurrentProcessId())+L".cfg");
    DeleteFileW(settings.c_str());
    assert(load_gain(settings,value)==SettingResult::Missing && value==100);
    for(LONG expected:{0,20,50,100}) {
        assert(save_gain(settings,expected)==0);
        value=-1; assert(load_gain(settings,value)==SettingResult::Loaded && value==expected);
    }
    assert(save_gain(settings,101)==ERROR_INVALID_PARAMETER);
    assert(load_gain(settings,value)==SettingResult::Loaded && value==100);
    HANDLE held=CreateFileW(settings.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr); assert(held!=INVALID_HANDLE_VALUE);
    assert(save_gain(settings,20)!=0); CloseHandle(held);
    assert(load_gain(settings,value)==SettingResult::Loaded && value==100);
    for(auto bad:{"volume=101", "volume=-1", "volume=50junk", "volume=", "other=50", "volume=20\nextra"}) {
        FILE* file=nullptr; assert(!_wfopen_s(&file,settings.c_str(),L"wb")); std::fputs(bad,file); std::fclose(file);
        assert(load_gain(settings,value)==SettingResult::Invalid && value==100);
    }
    assert(DeleteFileW(settings.c_str()));
    assert(parse_gain("0",value)&&value==0); assert(parse_gain("100",value)&&value==100);
    for(auto text:{"", "101", "-1", "50x", "50 ", "99999999"}) assert(!parse_gain(text,value));
    assert(scaled(-1,100)==-1 && scaled(150,100)==150 && scaled(127,50)==64 && scaled(127,0)==0);
    auto image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0xC15000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)); assert(image);
    clientBase=reinterpret_cast<uintptr_t>(image);
    const auto& selected=profiles[std::atoi(argv[2])];
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(image); dos->e_magic=IMAGE_DOS_SIGNATURE; dos->e_lfanew=0x100;
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(image+0x100);
    nt->Signature=IMAGE_NT_SIGNATURE; nt->FileHeader.Machine=IMAGE_FILE_MACHINE_I386;
    nt->FileHeader.TimeDateStamp=selected.timestamp; nt->OptionalHeader.SizeOfImage=selected.size;
    for(const auto& site:nativeSites) std::memcpy(image+site.rva+selected.policyDelta,site.bytes,site.size);
    const uint8_t continuation[]={0x89,0x86,0xE0,1,0,0,0xC7,0x86,0xE4,1,0,0,0,0,0x40,0x40,0x5F,0x5E,0x5B,0xC2,8,0};
    std::memcpy(image+0x36404,continuation,sizeof(continuation));
    put32(image+selected.pool,static_cast<uint32_t>(clientBase+0x700004));
    put32(image+0x700000,20); uintptr_t pool=0;
    assert(validate(clientBase,pool) && profile==&selected && pool==clientBase+0x700004);
    put32(image+0x700000,32); assert(!validate(clientBase,pool)); put32(image+0x700000,20);
    for(const auto& site:nativeSites) {
        image[site.rva+selected.policyDelta]^=1; assert(!validate(clientBase,pool)); image[site.rva+selected.policyDelta]^=1;
    }
    image[0x36404]=0xE9; assert(!validate(clientBase,pool)); image[0x36404]=0x89;
    nt->FileHeader.TimeDateStamp^=1; assert(!validate(clientBase,pool)); nt->FileHeader.TimeDateStamp^=1;
    assert(validate(clientBase,pool));
    const uint8_t request[]={0xA0,0,0,0,0,0x56,0x84,0xC0,
        0x8B,0x44,0x24,8,0x89,0x41,8,0x8B,0x44,0x24,12,0x89,0x41,12,
        0x8B,0x01,0x5E,0xC2,8,0};
    std::memcpy(image+0x36F50,request,sizeof(request)); put32(image+0x36F51,static_cast<uint32_t>(clientBase+profile->soundEnabled));
    std::memcpy(image+0x38470,volumePrefix,8);
    auto cursor=image+0x38478;
    for(auto entry:{std::pair<int,int*>{8,&recordedVolume},{12,&recordedDuration},{4,&recordedToken}}) {
        const uint8_t op[]={0x8B,0x44,0x24,static_cast<uint8_t>(entry.first),0xA3,0,0,0,0};
        std::memcpy(cursor,op,sizeof(op)); put32(cursor+5,reinterpret_cast<uintptr_t>(entry.second)); cursor+=sizeof(op);
    }
    *cursor=0xC3; image[0x384A6]=0xC3;
    for(auto site:{0x363FF,0x37014,0x38208}) {
        const uint8_t pushes[]={0xFF,0x74,0x24,8,0xFF,0x74,0x24,8};
        std::memcpy(image+site-8,pushes,8); call_at(image,site,0x36F50);
        const uint8_t end[]={0xC2,8,0}; std::memcpy(image+site+5,end,3);
    }
    // UI conversion wrapper nests into the same request detour: no double gain.
    std::memcpy(image+0x36F00,uiPrefix,8);
    std::memset(image+0x36F08,0x90,0x36F36-0x36F08);
    const uint8_t uiArgs[]={0x83,0xC4,0x10,0xFF,0x74,0x24,8,0xFF,0x74,0x24,8};
    std::memcpy(image+0x36F08,uiArgs,sizeof(uiArgs)); call_at(image,0x36F36,0x36F50);
    const uint8_t uiEnd[]={0xC2,8,0}; std::memcpy(image+0x36F3B,uiEnd,3);
    for(auto site:uiCallSites) {
        const uint8_t args[]={0xFF,0x74,0x24,8,0xFF,0x74,0x24,8};
        std::memcpy(image+site-8,args,8); call_at(image,site,0x36F00); std::memcpy(image+site+5,uiEnd,3);
    }
    const uint8_t unknownUiArgs[]={0xFF,0x74,0x24,8,0xFF,0x74,0x24,8};
    std::memcpy(image+0x38230,unknownUiArgs,8); call_at(image,0x38238,0x36F00); std::memcpy(image+0x3823D,uiEnd,3);
    for(auto site:{0x36710,0x3810C}) {
        const uint8_t push[]={0xFF,0x74,0x24,12};
        for(int i=0;i<3;++i) std::memcpy(image+site-12+i*4,push,4);
        call_at(image,site,0x38470); const uint8_t end[]={0x83,0xC4,12,0xC3}; std::memcpy(image+site+5,end,4);
    }
    image[0x363FF]=0x90; assert(enable_hooks()==MH_ERROR_UNSUPPORTED_FUNCTION && !hooksCreated); image[0x363FF]=0xE8;
    DWORD previous=0; assert(VirtualProtect(image,0x40000,PAGE_EXECUTE_READ,&previous));
    assert(FlushInstructionCache(GetCurrentProcess(),image,0x40000));
    auto element=reinterpret_cast<RequestFn>(image+0x363F7),spatial=reinterpret_cast<RequestFn>(image+0x3700C),other=reinterpret_cast<RequestFn>(image+0x38200);
    auto update=reinterpret_cast<VolumeFn>(image+0x36704),otherUpdate=reinterpret_cast<VolumeFn>(image+0x38100);
    uint32_t resource[4]={123,0,0,0}; uint32_t pan=64;
    assert(enable_hooks()==MH_OK);
    for(LONG percent:{100,50,0,100}) {
        InterlockedExchange(&gainPercent,percent);
        for(auto requestFn:{element,spatial}) {
            SetLastError(4321); assert(requestFn(resource,&pan,127)==123); assert(GetLastError()==4321);
            assert(resource[2]==reinterpret_cast<uintptr_t>(&pan) && resource[3]==static_cast<uint32_t>(scaled(127,percent)));
            resource[0]=0xFFFFFFFF; assert(requestFn(resource,&pan,90)==(requestFn==element?0:-1));
            resource[0]=0; assert(requestFn(resource,&pan,90)==0);
            resource[0]=0xFFFFFFFE; assert(requestFn(resource,&pan,90)==-2); resource[0]=123;
        }
        assert(other(resource,&pan,127)==123 && resource[3]==127);
        for(int i=0;i<2;++i) {
            SetLastError(4321); update(123,80,17); assert(GetLastError()==4321);
            assert(recordedVolume==scaled(80,percent) && recordedDuration==17 && recordedToken==123);
        }
        otherUpdate(123,80,17); assert(recordedVolume==80);
    }
    InterlockedExchange(&gainPercent,50); update(0,80,17); assert(recordedVolume==80 && recordedToken==0);
    for(LONG uiPercent:{100,50,0}) {
        InterlockedExchange(&uiGainPercent,uiPercent); InterlockedExchange(&gainPercent,0);
        for(auto site:uiCallSites) {
            auto ui=reinterpret_cast<UiFn>(image+site-8);
            SetLastError(4321); assert(ui(resource,64,127)==123 && GetLastError()==4321);
            assert(resource[2]==64 && resource[3]==static_cast<uint32_t>(scaled(127,uiPercent)));
            resource[0]=0xFFFFFFFF; assert(ui(resource,64,90)==-1); resource[0]=123;
        }
        auto unclassifiedUi=reinterpret_cast<UiFn>(image+0x38230);
        assert(unclassifiedUi(resource,64,127)==123 && resource[3]==127);
        assert(element(resource,&pan,127)==123 && resource[3]==0);
    }
    assert(disable_hooks()==MH_OK); assert(bytes_equal(requestTarget,requestPrefix) && bytes_equal(volumeTarget,volumePrefix) && bytes_equal(uiTarget,uiPrefix));
    assert(element(resource,&pan,127)==123 && resource[3]==127);
    resource[0]=0xFFFFFFFF; assert(element(resource,&pan,127)==-1); resource[0]=123;
    Running running{element,resource}; auto thread=CreateThread(nullptr,0,Running::run,&running,0,nullptr); assert(thread);
    for(int i=0;i<10;++i) { assert(enable_hooks()==MH_OK); Sleep(2); assert(disable_hooks()==MH_OK); }
    InterlockedExchange(&running.stop,1); assert(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0); CloseHandle(thread);
    assert(running.calls>0 && running.failures==0);
    HMODULE dll=LoadLibraryA(argv[1]); assert(dll);
    auto create=reinterpret_cast<export_CreatePlugin_f>(GetProcAddress(dll,"expCreatePlugin"));
    auto destroy=reinterpret_cast<export_DestroyPlugin_f>(GetProcAddress(dll,"expDestroyPlugin"));
    auto version=reinterpret_cast<export_GetInterfaceVersion_f>(GetProcAddress(dll,"expGetInterfaceVersion"));
    assert(!GetProcAddress(dll,"expCreatePolPlugin"));
    assert(create&&destroy&&version&&version()==4.30); auto instance=create("");
    assert(instance->GetFlags() & static_cast<uint32_t>(Ashita::PluginFlags::UseCommands));
    assert(instance->GetFlags() & static_cast<uint32_t>(Ashita::PluginFlags::UseDirect3D));
    assert(instance->Direct3DInitialize(nullptr));
    instance->Direct3DPresent(nullptr,nullptr,nullptr,nullptr);
    assert(instance->HandleCommand(0,"/vsound status",false)); assert(!instance->HandleCommand(0,"/other",false));
    assert(!instance->HandleCommand(0,"/rxmix",false));
    assert(instance->HandleCommand(0,"/vanasound",false));
    assert(instance->HandleCommand(0,"/vanasound status",false));
    assert(!instance->HandleCommand(0,"/vanasoundother",false));
    assert(!instance->Initialize(nullptr,nullptr,0)); instance->Release(); destroy(instance); assert(FreeLibrary(dll));
    puts("PASS: x86 request/update detours, caller filtering, gains, repeated updates, ABI, -1 return, scoped sanitizer, concurrency, exports");
}
