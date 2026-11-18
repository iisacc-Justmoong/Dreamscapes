#include "NativeResidentCommand.h"
#include "App/ViewModels/Native/ViewValue.h"
#include <iiSocietyHelper>
#include <iiJson>
#include <utility>

namespace dreamscapes::native {
ResidentCommandResult residentCommand(std::string_view prompt,const std::filesystem::path &broker)
{
    if (prompt.empty() || prompt.size()>256*1024) return {2,"The prompt must contain at most 256 KiB.\n"};
    iiSocietyHelper::ResidentNative client;
    if (const auto opened=client.connect(broker); !opened) return {1,opened.error()+"\n"};
    if (const auto subscribed=client.subscribe(); !subscribed) return {1,subscribed.error()+"\n"};
    std::string ticket;
    for (;;) {
        const auto frame=client.receiveJson();
        if (!frame) return {1,frame.error()+"\n"};
        const auto message=ii::json::parse(*frame);
        if (!message) return {1,"The Society daemon returned invalid JSON.\n"};
        const auto type=vm::text(vm::get(*message,"type"));
        const auto messageId=vm::text(vm::get(*message,"id"));
        const auto &resident=vm::get(*message,"resident");
        const auto state=vm::text(vm::get(resident,"state"));
        if (ticket.empty() && type=="result" && !vm::boolean(vm::get(*message,"accepted"))) return {1,*frame+"\n"};
        if (ticket.empty() && (type=="result" || type=="iisaccsonState") && !state.empty()) {
            if (state=="error" || state=="unavailable") {
                auto reason=vm::text(vm::get(resident,"error"),"The resident model is unavailable.");
                return {1,std::move(reason)+"\n"};
            }
            if (state=="ready") {
                const auto request=client.generate({{"user",std::string(prompt)}});
                if (!request) return {1,request.error()+"\n"};
                ticket=*request;
            }
        } else if (!ticket.empty() && messageId==ticket && type=="result" && !vm::boolean(vm::get(*message,"accepted"))) {
            return {1,*frame+"\n"};
        } else if (!ticket.empty() && messageId==ticket && type=="iisaccsonEvent") {
            const auto &event=vm::get(*message,"event");
            if (vm::text(vm::get(event,"kind"))=="finished") {
                const auto reason=vm::text(vm::get(event,"finish_reason"));
                return {(reason=="stop" || reason=="length")?0:1,*frame+"\n"};
            }
        }
    }
}
}
