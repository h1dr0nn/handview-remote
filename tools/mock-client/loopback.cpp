#include "common.hpp"
#include <windows.h>
#include <fstream>
#include <regex>
#include <thread>
#include <filesystem>
struct Child {
    PROCESS_INFORMATION process {};
    Child(std::string command,HANDLE output) {
        STARTUPINFOA startup {};
        startup.cb=sizeof(startup);
        startup.dwFlags=STARTF_USESTDHANDLES;
        startup.hStdOutput=output;
        startup.hStdError=output;
        startup.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
        if(!CreateProcessA(nullptr,command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))throw std::runtime_error("Cannot launch test child");
    }
    DWORD wait(DWORD timeout) {
        if(WaitForSingleObject(process.hProcess,timeout)!=WAIT_OBJECT_0)throw std::runtime_error("Test child timeout");
        DWORD code=1;
        GetExitCodeProcess(process.hProcess,&code);
        return code;
    }
    ~Child() {
        if(WaitForSingleObject(process.hProcess,0)==WAIT_TIMEOUT) {
            TerminateProcess(process.hProcess,1);
            WaitForSingleObject(process.hProcess,2000);
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
};
std::string read(const std::string& path) {
    std::ifstream file(path);
    return {
        (std::istreambuf_iterator<char>(file)), {}
    };
}
int main(int argc,char** argv)try {
    if(argc!=3)throw std::runtime_error("Expected host and client executables");
    auto base=std::filesystem::temp_directory_path()/("handview-loopback-"+std::to_string(GetCurrentProcessId()));
    auto hostLog=base.string()+"-host.log",clientLog=base.string()+"-client.log";
    SECURITY_ATTRIBUTES security {sizeof(security),nullptr,TRUE};
    HANDLE hostOutput=CreateFileA(hostLog.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(hostOutput==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create host log");
    Child host(std::string("\"")+argv[1]+"\" --no-launch --duration 6",hostOutput);
    CloseHandle(hostOutput);
    std::smatch match;
    std::string output;
    for(unsigned i=0;i<100;++i) {
        output=read(hostLog);
        if(std::regex_search(output,match,std::regex("HANDVIEW port=([0-9]+) token=([0-9a-f]{64})")))break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if(match.empty())throw std::runtime_error("Host did not print credentials: "+output);
    std::string command=std::string("\"")+argv[2]+"\" --port "+match[1].str()+" --token "+match[2].str()+" --play --tap-every 300 --decode --duration 4";
    HANDLE clientOutput=CreateFileA(clientLog.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&security,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(clientOutput==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create client log");
    Child client(command,clientOutput);
    CloseHandle(clientOutput);
    auto code=client.wait(15000);
    auto hostCode=host.wait(8000);
    output=read(clientLog);
    std::cout<<read(hostLog)<<output;
    std::smatch frames,taps,resolved;
    if(code||hostCode||!std::regex_search(output,frames,std::regex("\"frames\":([0-9]+)"))||frames[1].str()=="0"||!std::regex_search(output,taps,std::regex("\"taps\":([0-9]+)"))||!std::regex_search(output,resolved,std::regex("\"taps_resolved\":([0-9]+)"))||taps[1].str()=="0"||taps[1].str()!=resolved[1].str())throw std::runtime_error("Loopback failed; logs: "+hostLog+" and "+clientLog);
    std::filesystem::remove(hostLog);
    std::filesystem::remove(clientLog);
    return 0;
}
catch(const std::exception& e) {
    std::cerr<<e.what()<<'\n';
    return 1;
}
