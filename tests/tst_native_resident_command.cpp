#include "App/Native/NativeResidentCommand.h"
#include <iiJson>
#include "App/ViewModels/Native/ViewValue.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace vm=dreamscapes::vm;
static std::string receive(int socket) {
    unsigned char h[4]; assert(::recv(socket,h,4,MSG_WAITALL)==4);
    const auto n=(unsigned(h[0])<<24)|(unsigned(h[1])<<16)|(unsigned(h[2])<<8)|unsigned(h[3]);
    assert(n>0 && n<512*1024); std::string value(n,'\0'); assert(::recv(socket,value.data(),n,MSG_WAITALL)==ssize_t(n)); return value;
}
static void send(int socket,const std::string &value) {
    const auto n=unsigned(value.size()); unsigned char h[]{static_cast<unsigned char>(n>>24),static_cast<unsigned char>(n>>16),
        static_cast<unsigned char>(n>>8),static_cast<unsigned char>(n)};
    assert(::send(socket,h,4,0)==4); assert(::send(socket,value.data(),value.size(),0)==ssize_t(value.size()));
}
int main() {
    namespace fs=std::filesystem;
    const auto root=fs::path(DREAMSCAPES_TEST_RESOURCES)/("resident-"+std::to_string(::getpid()));
    assert(fs::create_directory(root)); ::chmod(root.c_str(),0700);
    const auto path=root/"backend.sock"; const int server=::socket(AF_UNIX,SOCK_STREAM,0); assert(server>=0);
    sockaddr_un addr{}; addr.sun_family=AF_UNIX; assert(path.string().size()<sizeof(addr.sun_path));
    std::strcpy(addr.sun_path,path.c_str()); assert(::bind(server,reinterpret_cast<sockaddr *>(&addr),sizeof(addr))==0);
    assert(::chmod(path.c_str(),0600)==0); assert(::listen(server,1)==0);
    std::jthread daemon([&] {
        const int socket=::accept(server,nullptr,nullptr); assert(socket>=0);
        send(socket,R"({"type":"state","state":{}})");
        auto sub=ii::json::parse(receive(socket)); assert(sub);
        const auto subId=vm::text(vm::get(*sub,"id"));
        send(socket,"{\"type\":\"result\",\"id\":\""+subId+"\",\"accepted\":true,\"resident\":{\"state\":\"loading\"}}");
        send(socket,R"({"type":"iisaccsonState","resident":{"state":"ready"}})");
        auto request=ii::json::parse(receive(socket)); assert(request);
        const auto ticket=vm::text(vm::get(*request,"id"));
        assert(vm::text(vm::get(*request,"op"))=="iisaccsonGenerate");
        const auto &messages=vm::array(vm::get(*request,"messages")); assert(!messages.empty());
        assert(vm::text(vm::get(messages.front(),"content"))=="hello");
        send(socket,"{\"type\":\"result\",\"id\":\""+ticket+"\",\"accepted\":true}");
        send(socket,"{\"type\":\"iisaccsonEvent\",\"id\":\""+ticket+"\",\"event\":{\"kind\":\"finished\",\"finish_reason\":\"stop\",\"text\":\"fixture reply\"}}");
        ::close(socket);
    });
    const auto result=dreamscapes::native::residentCommand("hello",root);
    assert(result.code==0 && result.text.find("fixture reply")!=std::string::npos);
    daemon.join();
    std::jthread failedDaemon([&] {
        const int socket=::accept(server,nullptr,nullptr); assert(socket>=0);
        send(socket,R"({"type":"state","state":{}})");
        const auto sub=ii::json::parse(receive(socket)); assert(sub);
        const auto subId=vm::text(vm::get(*sub,"id"));
        send(socket,"{\"type\":\"result\",\"id\":\""+subId+"\",\"accepted\":true,\"resident\":{\"state\":\"error\",\"error\":\"not enough memory\"}}");
        ::close(socket);
    });
    const auto failed=dreamscapes::native::residentCommand("hello",root);
    assert(failed.code!=0 && failed.text.find("not enough memory")!=std::string::npos);
    failedDaemon.join(); ::close(server); fs::remove(path); fs::remove(root);
    const auto absent=dreamscapes::native::residentCommand("hello",root);
    assert(absent.code!=0 && !absent.text.empty());
}
