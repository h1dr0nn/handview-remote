#include "test_support.hpp"
#include "handview/session/session.hpp"
using namespace handview;
using namespace handview::protocol;
using namespace std::chrono_literals;
struct App {
 net::Socket socket; net::FrameWriter writer; net::FrameReader reader{Channel::Link}; std::deque<net::Frame> frames; std::vector<std::uint8_t> last_bytes;
 explicit App(std::uint16_t port):socket(net::connect_loopback(port)),writer(socket) {}
 template<class T> std::optional<T> receive() {
  const auto end=std::chrono::steady_clock::now()+1500ms;
  while(std::chrono::steady_clock::now()<end) {
   while(!frames.empty()){auto f=std::move(frames.front()); frames.pop_front(); if(auto p=std::get_if<T>(&f.message)){last_bytes=f.bytes;return *p;}}
   std::array<std::uint8_t,4096> b{}; int n=net::recv_some(socket,b,100ms); if(n==0)return {}; if(n<0)continue;
   for(auto& f:reader.feed(std::span(b.data(),static_cast<std::size_t>(n))))frames.push_back(std::move(f));
  } return {};
 }
};
template<class F> bool eventually(F f) {auto end=std::chrono::steady_clock::now()+1000ms; do{if(f())return true;std::this_thread::sleep_for(5ms);}while(std::chrono::steady_clock::now()<end);return false;}
int main(){
 session::ServerConfig config; config.timeout=900ms; config.ping_interval=100ms; session::EditorServer server(config); CHECK(server.start(0));
 std::array<std::uint8_t,32> token{};token[0]=1;server.set_pending_token(token);server.set_link_token(token);
 Hello hello{1,token,2400,1080,400,60,{{Codec::H264,Profile::Main,41,1920,1080}},"test"};
 {App bad(server.port());auto h=hello;h.protoVersion=2;bad.writer.send(h);auto r=bad.receive<Reject>();CHECK(r && r->reason==RejectReason::BadVersion);}
 {App bad(server.port());auto h=hello;h.token[0]=2;bad.writer.send(h);auto r=bad.receive<Reject>();CHECK(r && r->reason==RejectReason::BadToken);}
 App app(server.port());app.writer.send(hello);auto welcome=app.receive<Welcome>();CHECK(welcome.has_value());if(!welcome)return 1;
 { App wrong(server.port()); auto id=welcome->sessionId; id[0]^=1; wrong.writer.send(VideoAttach{id}); std::array<std::uint8_t,1> b{}; CHECK(net::recv_some(wrong.socket,b,500ms)==0); }
 App video(server.port());video.writer.send(VideoAttach{welcome->sessionId});
 CHECK(eventually([&]{auto e=server.poll_event();return e && e->kind==session::SessionEvent::Kind::connected && e->hello.model=="test";}));
 {App bad(server.port());bad.writer.send(hello);auto r=bad.receive<Reject>();CHECK(r && r->reason==RejectReason::BadToken);}
 session::GameLink game;CHECK(game.connect(server.link_port(),token));std::optional<LinkStart> start;
 CHECK(eventually([&]{auto m=game.poll();if(m && std::holds_alternative<LinkStart>(*m))start=std::get<LinkStart>(*m);return start.has_value();}));
 CHECK(start && start->maxW==1920 && start->maxH==1080 && start->maxFps==60 && start->bitrateKbps==8000);
 app.writer.send(Ping{123}); auto pong=app.receive<Pong>(); CHECK(pong && pong->timeUs==123);
 app.writer.send(Command{Cmd::Play});CHECK(eventually([&]{auto c=server.poll_command();return c && c->cmd==Cmd::Play;}));
 server.command_result(Cmd::Play,Result::Ok,EditorStateValue::Playing);auto result=app.receive<CommandResult>();CHECK(result && result->result==Result::Ok);
 app.writer.send(Touch{7,20,{{0,Phase::Began,0.25f,0.5f,1.0f}}});CHECK(eventually([&]{auto m=game.poll();return m && std::holds_alternative<Touch>(*m) && std::get<Touch>(*m).touchSeq==7;}));
 app.writer.send(FrameAck{17}); CHECK(eventually([&]{auto m=game.poll();return m && std::holds_alternative<FrameAck>(*m) && std::get<FrameAck>(*m).frameSeq==17;}));
 app.writer.send(DecoderStats{2,3}); CHECK(eventually([&]{auto m=game.poll();return m && std::holds_alternative<DecoderStats>(*m) && std::get<DecoderStats>(*m).pendingOutputs==2;}));
 app.writer.send(RequestKeyframe{}); CHECK(eventually([&]{auto m=game.poll();return m && std::holds_alternative<RequestKeyframe>(*m);}));
 StreamConfigChanged change{640,480,Orientation::Landscape,30,3000}; CHECK(game.send_stream_config_changed(change)); auto changed=app.receive<StreamConfigChanged>(); CHECK(changed && app.last_bytes==encode(change));
 CHECK(game.send_error(ErrorSource::Encoder,ErrorCode::DeviceLost)); auto error=app.receive<Error>(); CHECK(error && error->code==ErrorCode::DeviceLost);
 VideoFrame frame{1,7,100,VideoFlags::Keyframe|VideoFlags::CodecConfig,{0,0,0,1,0x65,42}};CHECK(game.send_video_frame(frame));auto received=video.receive<VideoFrame>();CHECK(received && video.last_bytes==encode(frame));
 game.close();auto paused=app.receive<StreamPaused>();CHECK(paused && paused->reason==PauseReason::NotPlaying);
 CHECK(eventually([&]{auto e=server.poll_event();return e && e->kind==session::SessionEvent::Kind::game_disconnected;}));
 CHECK(game.connect(server.link_port(),token)); CHECK(eventually([&]{auto m=game.poll();return m && std::holds_alternative<LinkStart>(*m);}));
 server.set_pending_token(token);App replacement(server.port());replacement.writer.send(hello);CHECK(replacement.receive<Welcome>().has_value());auto bye=app.receive<Bye>();CHECK(bye && bye->reason==ByeReason::Replaced);
 CHECK(eventually([&]{auto m=game.poll();return m && std::holds_alternative<LinkStop>(*m);}));
 CHECK(eventually([&]{auto m=game.poll();return m && std::holds_alternative<LinkStart>(*m);}));
 CHECK(replacement.receive<Bye>().has_value());CHECK(eventually([&]{auto e=server.poll_event();return e && e->kind==session::SessionEvent::Kind::disconnected && e->timed_out;}));
 CHECK(eventually([&]{auto m=game.poll();return m && std::holds_alternative<LinkStop>(*m);}));
 server.stop();return test_result("session");
}

