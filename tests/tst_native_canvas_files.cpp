#include "App/Native/NativeCanvasFiles.h"
#include <Document/DocumentEditor.h>
#include <File/DocumentFile.h>
#include <Serialization/IiscCodec.h>
#include <iiFileProvider>
#include <filesystem>
#include <iostream>
#include <stdexcept>

template<class T> T take(ii::core::Result<T> result) {
    if (!result) throw std::runtime_error(result.error().message);
    return std::move(*result);
}
void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
int main() {
    try {
        using namespace dreamscapes::native;
        using namespace iiSharedCanvas;
        namespace fs=std::filesystem;
        iiFileProvider::native::TemporaryDirectory temporary(fs::canonical(DREAMSCAPES_TEST_RESOURCES));
        check(temporary.isValid(),"cannot create isolated file fixture");
        const auto path=temporary.path()/"source.iisc";
        Document doc; doc.extent={8,6};
        doc.publicationMetadata=PublicationMetadata{.title="Original",.settings={{"foreign.option",std::string("kept")}}};
        auto original=take(createCanvasFile(path,doc));
        auto loaded=take(openCanvasFile(path));
        check(loaded.version.working && loaded.document.publicationMetadata->title=="Original",
              "native working file was not read");
        DocumentEditor editor(loaded.document);
        auto metadata=*loaded.document.publicationMetadata; metadata.title="Edited";
        check(editor.setPublicationMetadata(metadata).ok(),"cannot edit fixture metadata");
        auto saved=take(saveCanvasFile(loaded.version,loaded.document));
        auto reopened=take(openCanvasFile(path));
        check(reopened.document.publicationMetadata->title=="Edited" &&
              std::get<std::string>(reopened.document.publicationMetadata->settings.at("foreign.option"))=="kept",
              "save did not persist the actual shared document metadata");
        DocumentFile concurrent;
        check(concurrent.open(path.string()).ok(),"cannot open concurrent writer");
        check(concurrent.edit([](Document &value) {value.publicationMetadata->description="External"; return true;}).ok(),
              "cannot perform concurrent fixture edit");
        concurrent.close();
        check(!saveCanvasFile(saved.version,loaded.document),"stale save was accepted");
        check(take(openCanvasFile(path)).document.publicationMetadata->description=="External",
              "a stale save overwrote external work");
        auto beforeReplacement=take(openCanvasFile(path));
        const auto replacement=temporary.path()/"replacement.iisc";
        take(createCanvasFile(replacement,doc));
        fs::rename(path,temporary.path()/"original-preserved.iisc");
        fs::rename(replacement,path);
        check(!saveCanvasFile(beforeReplacement.version,loaded.document),"a replaced file was overwritten");
        check(take(openCanvasFile(path)).document.publicationMetadata->title=="Original",
              "replacement file was modified by a stale save");
        const auto packed=temporary.path()/"legacy.iisc";
        const auto encoded=encodeIisc(doc); check(encoded.ok(),"cannot encode packed fixture");
        const std::string bytes(reinterpret_cast<const char *>(encoded.bytes.data()),encoded.bytes.size());
        iiFileProvider::native::File::create(packed,bytes);
        auto legacy=take(openCanvasFile(packed));
        check(!legacy.version.working && !saveCanvasFile(legacy.version,loaded.document),
              "a packed snapshot was silently overwritten");
        const auto converted=temporary.path()/"saved-as.iisc";
        take(createCanvasFile(converted,legacy.document));
        check(iiFileProvider::native::File::read(packed)==bytes && !createCanvasFile(converted,legacy.document),
              "Save As modified the original or overwrote an existing destination");
        check(!openCanvasFile(fs::path("relative.iisc")),"relative input path was accepted");
    } catch (const std::exception &error) {std::cerr << error.what() << '\n'; return 1;}
}
