#include "test_support.hpp"
#include "handview/net/net.hpp"
#include <thread>
using namespace handview;
int main() {
 net::WinsockInit init; CHECK(init.valid());
 auto bytes=protocol::encode(protocol::Ping{42}); net::FrameReader reader(protocol::Channel::Control);
 unsigned count=0; for(auto b:bytes) count+=static_cast<unsigned>(reader.feed(std::span(&b,1)).size()); CHECK(count==1);
 std::vector<std::uint8_t> batch; for(int n=0;n<100;++n) batch.insert(batch.end(),bytes.begin(),bytes.end()); CHECK(reader.feed(batch).size()==100);
 int logs=0; net::FrameReader unknown(protocol::Channel::Control,[&](std::string_view){++logs;});
 auto u=protocol::encode(protocol::Unknown{0x7777,3}); unknown.feed(u); unknown.feed(u); CHECK(logs==1);
 std::array<std::uint8_t,6> large{}; protocol::encode_header({1,protocol::kMaxControlPayload+1},large.data()); reader.feed(large); CHECK(reader.terminal());
 net::FrameReader bad_size(protocol::Channel::Control); auto bad=protocol::encode(protocol::Ping{0}); bad[2]=7; bad.pop_back(); bad_size.feed(bad); CHECK(bad_size.error()==protocol::DecodeStatus::BadSize);
 net::FrameReader bad_enum(protocol::Channel::Control); auto invalid=protocol::encode(protocol::Command{protocol::Cmd::Play}); invalid.back()=0; bad_enum.feed(invalid); CHECK(bad_enum.error()==protocol::DecodeStatus::BadEnum);
 auto listener=net::listen_loopback(0); CHECK(listener.port()!=0); auto client=net::connect_loopback(listener.port()); auto server=net::accept(listener); CHECK(client && server);
 net::FrameWriter writer(client); unsigned total=0; bool valid=true;
 std::thread receiver([&]{net::FrameReader r(protocol::Channel::Control); std::array<std::uint8_t,4096> buffer{};
 while(total<400) {int n=net::recv_some(server,buffer,net::Milliseconds{2000}); if(n<=0)break; for(const auto& f:r.feed(std::span(buffer.data(),static_cast<std::size_t>(n)))) {valid=valid && std::holds_alternative<protocol::Ping>(f.message) && std::get<protocol::Ping>(f.message).timeUs==42; ++total;}} valid=valid && !r.terminal();});
 std::vector<std::thread> threads; for(int t=0;t<4;++t)threads.emplace_back([&]{for(int n=0;n<100;++n)writer.send(protocol::Ping{42});});
 for(auto& t:threads)t.join(); receiver.join(); CHECK(total==400 && valid);
 int read_result=-1; std::thread blocked([&]{std::array<std::uint8_t,1> b{}; read_result=net::recv_some(server,b,net::Milliseconds{2000});}); server.close(); blocked.join(); CHECK(read_result==0);
 return test_result("net");
}
