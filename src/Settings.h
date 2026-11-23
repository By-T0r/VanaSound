#pragma once
// One global preference per installation. No save during unload/reset of hooks.
enum class SettingResult { Loaded=0, Missing=1, Invalid=2, IoError=3 };
SettingResult load_gain(const std::filesystem::path& path,LONG& value) {
    value=100;
    if(path.empty()) return SettingResult::IoError;
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) {
        return GetLastError()==ERROR_FILE_NOT_FOUND ? SettingResult::Missing : SettingResult::IoError;
    }
    char data[64]{}; DWORD count=0;
    const bool ok=ReadFile(file,data,sizeof(data)-1,&count,nullptr)!=0; CloseHandle(file);
    if(!ok) return SettingResult::IoError;
    if(count>=sizeof(data)-1 || count<8 || std::memcmp(data,"volume=",7)) return SettingResult::Invalid;
    while(count && (data[count-1]=='\r' || data[count-1]=='\n')) data[--count]=0;
    // Reject embedded NULs and extra content, including malformed integer suffixes.
    if(std::strlen(data)!=count || !parse_gain(data+7,value)) { value=100; return SettingResult::Invalid; }
    return SettingResult::Loaded;
}
DWORD save_gain(const std::filesystem::path& path,LONG value) {
    if(path.empty() || value<0 || value>100) return ERROR_INVALID_PARAMETER;
    auto temporary=path;
    temporary+=L".tmp."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetCurrentThreadId());
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return GetLastError();
    char data[32]{}; const int length=std::snprintf(data,sizeof(data),"volume=%ld\n",value); DWORD count=0,error=0;
    if(!WriteFile(file,data,length,&count,nullptr)) error=GetLastError();
    else if(count!=static_cast<DWORD>(length)) error=ERROR_WRITE_FAULT;
    else if(!FlushFileBuffers(file)) error=GetLastError();
    CloseHandle(file);
    if(!error && !MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) error=GetLastError();
    if(error) DeleteFileW(temporary.c_str());
    return error;
}
