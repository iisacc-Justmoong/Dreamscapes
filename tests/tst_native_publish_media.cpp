#include "App/Views/Publishing/NativePublishMedia.h"
#include <iiFileProvider>
#include <iiVisualCodec>
#include <filesystem>
#include <iostream>
#include <stdexcept>

void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
int main() {
    try {
        using namespace dreamscapes::publishing;using namespace iiSharedCanvas;
        iiFileProvider::native::TemporaryDirectory fixture(std::filesystem::canonical(DREAMSCAPES_TEST_RESOURCES));
        check(fixture.isValid(),"cannot create publication fixture");
        auto doc=std::make_shared<Document>();doc->extent={4,2};doc->timeline.frameCount=2;
        doc->artboards={{"red","Red",{{0,0},{2,2}},0xffff0000,true},
                        {"green","Green",{{2,0},{2,2}},0xff00ff00,true}};
        doc->publicationMetadata=PublicationMetadata{.title="Campaign",.description="Shared description",
            .canvases={{"green","Green caption","Green alternative"},{"red","Red caption","Red alternative"}}};
        const auto original=*doc;std::filesystem::path directory;
        {
            auto result=preparePublicationMedia(doc,0,fixture.path());
            check(bool(result),"valid IISC publication media could not be prepared");
            check(result->source.post.media.size()==2 && result->source.captions==std::vector<std::string>({"Green caption","Red caption"}) &&
                  result->source.post.media[0].altText=="Green alternative" && result->source.post.media[1].altText=="Red alternative",
                  "publication canvas ordering, captions or alternative text was lost");
            for (const auto &media:result->source.post.media)
                check(media.kind==iiSocialIntegration::MediaKind::Image && media.mimeType=="image/png" &&
                      media.publicUrl.empty() && std::filesystem::is_regular_file(media.localFile),"prepared media did not reference a local PNG");
            const auto bytes=iiFileProvider::native::File::read(result->source.post.media[0].localFile,1024*1024);
            auto decoded=ii::visual::image::decode(std::span(reinterpret_cast<const std::uint8_t *>(bytes.data()),bytes.size()));
            check(decoded && decoded->width==2 && decoded->height==2 && decoded->rgba[0]==0 && decoded->rgba[1]==255,
                  "the ordered first media was not rendered from the selected artboard");
            directory=result->source.post.media[0].localFile.parent_path();
            check(doc->artboards==original.artboards && doc->publicationMetadata==original.publicationMetadata,
                  "preparing publication media modified the IISC document");
        }
        check(!std::filesystem::exists(directory),"retired publication staging was not cleaned up");
        check(!preparePublicationMedia(doc,2,fixture.path()),"out-of-range publication frame was accepted");
        check(!preparePublicationMedia(doc,0,fixture.path(),[]{return true;}),"cancelled publication preparation continued");
        auto missing=*doc;missing.publicationMetadata->canvases={{"missing","",""}};
        check(!preparePublicationMedia(std::make_shared<Document>(missing),0,fixture.path()),"missing publication artboard was accepted");
        auto root=*doc;root.artboards.push_back({"far","Far",{{10,0},{2,2}},0xff0000ff,true});
        root.publicationMetadata->canvases={{"","Root caption","Root alternative"}};
        auto rootMedia=preparePublicationMedia(std::make_shared<Document>(root),0,fixture.path());
        check(bool(rootMedia),"root canvas failed when an artboard extended outside it");
        const auto rootBytes=iiFileProvider::native::File::read(rootMedia->source.post.media.front().localFile,1024*1024);
        auto rootImage=ii::visual::image::decode(std::span(reinterpret_cast<const std::uint8_t *>(rootBytes.data()),rootBytes.size()));
        check(rootImage && rootImage->width==4 && rootImage->height==2,
              "the root canvas included an unrelated outlying artboard");
        auto large=std::make_shared<Document>();large->extent={32769,1};
        const auto overDimension=preparePublicationMedia(large,0,fixture.path());
        check(!overDimension && overDimension.error().code==ii::core::ErrorCode::out_of_range,
              "oversized output was not rejected before rendering");
        PublicationMediaLimits tiny;tiny.maximumTotalPixels=3;
        check(!preparePublicationMedia(doc,0,fixture.path(),{},tiny),"publication pixel budget was ignored");
        auto many=std::make_shared<Document>();many->extent={40,2};
        many->publicationMetadata=PublicationMetadata{.title="Campaign",.description="Shared description"};
        for (int i=0;i<20;++i) {
            const auto id="board-"+std::to_string(i);
            many->artboards.push_back({id,id,{{i*2,0},{2,2}},0xff123456,true});
            many->publicationMetadata->canvases.push_back({id,"Caption "+std::to_string(i),"Alt "+std::to_string(i)});
        }
        auto staged=preparePublicationMedia(many,0,fixture.path());
        check(bool(staged),"twenty publication canvases could not be prepared");
        auto plan=iiSocialIntegration::planPublication(iiSocialIntegration::Platform::X,staged->source);
        check(plan.ready() && plan.posts.size()==5,"twenty rendered canvases were not planned as five X posts");
        for (std::size_t i=0;i<plan.posts.size();++i) {
            check(plan.posts[i].post.media.size()==4 && plan.posts[i].sourceMedia.front()==i*4 &&
                  plan.posts[i].post.media.front().altText=="Alt "+std::to_string(i*4) &&
                  plan.posts[i].post.text.find("Caption "+std::to_string(i*4))!=std::string::npos &&
                  (i==0?!plan.posts[i].replyTo:plan.posts[i].replyTo==std::optional<std::size_t>{i-1}),
                  "rendered media order, captions, alt text or thread linkage changed");
        }
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
