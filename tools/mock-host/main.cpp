#include "../mock-client/common.hpp"
#include "handview/session/session.hpp"
#include "handview/stream/stream.hpp"
#include <windows.h>
#include <random>
#include <sstream>
#include <iomanip>
#include <thread>
#include <atomic>
#include <algorithm>
using namespace handview;
using namespace handview::protocol;
namespace {
    std::atomic<bool> interrupted=false;
    BOOL WINAPI stop(DWORD event) {
        if(event==CTRL_C_EVENT||event==CTRL_BREAK_EVENT||event==CTRL_CLOSE_EVENT) {
            interrupted=true;
            return TRUE;
        }
        return FALSE;
    }
    std::wstring quote(const std::wstring& s) {
        std::wstring out=L"\"";
        unsigned slashes=0;
        for(wchar_t c:s) {
            if(c==L'\\') {
                ++slashes;
                continue;
            }
            if(c==L'"')out.append(slashes*2+1,L'\\');
            else out.append(slashes,L'\\');
            slashes=0;
            out+=c;
        }
        out.append(slashes*2,L'\\');
        return out+L'"';
    }
    std::wstring wide(const std::string& s) {
        if(s.empty())return {};
        int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
        if(!count)throw std::runtime_error("Invalid UTF-8 argument");
        std::wstring out(static_cast<size_t>(count),0);
        MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),count);
        return out;
    }
    struct ProcessResult {
        DWORD code;
        std::string output;
    };
    ProcessResult run(const std::vector<std::string>& args) {
        SECURITY_ATTRIBUTES security {sizeof(security),nullptr,TRUE};
        HANDLE read=nullptr,write=nullptr;
        if(!CreatePipe(&read,&write,&security,0))throw std::runtime_error("Cannot create adb output pipe");
        SetHandleInformation(read,HANDLE_FLAG_INHERIT,0);
        std::wstring command;
        for(const auto& a:args) {
            if(!command.empty())command+=L' ';
            command+=quote(wide(a));
        }
        STARTUPINFOW si {};
        si.cb=sizeof(si);
        si.dwFlags=STARTF_USESTDHANDLES;
        si.hStdOutput=write;
        si.hStdError=write;
        si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
        PROCESS_INFORMATION pi {};
        BOOL ok=CreateProcessW(nullptr,command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi);
        CloseHandle(write);
        if(!ok) {
            CloseHandle(read);
            throw std::runtime_error("Cannot launch adb: "+args.front());
        }
        std::string output;
        char bytes[4096];
        DWORD n=0;
        while(ReadFile(read,bytes,sizeof(bytes),&n,nullptr)&&n)output.append(bytes,n);
        CloseHandle(read);
        WaitForSingleObject(pi.hProcess,INFINITE);
        DWORD code=1;
        GetExitCodeProcess(pi.hProcess,&code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return {code,output};
    }
    struct Reverse {
        std::string adb,serial,port;
        bool active=false;
        ~Reverse() {
            if(active)try {
                auto result=run( {adb,"-s",serial,"reverse","--remove","tcp:"+port});
                if(result.code)std::cerr<<"adb cleanup: "<<result.output;
            }
            catch(const std::exception& e) {
                std::cerr<<e.what()<<'\n';
            }
        }
    };
    std::array<std::uint8_t,32> randomToken() {
        std::random_device random;
        std::array<std::uint8_t,32> result {};
        for(auto& b:result)b=static_cast<std::uint8_t>(random());
        return result;
    }
    std::string hexToken(const std::array<std::uint8_t,32>& token) {
        std::ostringstream out;
        out<<std::hex<<std::setfill('0');
        for(auto b:token)out<<std::setw(2)<<static_cast<unsigned>(b);
        return out.str();
    }
    void draw(std::vector<std::uint8_t>& image,unsigned w,unsigned h,unsigned frame,float tx,float ty) {
        int dx=static_cast<int>(tx*static_cast<float>(w-1)),dy=static_cast<int>(ty*static_cast<float>(h-1));
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
            auto i=(static_cast<size_t>(y)*w+x)*4;
            image[i]=static_cast<std::uint8_t>(x*255/w+frame);
            image[i+1]=static_cast<std::uint8_t>(y*255/h+frame/2);
            image[i+2]=static_cast<std::uint8_t>(frame*3);
            image[i+3]=255;
            bool counter=y<h/8&&(frame&(1u<<std::min(15u,x*16/w)));
            int ox=static_cast<int>(x)-dx,oy=static_cast<int>(y)-dy;
            if(counter||ox*ox+oy*oy<400) {
                image[i]=255;
                image[i+1]=255;
                image[i+2]=255;
            }
        }
    }
}
int main(int argc,char** argv) try {
    testtool::Args args(argc,argv,"|--no-launch|");
    args.check("|--adb||--serial||--port||--size||--fps||--duration||--no-launch|");
    auto [width,height]=testtool::size(args.get("--size","1280x720"));
    if(width%2||height%2)throw std::runtime_error("Size must have even dimensions");
    auto fps=static_cast<std::uint8_t>(testtool::number(args.get("--fps","60"),1,240));
    auto duration=testtool::number(args.get("--duration","0"),0,86400);
    net::WinsockInit winsock;
    if(!winsock.valid())throw std::runtime_error("Winsock initialization failed");
    auto token=randomToken(),linkToken=randomToken();
    session::ServerConfig config;
    config.host.width=width;
    config.host.height=height;
    config.host.fps=fps;
    config.host.orientation=width>height?Orientation::Landscape:Orientation::Portrait;
    config.log=[](std::string_view text) {
        std::cerr<<text<<'\n';
    };
    session::EditorServer server(config);
    server.set_pending_token(token);
    server.set_link_token(linkToken);
    if(!(args.has("--port")?server.start(static_cast<std::uint16_t>(testtool::number(args.get("--port"),1,65535))):server.start()))throw std::runtime_error("Cannot listen on requested port");
    stream::StreamController stream( {},config.log);
    if(!stream.connect(server.link_port(),linkToken))throw std::runtime_error("Cannot connect stream to editor link");
    std::cout<<"HANDVIEW port="<<server.port()<<" token="<<hexToken(token)<<std::endl;
    SetConsoleCtrlHandler(stop,TRUE);
    Reverse reverse {args.get("--adb","adb"),args.get("--serial"),std::to_string(server.port())};
    if(!args.has("--no-launch")) {
        if(reverse.serial.empty()) {
            auto devices=run( {reverse.adb,"devices","-l"});
            if(devices.code)throw std::runtime_error(devices.output);
            std::istringstream lines(devices.output);
            std::string line;
            std::vector<std::string> connected;
            unsigned deviceCount=0;
            while(std::getline(lines,line)) {
                std::istringstream fields(line);
                std::string serial,state;
                fields>>serial>>state;
                if(state=="device"||state=="unauthorized"||state=="offline")++deviceCount;
                if(state=="device")connected.push_back(serial);
                if(state=="unauthorized")std::cerr<<"Authorize device "<<serial<<" on the phone\n";
            }
            if(connected.size()!=1||deviceCount!=1)throw std::runtime_error("Select one authorized device with --serial");
            reverse.serial=connected.front();
        }
        auto result=run( {reverse.adb,"-s",reverse.serial,"reverse","tcp:"+reverse.port,"tcp:"+reverse.port});
        if(result.code)throw std::runtime_error("adb reverse: "+result.output);
        reverse.active=true;
        result=run( {reverse.adb,"-s",reverse.serial,"shell","am","start","-n","io.github.h1dr0nn.handview/.MainActivity","--es","token",hexToken(token),"--ei","port",reverse.port});
        if(result.code||result.output.find("Error:")!=std::string::npos)throw std::runtime_error("adb launch: "+result.output);
        std::cout<<result.output;
    }
    bool playing=false;
    unsigned frame=0;
    float tx=.5f,ty=.5f;
    std::vector<std::uint8_t> rgba(static_cast<size_t>(width)*height*4);
    auto start=stream::now_us(),lastStatus=start,next=start;
    while(!interrupted&&(duration==0||stream::now_us()-start<duration*1000000ull)) {
        while(auto event=server.poll_event()) {
            std::cout<<"SessionEvent "<<static_cast<int>(event->kind)<<" reason="<<static_cast<int>(event->reason)<<'\n';
            if(event->kind==session::SessionEvent::Kind::disconnected) {
                playing=false;
                server.editor_state(EditorStateValue::Stopped);
            }
        }
        while(auto command=server.poll_command()) {
            auto result=Result::Ignored;
            if(command->cmd==Cmd::Play&&!playing) {
                playing=true;
                result=Result::Ok;
            }
            else if(command->cmd==Cmd::Stop&&playing) {
                playing=false;
                result=Result::Ok;
                server.stream_paused(PauseReason::NotPlaying);
            }
            auto state=playing?EditorStateValue::Playing:EditorStateValue::Stopped;
            server.command_result(command->cmd,result,state);
            if(result==Result::Ok)server.editor_state(state);
            std::cout<<"Command "<<static_cast<int>(command->cmd)<<" result="<<static_cast<int>(result)<<'\n';
        }
        auto now=stream::now_us();
        if(now>=next) {
            for(const auto& touch:stream.take_touches()) {
                tx=std::clamp(touch.x,0.f,1.f);
                ty=std::clamp(touch.y,0.f,1.f);
            }
            if(playing&&stream.wants_frame(now)) {
                draw(rgba,width,height,frame++,tx,ty);
                stream.submit_frame(rgba.data(),width,height,static_cast<std::uint32_t>(width)*4);
            }
            next=now+1000000u/fps;
        }
        if(now-lastStatus>=1000000) {
            auto s=stream.status();
            std::cout<<"Stream link="<<s.link_connected<<" playing="<<playing<<" streaming="<<s.streaming<<" encoder="<<s.encoder<<" size="<<s.width<<'x'<<s.height<<" fps="<<static_cast<unsigned>(s.fps)<<" frames="<<s.frames_sent<<" error="<<s.last_error<<std::endl;
            lastStatus=now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    stream.close();
    server.stop();
    SetConsoleCtrlHandler(stop,FALSE);
    return 0;
}
catch(const std::exception& e) {
    std::cerr<<"mock-host: "<<e.what()<<'\n';
    return 1;
}
