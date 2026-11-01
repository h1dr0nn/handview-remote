#include <optional>
#include "handview/session/session.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#pragma comment(lib, "bcrypt.lib")

namespace handview::session {
using namespace protocol;
using namespace std::chrono_literals;
namespace {
struct Peer {
    explicit Peer(net::Socket s) : socket(std::move(s)), writer(socket) {}
    net::Socket socket;
    net::FrameWriter writer;
};
template<class F> bool read_peer(Peer& peer, Channel channel, net::LogSink log, F callback,
                               std::function<void()> activity = {}) {
    net::FrameReader reader(channel, std::move(log));
    std::array<std::uint8_t,16384> bytes{};
    while (peer.socket) {
        const int n = net::recv_some(peer.socket, bytes, 100ms);
        if (n < 0) continue;
        if (n == 0) break;
        const auto before = reader.messages_seen();
        auto frames = reader.feed(std::span(bytes.data(), static_cast<std::size_t>(n)));
        if (activity && before != reader.messages_seen()) activity();
        for (auto& frame : frames) if (!callback(frame)) return false;
        if (reader.terminal()) return true;
    }
    return false;
}
}
struct GameLink::Impl {
    net::WinsockInit winsock;
    net::LogSink log;
    std::shared_ptr<Peer> peer;
    std::mutex peer_mutex;
    std::thread thread;
    std::atomic<bool> live{false};
    Queue<Message> incoming;
    std::shared_ptr<Peer> snapshot() { std::lock_guard lock(peer_mutex); return peer; }
};
GameLink::GameLink(net::LogSink log) : impl_(std::make_unique<Impl>()) { impl_->log = std::move(log); }
GameLink::~GameLink() { close(); }
bool GameLink::connect(std::uint16_t port, const std::array<std::uint8_t,32>& token) {
    close();
    auto socket = net::connect_loopback(port);
    if (!socket) return false;
    auto peer = std::make_shared<Peer>(std::move(socket));
    if (!peer->writer.send(LinkHello{token, GetCurrentProcessId()})) return false;
    { std::lock_guard lock(impl_->peer_mutex); impl_->peer = peer; }
    while (impl_->incoming.pop()) {}
    impl_->live = true;
    impl_->thread = std::thread([this,peer] {
        try { read_peer(*peer, Channel::Link, impl_->log, [this](const net::Frame& frame) {
            const auto& m = frame.message;
            if (std::holds_alternative<LinkStart>(m) || std::holds_alternative<LinkStop>(m) || std::holds_alternative<Touch>(m) || std::holds_alternative<FrameAck>(m) || std::holds_alternative<DecoderStats>(m) || std::holds_alternative<RequestKeyframe>(m) || std::holds_alternative<ViewSize>(m)) impl_->incoming.push(m);
            return true;
        }); } catch (...) {}
        impl_->live = false; peer->socket.close();
    });
    return true;
}
void GameLink::close() {
    auto peer = impl_->snapshot();
    if (peer) peer->socket.close();
    if (impl_->thread.joinable()) impl_->thread.join();
    impl_->live = false;
    std::lock_guard lock(impl_->peer_mutex); impl_->peer.reset();
}
bool GameLink::connected() const { return impl_->live; }
std::optional<Message> GameLink::poll() { return impl_->incoming.pop(); }
bool GameLink::send_video_frame(const VideoFrame& frame) { auto p=impl_->snapshot(); return p && p->writer.send(frame); }
bool GameLink::send_stream_config_changed(const StreamConfigChanged& config) { auto p=impl_->snapshot(); return p && p->writer.send(config); }
bool GameLink::send_error(ErrorSource source, ErrorCode code) { auto p=impl_->snapshot(); return p && p->writer.send(Error{source,code}); }

struct EditorServer::Impl {
    explicit Impl(ServerConfig c) : config(std::move(c)) {
        config.host.host = Host::Godot;
        config.host.codec = Codec::H264;
    }
    struct Session {
        std::shared_ptr<Peer> control;
        std::shared_ptr<Peer> video;
        Hello hello;
        Welcome welcome;
        std::optional<ViewSize> view;
        std::optional<StreamPrefs> prefs;
        std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
    };
    net::WinsockInit winsock;
    ServerConfig config;
    net::Socket listener, link_listener;
    std::atomic<bool> running{false};
    std::mutex mutex;
    std::recursive_mutex handshake_mutex;
    std::mutex workers_mutex;
    std::optional<std::array<std::uint8_t,32>> token, link_token;
    std::shared_ptr<Session> active;
    std::shared_ptr<Peer> game;
    std::uint32_t game_pid{};
    std::vector<std::shared_ptr<Peer>> peers;
    struct Worker {
        std::shared_ptr<std::atomic<bool>> done;
        std::thread thread;
    };
    std::vector<Worker> workers;
    std::thread accept_thread, link_thread, timer_thread, video_thread;
    Queue<Command> commands;
    Queue<SessionEvent> events;
    std::mutex video_mutex;
    std::condition_variable video_cv;
    std::deque<std::pair<std::shared_ptr<Session>,std::vector<std::uint8_t>>> videos;
    void wake_video() { std::lock_guard lock(video_mutex); video_cv.notify_all(); }
    void send_app(const Message& m) { std::shared_ptr<Peer> p; { std::lock_guard lock(mutex); if (active) p = active->control; } if (p) p->writer.send(m); }
    // Stream limits for the game: phone screen and decoder, then the user's
    // StreamPrefs (0 fields keep the default).
    LinkStart caps(const Hello& h, const std::optional<StreamPrefs>& prefs = std::nullopt) const {
        auto w = (std::max)(h.screenW,h.screenH), s = (std::min)(h.screenW,h.screenH);
        std::uint16_t dw = 0, ds = 0;
        for (const auto& d : h.decoders) { auto a = (std::max)(d.maxW,d.maxH), b = (std::min)(d.maxW,d.maxH); if (static_cast<unsigned>(a)*b > static_cast<unsigned>(dw)*ds) { dw=a; ds=b; } }
        if (dw && ds) { w=(std::min)(w,dw); s=(std::min)(s,ds); }
        LinkStart start{w,s,config.host.fps,config.host.bitrateKbps};
        if (prefs) {
            if (prefs->maxLongSide) start.maxW = (std::min)(start.maxW, prefs->maxLongSide);
            if (prefs->maxShortSide) start.maxH = (std::min)(start.maxH, prefs->maxShortSide);
            if (prefs->maxFps) start.maxFps = prefs->maxFps;
            if (prefs->bitrateKbps) start.bitrateKbps = prefs->bitrateKbps;
        }
        return start;
    }
    void finish(const std::shared_ptr<Session>& session, ByeReason reason, bool timeout = false) {
        std::lock_guard transition_lock(handshake_mutex);
        std::shared_ptr<Peer> g, v;
        {
            std::lock_guard lock(mutex);
            if (active != session) return;
            if (timeout && std::chrono::steady_clock::now()-session->last < config.timeout) return;
            active.reset(); g=game; v=session->video;
        }
        wake_video();
        session->control->writer.send(Bye{reason}); session->control->socket.close(); if(v) v->socket.close();
        if(g) g->writer.send(LinkStop{});
        events.push({SessionEvent::Kind::disconnected,session->hello,reason,0,timeout});
    }
    void phone(std::shared_ptr<Peer> p) {
        std::shared_ptr<Session> own;
        std::shared_ptr<Session> attached;
        bool wrong_channel = false;
        bool first = true;
        const bool protocol_error = read_peer(*p,Channel::Control,config.log,[&](const net::Frame& f) {
            if(first) {
                first=false;
                if(const auto* attach=std::get_if<VideoAttach>(&f.message)) {
                    {
                        std::lock_guard lock(mutex);
                        if(!active || active->welcome.sessionId!=attach->sessionId || active->video) return false;
                        active->video=p;
                        attached=active;
                    }
                    wake_video();
                    return true;
                }
                const auto* h=std::get_if<Hello>(&f.message); if(!h) return false;
                std::lock_guard handshake_lock(handshake_mutex);
                if(h->protoVersion!=kProtocolVersion) { p->writer.send(Reject{RejectReason::BadVersion}); return false; }
                own=std::make_shared<Session>(); own->control=p; own->hello=*h;
                { std::lock_guard lock(mutex); own->welcome=config.host; }
                if(BCryptGenRandom(nullptr,own->welcome.sessionId.data(),16,BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0) return false;
                std::shared_ptr<Session> old;
                { std::lock_guard lock(mutex); if(!token || *token!=h->token) { p->writer.send(Reject{RejectReason::BadToken}); return false; } token.reset(); old=active; }
                if(old) finish(old,ByeReason::Replaced);
                std::shared_ptr<Peer> g;
                {
                    std::lock_guard lock(mutex);
                    if(!p->writer.send(own->welcome)) return false;
                    active=own;
                    g=game;
                }
                events.push({SessionEvent::Kind::connected,*h});
                if(g) g->writer.send(caps(*h));
                return true;
            }
            if(!own) { wrong_channel=true; return false; }
            { std::lock_guard lock(mutex); if(active!=own) return false; own->last=std::chrono::steady_clock::now(); }
            if(const auto* ping=std::get_if<Ping>(&f.message)) p->writer.send(Pong{ping->timeUs});
            else if(const auto* cmd=std::get_if<Command>(&f.message)) commands.push(*cmd);
            else if(const auto* error=std::get_if<Error>(&f.message)) {
                if(config.log) config.log("Phone error: source " + std::to_string(static_cast<unsigned>(error->source)) + ", code " + std::to_string(static_cast<unsigned>(error->code)));
                SessionEvent event{SessionEvent::Kind::phone_error};
                event.source=error->source; event.code=error->code;
                events.push(std::move(event));
            }
            else if(const auto* bye=std::get_if<Bye>(&f.message)) { finish(own,bye->reason); return false; }
            else if(const auto* prefs=std::get_if<StreamPrefs>(&f.message)) {
                // Applied at once: a linked game restarts its stream with the new limits.
                std::shared_ptr<Peer> g; LinkStart start;
                { std::lock_guard lock(mutex); own->prefs=*prefs; g=game; start=caps(own->hello,own->prefs); }
                if(g) g->writer.send(start);
            }
            else if(const auto* view=std::get_if<ViewSize>(&f.message)) {
                // Kept for a game that links later; forwarded to one already linked.
                std::shared_ptr<Peer> g; { std::lock_guard lock(mutex); own->view=*view; g=game; } if(g) g->writer.send_bytes(f.bytes);
            }
            else if(std::holds_alternative<Touch>(f.message)||std::holds_alternative<FrameAck>(f.message)||std::holds_alternative<DecoderStats>(f.message)||std::holds_alternative<RequestKeyframe>(f.message)) {
                std::shared_ptr<Peer> g; { std::lock_guard lock(mutex); g=game; } if(g) g->writer.send_bytes(f.bytes);
            }
            return true;
        }, [&] {
            std::lock_guard lock(mutex);
            if (own && active==own) own->last=std::chrono::steady_clock::now();
        });
        if(own) finish(own,protocol_error ? ByeReason::ProtocolError : ByeReason::Normal);
        else if(attached) finish(attached,(protocol_error || wrong_channel) ? ByeReason::ProtocolError : ByeReason::Normal);
        else if(protocol_error) p->writer.send(Bye{ByeReason::ProtocolError});
        p->socket.close();
    }
    void game_reader(std::shared_ptr<Peer> p) {
        bool authenticated=false;
        read_peer(*p,Channel::Link,config.log,[&](const net::Frame& f) {
            if(!authenticated) {
                const auto* h=std::get_if<LinkHello>(&f.message); if(!h) return false;
                std::lock_guard transition_lock(handshake_mutex);
                std::shared_ptr<Session> s;
                { std::lock_guard lock(mutex); if(!link_token || *link_token!=h->linkToken || game) return false; game=p; game_pid=h->processId; authenticated=true; s=active; }
                if(s) { std::lock_guard lock(mutex); p->writer.send(caps(s->hello,s->prefs)); if(s->view) p->writer.send(*s->view); }
                return true;
            }
            if(std::holds_alternative<VideoFrame>(f.message)) {
                std::shared_ptr<Session> s; { std::lock_guard lock(mutex); s=active; }
                if(s) { std::lock_guard lock(video_mutex); videos.emplace_back(s,f.bytes); video_cv.notify_all(); }
            } else if(std::holds_alternative<StreamConfigChanged>(f.message)||std::holds_alternative<Error>(f.message)) {
                std::shared_ptr<Peer> c; { std::lock_guard lock(mutex); if(active) c=active->control; } if(c) c->writer.send_bytes(f.bytes);
            }
            return true;
        });
        if(authenticated) {
            std::lock_guard transition_lock(handshake_mutex);
            std::uint32_t pid=0;
            { std::lock_guard lock(mutex); if(game==p) { game.reset(); pid=game_pid; } }
            send_app(StreamPaused{PauseReason::NotPlaying}); events.push({SessionEvent::Kind::game_disconnected,{},ByeReason::Normal,pid});
        }
        p->socket.close();
    }
    void accept_loop(bool link) {
        while(running) {
            auto socket=net::accept(link?link_listener:listener); if(!socket) continue;
            auto p=std::make_shared<Peer>(std::move(socket));
            std::lock_guard worker_lock(workers_mutex);
            for(auto it=workers.begin();it!=workers.end();) {
                if(*it->done) { it->thread.join(); it=workers.erase(it); }
                else ++it;
            }
            {
                std::lock_guard lock(mutex);
                peers.erase(std::remove_if(peers.begin(),peers.end(),[](const auto& peer) { return !peer->socket; }),peers.end());
                peers.push_back(p);
            }
            auto done=std::make_shared<std::atomic<bool>>(false);
            workers.push_back({done,{}});
            workers.back().thread=std::thread([this,p,link,done] {
                try { if(link) game_reader(p); else phone(p); } catch(...) { p->socket.close(); }
                *done=true;
            });
        }
    }
};
EditorServer::EditorServer(ServerConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}
EditorServer::~EditorServer() { stop(); }
bool EditorServer::start() { for(std::uint16_t p=38500;p<=38509;++p) if(start(p)) return true; return false; }
bool EditorServer::start(std::uint16_t port) {
    auto& i=*impl_; if(i.running) return false;
    i.listener=net::listen_loopback(port); if(!i.listener) return false;
    i.link_listener=net::listen_loopback(0); if(!i.link_listener) { i.listener.close(); return false; }
    i.running=true;
    i.accept_thread=std::thread([&i] { try { i.accept_loop(false); } catch(...) {} });
    i.link_thread=std::thread([&i] { try { i.accept_loop(true); } catch(...) {} });
    i.timer_thread=std::thread([&i] {
        try {
            auto next=std::chrono::steady_clock::now()+i.config.ping_interval;
            while(i.running) {
                std::unique_lock wait(i.video_mutex); i.video_cv.wait_for(wait,20ms,[&i]{return !i.running;}); wait.unlock();
                const auto now=std::chrono::steady_clock::now(); std::shared_ptr<Impl::Session> s; bool expired=false;
                { std::lock_guard lock(i.mutex); s=i.active; if(s) expired=now-s->last>=i.config.timeout; }
                if(expired) i.finish(s,ByeReason::Normal,true);
                else if(s && now>=next) i.send_app(Ping{static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count())});
                if(now>=next) next=now+i.config.ping_interval;
            }
        } catch(...) {}
    });
    i.video_thread=std::thread([&i] {
        try { for(;;) {
            std::unique_lock lock(i.video_mutex); i.video_cv.wait(lock,[&i]{return !i.running || !i.videos.empty();});
            if(!i.running) break;
            auto frame=std::move(i.videos.front()); i.videos.pop_front();
            i.video_cv.wait(lock,[&i,&frame] {
                std::lock_guard state_lock(i.mutex);
                return !i.running || i.active!=frame.first || frame.first->video;
            });
            if(!i.running) break;
            std::shared_ptr<Peer> video;
            { std::lock_guard state_lock(i.mutex); if(i.active==frame.first) video=frame.first->video; }
            lock.unlock();
            if(video && !video->writer.send_bytes(frame.second)) i.finish(frame.first,ByeReason::Normal);
        } } catch(...) {}
    });
    return true;
}
void EditorServer::stop() {
    auto& i=*impl_; if(!i.running.exchange(false)) return;
    i.listener.close(); i.link_listener.close(); i.wake_video();
    if(i.accept_thread.joinable()) i.accept_thread.join(); if(i.link_thread.joinable()) i.link_thread.join();
    { std::lock_guard lock(i.mutex); for(auto& p:i.peers) p->socket.close(); }
    if(i.timer_thread.joinable()) i.timer_thread.join(); if(i.video_thread.joinable()) i.video_thread.join();
    for(auto& worker:i.workers) if(worker.thread.joinable()) worker.thread.join();
    i.workers.clear();
    { std::lock_guard lock(i.mutex); i.peers.clear(); i.active.reset(); i.game.reset(); }
    { std::lock_guard lock(i.video_mutex); i.videos.clear(); }
}
std::uint16_t EditorServer::port() const { return impl_->listener.port(); }
std::uint16_t EditorServer::link_port() const { return impl_->link_listener.port(); }
void EditorServer::set_pending_token(std::array<std::uint8_t,32> token) { std::lock_guard lock(impl_->mutex); impl_->token=token; }
void EditorServer::set_link_token(std::array<std::uint8_t,32> token) { std::lock_guard lock(impl_->mutex); impl_->link_token=token; }
std::optional<Command> EditorServer::poll_command() { return impl_->commands.pop(); }
std::optional<SessionEvent> EditorServer::poll_event() { return impl_->events.pop(); }
void EditorServer::command_result(Cmd cmd,Result result,EditorStateValue state) { impl_->send_app(CommandResult{cmd,result,state}); }
void EditorServer::editor_state(EditorStateValue state) {
    { std::lock_guard lock(impl_->mutex); impl_->config.host.editorState=state; }
    impl_->send_app(EditorState{state});
}
void EditorServer::set_stream_info(Encoder encoder,std::uint16_t width,std::uint16_t height,Orientation orientation,std::uint8_t fps,std::uint32_t bitrate_kbps) {
    std::lock_guard lock(impl_->mutex);
    auto& host=impl_->config.host;
    host.encoder=encoder; host.width=width; host.height=height; host.orientation=orientation; host.fps=fps; host.bitrateKbps=bitrate_kbps;
}
void EditorServer::stream_paused(PauseReason reason) { impl_->send_app(StreamPaused{reason}); }
void EditorServer::error(ErrorSource source,ErrorCode code) { impl_->send_app(Error{source,code}); }
}
