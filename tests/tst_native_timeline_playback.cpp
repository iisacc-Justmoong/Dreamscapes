#include "App/Views/Editor/NativeTimelinePlayback.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
int main() {
    try {
        using namespace dreamscapes;
        using namespace std::chrono_literals;
        auto document=native::DocumentSession::create();
        ii::runtime::EventLoop loop;
        auto now=ii::runtime::Clock::now();
        auto playback=editor::TimelinePlayback::create(document,loop,[&]{return now;});
        check(!playback->play(),"empty timeline was playable");
        iiSharedCanvas::Document input; input.extent={4,4}; input.timeline={{24000,1001},4};
        good(document->attach(input,"movie.iisc"));
        const auto revision=document->revision.get();
        good(playback->play());
        check(playback->playing.get() && document->frame.get()==0,"playback did not start from the current frame");
        now+=42ms; good(playback->advance());
        check(document->frame.get()==1 && playback->playing.get(),"rational frame rate was not respected");
        now+=84ms; good(playback->advance());
        check(document->frame.get()==3 && playback->playing.get(),"late ticks did not skip to the current frame");
        now+=42ms; good(playback->advance());
        check(!playback->playing.get() && document->frame.get()==3,"playback did not stop on the final frame");
        check(!document->dirty.get() && document->revision.get()==revision,"playback modified the canvas document");
        good(playback->play());
        check(document->frame.get()==0 && playback->playing.get(),"replaying from the end did not rewind");
        good(document->seek(2));
        check(!playback->playing.get(),"manual seek did not pause playback");
        good(playback->play());
        iiSharedCanvas::Document replacement; replacement.extent={2,2};
        good(document->attach(replacement,"still.iisc"));
        check(!playback->playing.get() && !playback->play(),"playback survived a document replacement");
        good(document->attach(input,"reentrant.iisc"));
        good(playback->play());
        auto externalSeek=document->frame.observe([&](std::int64_t frame,std::uint64_t) {
            if (frame==1) good(document->seek(2));
        });
        now+=42ms; good(playback->advance());
        check(document->frame.get()==2 && !playback->playing.get(),"reentrant manual seek was overwritten or did not pause");
        playback->close();
        check(!playback->play(),"closed playback accepted a command");
        ii::runtime::EventLoop actualLoop;
        auto actualDocument=native::DocumentSession::create();
        input.timeline={{1000,1},4};
        good(actualDocument->attach(input,"scheduled.iisc"));
        auto scheduled=editor::TimelinePlayback::create(actualDocument,actualLoop);
        good(scheduled->play());
        std::this_thread::sleep_for(30ms);
        (void)actualLoop.poll();
        check(actualDocument->frame.get()==3 && !scheduled->playing.get(),"runtime timer did not advance playback");
        scheduled->close();
    } catch (const std::exception &error) {std::cerr << error.what() << '\n'; return 1;}
}
