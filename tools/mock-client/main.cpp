#include "common.hpp"
#include "handview/stream/stream.hpp"
#include "handview/encode/h264.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <regex>
#include <thread>
#include <memory>
#include "decoder.hpp"
using namespace handview;
using namespace handview::protocol;
struct CredentialError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
int run_client(int argc,char** argv,std::uint64_t deadline) {
    testtool::Args args(argc,argv,"|--play||--stop-at-end||--decode|");
    args.check("|--port||--token||--loopback-file||--wait||--duration||--play||--stop-at-end||--tap-every||--screen||--decode||--json||--cycles||--cycle-seconds||--view|");
    // --cycles N: Play for --cycle-seconds, Stop, repeat; every cycle must stream frames.
    const auto cycles=testtool::number(args.get("--cycles","0"),0,10000);
    const auto cycleSeconds=testtool::number(args.get("--cycle-seconds","4"),1,3600);
    const auto duration=testtool::number(args.get("--duration","10"),1,86400);
    const auto tapEvery=testtool::number(args.get("--tap-every","0"),0,3600000);
    auto [sw,sh]=testtool::size(args.get("--screen","1080x2400"));
    std::string port=args.get("--port"),hex=args.get("--token");
    if(args.has("--loopback-file")) {
        if(args.has("--port")||args.has("--token"))throw std::runtime_error("Choose loopback file or port and token");
        for(;;) {
            std::ifstream file(args.get("--loopback-file"));
            std::string content((std::istreambuf_iterator<char>(file)), {});
            std::smatch p,t;
            if(std::regex_search(content,p,std::regex("\"port\"\\s*:\\s*([0-9]+)"))&&std::regex_search(content,t,std::regex("\"token\"\\s*:\\s*\"([0-9a-fA-F]{64})\""))) {
                port=p[1];
                hex=t[1];
                break;
            }
            if(stream::now_us()>=deadline)throw std::runtime_error("Timed out waiting for loopback JSON");
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    auto token=testtool::token(hex);
    net::WinsockInit winsock;
    if(!winsock.valid())throw std::runtime_error("Winsock initialization failed");
    auto control=net::connect_loopback(static_cast<std::uint16_t>(testtool::number(port,1,65535)));
    if(!control)throw CredentialError("Control connection failed");
    net::set_nodelay(control);
    net::FrameWriter writer(control);
    net::Socket video;
    net::FrameReader cr(Channel::Control),vr(Channel::Video);
    auto send=[&](const Message& m) {
        if(!writer.send(m))throw std::runtime_error("Control send failed");
    };
    std::unique_ptr<H264Decoder> decoder;
    send(Hello {
        1,token,sw,sh,420,60, {
           {Codec::H264,Profile::Main,51,3840,2160}
        },"Handview Mock Client"
    });
    std::uint64_t frames=0,keys=0,bytes=0,decodeErrors=0,errors=0,taps=0;
    std::uint16_t width=0,height=0;
    bool welcomed=false,played=false,playSent=false,expectKey=true,done=false,stopResult=false;
    EditorStateValue state=EditorStateValue::Stopped;
    struct Tap {
        std::uint32_t seq;
        std::uint64_t time;
    };
    std::deque<Tap> pending;
    std::vector<double> latencies;
    std::uint32_t touchSeq=0;
    std::uint64_t streamFrames=0;
    std::uint64_t cyclesDone=0,emptyCycles=0,cycleStart=0;
    bool stopSent=false;
    auto start=stream::now_us(),lastStats=start,lastPing=start,lastTap=start,lastPeer=start;
    auto maybePlay=[&] {
        if(welcomed&&state==EditorStateValue::Stopped&&args.has("--play")&&!playSent) {
            send(Command {Cmd::Play});
            playSent=true;
        }
    };
    auto handle=[&](const Message& m,bool onVideo) {
        if(onVideo&&!std::holds_alternative<VideoFrame>(m)&&!std::holds_alternative<Unknown>(m)) {
            ++errors;
            return;
        }
        if(const auto* w=std::get_if<Welcome>(&m)) {
            if(onVideo||welcomed) {
                ++errors;
                return;
            }
            welcomed=true;
            width=w->width;
            height=w->height;
            state=w->editorState;
            played|=state==EditorStateValue::Playing;
            video=net::connect_loopback(static_cast<std::uint16_t>(testtool::number(port,1,65535)));
            if(!video)throw std::runtime_error("Video connection failed");
            net::set_nodelay(video);
            net::FrameWriter vw(video);
            if(!vw.send(VideoAttach {w->sessionId}))throw std::runtime_error("VideoAttach failed");
            // --view WxH: send ViewSize like the app's video area.
            if(args.has("--view")) {
                auto [viewW,viewH]=testtool::size(args.get("--view"));
                send(ViewSize {static_cast<std::uint16_t>(viewW),static_cast<std::uint16_t>(viewH),0,0,0,0});
            }
            maybePlay();
        }
        else if(const auto* r=std::get_if<Reject>(&m)) {
            std::cout<<"Reject "<<static_cast<int>(r->reason)<<'\n';
            if(args.has("--loopback-file"))throw CredentialError("Loopback credentials rejected");
            ++errors;
            done=true;
        }
        else if(const auto* s=std::get_if<EditorState>(&m)) {
            state=s->state;
            played|=state==EditorStateValue::Playing;
            std::cout<<"EditorState "<<static_cast<int>(state)<<'\n';
            maybePlay();
        }
        else if(const auto* c=std::get_if<CommandResult>(&m)) {
            std::cout<<"CommandResult "<<static_cast<int>(c->cmd)<<' '<<static_cast<int>(c->result)<<' '<<static_cast<int>(c->editorState)<<'\n';
            state=c->editorState;
            played|=state==EditorStateValue::Playing;
            if(c->cmd==Cmd::Stop)stopResult=true;
        }
        else if(const auto* config=std::get_if<StreamConfigChanged>(&m)) {
            width=config->width;
            height=config->height;
            expectKey=true;
            // Touches sent before this stream existed never reach a game; do not count them.
            pending.clear();
            streamFrames=0;
            std::cout<<"StreamConfigChanged "<<width<<'x'<<height<<'\n';
        }
        else if(const auto* paused=std::get_if<StreamPaused>(&m)) {
            std::cout<<"StreamPaused "<<static_cast<int>(paused->reason)<<'\n';
        }
        else if(const auto* e=std::get_if<Error>(&m)) {
            std::cout<<"Error "<<static_cast<int>(e->source)<<' '<<static_cast<int>(e->code)<<'\n';
            ++errors;
            expectKey=true;
            // Touches sent before this stream existed never reach a game; do not count them.
            pending.clear();
            streamFrames=0;
        }
        else if(const auto* p=std::get_if<Ping>(&m))send(Pong {p->timeUs});
        else if(const auto* b=std::get_if<Bye>(&m)) {
            if(b->reason!=ByeReason::Normal)++errors;
            done=true;
        }
        else if(const auto* f=std::get_if<VideoFrame>(&m)) {
            if(!onVideo) {
                ++errors;
                return;
            }
            ++frames;
            ++streamFrames;
            bytes+=f->data.size();
            auto flags=static_cast<unsigned>(f->flags);
            if(flags&1)++keys;
            if(expectKey) {
                if((flags&3)!=3)++errors;
                bool found=false;
                for(auto nal:encode::split_annex_b(f->data)) {
                    if(encode::nal_type(nal)==7) {
                        auto sps=encode::parse_sps(nal);
                        found=sps&&sps->width==width&&sps->height==height;
                        break;
                    }
                }
                if(!found)++errors;
                expectKey=false;
            }
            const auto now=stream::now_us();
            while(!pending.empty()&&seq_newer_or_equal(f->lastTouchSeq,pending.front().seq)) {
                latencies.push_back(static_cast<double>(now-pending.front().time)/1000.0);
                pending.pop_front();
            }
            if(args.has("--decode")) {
                try {
                    if(!decoder)decoder=std::make_unique<H264Decoder>(width,height);
                    for(auto seq:decoder->feed(*f))send(FrameAck {seq});
                }
                catch(const std::exception& e) {
                    ++decodeErrors;
                    std::cerr<<e.what()<<'\n';
                    done=true;
                }
            }
            else send(FrameAck {f->frameSeq});
        }
        else if(!std::holds_alternative<Pong>(m)&&!std::holds_alternative<Unknown>(m))++errors;
    };
    std::array<std::uint8_t,65536> buffer {};
    while(!done&&stream::now_us()-start<duration*1000000ull) {
        for(int channel=0;channel<2;++channel) {
            auto& socket=channel==0?control:video;
            if(!socket)continue;
            auto& reader=channel==0?cr:vr;
            int n=net::recv_some(socket,buffer,std::chrono::milliseconds(2));
            if(n==0) {
                ++errors;
                done=true;
                break;
            }
            if(n>0) {
                if(channel==0)lastPeer=stream::now_us();
                for(const auto& frame:reader.feed(std::span(buffer.data(),static_cast<size_t>(n))))handle(frame.message,channel==1);
                if(reader.terminal()) {
                    ++errors;
                    done=true;
                }
            }
        }
        if(done)break;
        auto now=stream::now_us();
        if(now-lastPeer>5000000) {
            ++errors;
            break;
        }
        if(welcomed&&now-lastStats>=250000) {
            send(DecoderStats {0,0});
            lastStats=now;
        }
        if(now-lastPing>=1000000) {
            send(Ping {now});
            lastPing=now;
        }
        if(cycles&&welcomed) {
            if(state==EditorStateValue::Playing&&!stopSent) {
                if(cycleStart==0)cycleStart=now;
                if(now-cycleStart>=cycleSeconds*1000000ull) {
                    if(streamFrames==0)++emptyCycles;
                    send(Command {Cmd::Stop});
                    stopSent=true;
                }
            }
            else if(state==EditorStateValue::Stopped&&stopSent) {
                ++cyclesDone;
                std::cout<<"Cycle "<<cyclesDone<<" frames "<<streamFrames<<std::endl;
                stopSent=false;
                cycleStart=0;
                streamFrames=0;
                pending.clear();
                if(cyclesDone>=cycles)break;
                send(Command {Cmd::Play});
            }
        }
        if(tapEvery&&state==EditorStateValue::Playing&&streamFrames>0&&now-lastTap>=tapEvery*1000ull&&now-start+250000<duration*1000000ull) {
            auto seq=++touchSeq;
            send(Touch {
                seq,now, {
                   {0,Phase::Began,.5f,.5f,1.f}
                }
            });
            pending.push_back( {seq,now});
            send(Touch {
                ++touchSeq,now, {
                   {0,Phase::Ended,.5f,.5f,0.f}
                }
            });
            ++taps;
            lastTap=now;
        }
    }
    if(decoder&&!done)try {
        for(auto seq:decoder->finish())send(FrameAck {seq});
        if(!decoder->pending.empty())++decodeErrors;
    }
    catch(const std::exception& e) {
        ++decodeErrors;
        std::cerr<<e.what()<<'\n';
    }
    if(args.has("--stop-at-end")&&welcomed&&!done) {
        send(Command {Cmd::Stop});
        const auto stopDeadline=stream::now_us()+1000000ull;
        while(!stopResult&&!done&&stream::now_us()<stopDeadline) {
            const int n=net::recv_some(control,buffer,std::chrono::milliseconds(10));
            if(n==0){++errors;done=true;break;}
            if(n>0)for(const auto& frame:cr.feed(std::span(buffer.data(),static_cast<size_t>(n))))handle(frame.message,false);
            if(cr.terminal()){++errors;done=true;}
        }
        if(!stopResult)++errors;
    }
    if(!done)send(Bye {ByeReason::Normal});
    double seconds=static_cast<double>(stream::now_us()-start)/1000000.;
    std::sort(latencies.begin(),latencies.end());
    auto percentile=[&](double p) {
        return latencies.empty()?0.:latencies[static_cast<size_t>(p*static_cast<double>(latencies.size()-1))];
    };
    std::ostringstream json;
    json<<"{\"frames\":"<<frames<<",\"keyframes\":"<<keys<<",\"fps\":"<<static_cast<double>(frames)/seconds<<",\"bytes_per_second\":"<<static_cast<double>(bytes)/seconds<<",\"decode_errors\":"<<decodeErrors<<",\"width\":"<<width<<",\"height\":"<<height<<",\"taps\":"<<taps<<",\"taps_resolved\":"<<latencies.size()<<",\"tap_latency_p50_ms\":"<<percentile(.5)<<",\"tap_latency_p95_ms\":"<<percentile(.95)<<",\"tap_latency_max_ms\":"<<percentile(1)<<",\"protocol_errors\":"<<errors<<"}";
    std::cout<<"Summary "<<json.str()<<std::endl;
    if(args.has("--json")) {
        std::ofstream out(args.get("--json"));
        out<<json.str()<<'\n';
        if(!out)throw std::runtime_error("Could not write stats JSON");
    }
    if(cycles)std::cout<<"Cycles "<<cyclesDone<<"/"<<cycles<<" empty "<<emptyCycles<<std::endl;
    return (!welcomed||errors||decodeErrors||((played||playSent)&&frames==0)||(cycles&&(cyclesDone<cycles||emptyCycles)))?1:0;
}
int main(int argc,char** argv) try {
    testtool::Args args(argc,argv,"|--play||--stop-at-end||--decode|");
    const auto path=args.get("--loopback-file");
    const auto deadline=stream::now_us()+testtool::number(args.get("--wait","30"),0,86400)*1000000ull;
    for(;;) {
        std::ifstream file(path);
        std::string before((std::istreambuf_iterator<char>(file)), {});
        try {
            return run_client(argc,argv,deadline);
        }
        catch(const CredentialError& error) {
            if(path.empty())throw;
            std::cerr<<error.what()<<"; waiting for changed loopback credentials\n";
            for(;;) {
                if(stream::now_us()>=deadline)throw std::runtime_error("Timed out waiting for changed loopback credentials");
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                std::ifstream changed(path);
                std::string after((std::istreambuf_iterator<char>(changed)), {});
                if(after!=before)break;
            }
        }
    }
}
catch(const std::exception& error) {
    std::cerr<<"mock-client: "<<error.what()<<'\n';
    return 1;
}
