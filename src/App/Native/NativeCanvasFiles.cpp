#include "NativeCanvasFiles.h"
#include <File/DocumentFile.h>
#include <Media/MediaIo.h>
#include <iiFileProvider>
#include <iiVisualCodec>
#include <algorithm>
#include <cctype>
#include <exception>
#include <optional>
#include <utility>

namespace dreamscapes::native {
namespace {
using ii::core::ErrorCode;
using iiSharedCanvas::DocumentFile;
using iiSharedCanvas::DocumentFileCode;
std::string utf8(const std::filesystem::path &path) {
    const auto text=path.u8string(); return {text.begin(),text.end()};
}
std::string extension(const std::filesystem::path &path) {
    auto suffix=path.extension().string();
    std::ranges::transform(suffix,suffix.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return suffix;
}
bool validPath(const std::filesystem::path &path) {
    if (!path.is_absolute() || path.filename().empty() || path.lexically_normal()!=path) return false;
    return true;
}
bool valid(const std::filesystem::path &path) {return validPath(path) && extension(path)==".iisc";}
ii::core::ErrorCode errorCode(DocumentFileCode code) {
    switch (code) {
    case DocumentFileCode::AlreadyExists:return ErrorCode::already_exists;
    case DocumentFileCode::InvalidArgument:
    case DocumentFileCode::InvalidPath:
    case DocumentFileCode::InvalidDocument:return ErrorCode::invalid_argument;
    case DocumentFileCode::LimitExceeded:return ErrorCode::out_of_range;
    case DocumentFileCode::UnsupportedFormat:return ErrorCode::unsupported;
    case DocumentFileCode::Conflict:return ErrorCode::busy;
    default:return ErrorCode::failed;
    }
}
template<class T> ii::core::Result<T> failure(const iiSharedCanvas::DocumentFileResult &result) {
    return ii::core::fail(errorCode(result.code),result.message.empty()?"Canvas file operation failed":result.message);
}
ii::core::Result<CanvasFileVersion> currentVersion(const std::filesystem::path &path,
                                                   std::optional<std::uint64_t> expected={}) {
    try {
        const auto before=iiFileProvider::ObjectSource::inspect(path);
        iiSharedCanvas::DocumentFileInfo info;
        const auto status=DocumentFile::readStorageInfo(utf8(path),info);
        if (!status.ok()) return failure<CanvasFileVersion>(status);
        const auto after=iiFileProvider::ObjectSource::inspect(path);
        if (before!=after || (expected && info.revision!=*expected))
            return ii::core::fail(ErrorCode::busy,"Canvas changed while its saved version was checked");
        return CanvasFileVersion{path,after,info.revision,true};
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot inspect the canvas file",std::current_exception());}
}
}
bool isCanvasImage(const std::filesystem::path &path) {
    if (!validPath(path)) return false;
    const auto suffix=extension(path);
    return suffix==".png" || suffix==".jpg" || suffix==".jpeg" || suffix==".bmp" ||
        suffix==".webp" || suffix==".tif" || suffix==".tiff" || suffix==".tga";
}
bool supportsCanvasInput(const std::filesystem::path &path) {return valid(path) || isCanvasImage(path);}
ii::core::Result<CanvasFileResult> importCanvasImage(const std::filesystem::path &path) {
    if (!isCanvasImage(path)) return ii::core::fail(ErrorCode::invalid_argument,"Choose an absolute supported image file path");
    try {
        const auto identity=iiFileProvider::ObjectSource::inspect(path);
        const iiSharedCanvas::MediaLimits bounds;
        ii::visual::image::Limits limits;
        limits.input_bytes=static_cast<std::size_t>(bounds.maxInputBytes);
        limits.pixel_bytes=static_cast<std::size_t>(std::min(bounds.maxDecodedBytes,bounds.maxPixelsPerFrame*4));
        const auto bytes=iiFileProvider::native::File::read(path,limits.input_bytes);
        const auto input=std::span(reinterpret_cast<const std::uint8_t *>(bytes.data()),bytes.size());
        auto source=ii::visual::image::decode(input,limits);
        if (source && !source->metadata.icc_profile.empty()) source=ii::visual::image::to_srgb(std::move(*source),limits);
        if (!source) {
            const auto code=source.error().code==ii::visual::image::ErrorCode::limit_exceeded?ErrorCode::out_of_range:
                source.error().code==ii::visual::image::ErrorCode::unsupported_format?ErrorCode::unsupported:ErrorCode::failed;
            return ii::core::fail(code,source.error().message.empty()?"Cannot decode the image":source.error().message);
        }
        const auto pixelCount=std::uint64_t(source->width)*source->height;
        if (!source->width || !source->height || source->width>limits.dimension || source->height>limits.dimension ||
            pixelCount>bounds.maxPixelsPerFrame || source->rgba.size()!=pixelCount*4)
            return ii::core::fail(ErrorCode::invalid_argument,"Image decoder returned invalid pixels");
        if (iiFileProvider::ObjectSource::inspect(path)!=identity)
            return ii::core::fail(ErrorCode::busy,"Image changed while it was being opened");
        iiSharedCanvas::Document document;
        document.extent={static_cast<std::int32_t>(source->width),static_cast<std::int32_t>(source->height)};
        const auto fps=document.timeline.frameRate;
        if (!fps.numerator || !fps.denominator) return ii::core::fail(ErrorCode::invalid_argument,"The default frame rate is invalid");
        document.timeline.frameCount=static_cast<iiSharedCanvas::FrameIndex>(
            (std::uint64_t(fps.numerator)+fps.denominator-1)/fps.denominator);
        RasterLayer pixels=makeRasterLayer(document.extent.width,document.extent.height,0);
        for (std::size_t i=0;i<pixels.pixels.size();++i) {
            const auto *rgba=source->rgba.data()+4*i;
            pixels.pixels[i]=(std::uint32_t(rgba[3])<<24)|(std::uint32_t(rgba[0])<<16)|
                (std::uint32_t(rgba[1])<<8)|rgba[2];
        }
        document.assets.emplace_back(iiSharedCanvas::RasterAsset{"image",std::move(pixels)});
        document.layers.emplace_back(iiSharedCanvas::StaticBitmapLayer{{"image.layer",utf8(path.filename())},
                                                                       iiSharedCanvas::StaticSource{"image"}});
        const std::string warning=source->metadata.dots_per_meter_x || source->metadata.dots_per_meter_y?
            "Print resolution is not retained by the canvas pixel model":std::string{};
        return CanvasFileResult{std::move(document),CanvasFileVersion{path,identity,0,false},std::move(warning)};
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot open the image file",std::current_exception());}
}
ii::core::Result<CanvasFileResult> openCanvasFile(const std::filesystem::path &path) {
    if (!valid(path)) return ii::core::fail(ErrorCode::invalid_argument,"Choose an absolute .iisc file path");
    try {
        const auto identity=iiFileProvider::ObjectSource::inspect(path);
        iiSharedCanvas::DocumentFileInfo before;
        const auto info=DocumentFile::readStorageInfo(utf8(path),before);
        const bool working=info.ok();
        if (!working && info.code!=DocumentFileCode::UnsupportedFormat) return failure<CanvasFileResult>(info);
        iiSharedCanvas::Document document;
        const auto read=DocumentFile::readSnapshot(utf8(path),document);
        if (!read.ok()) return failure<CanvasFileResult>(read);
        if (iiFileProvider::ObjectSource::inspect(path)!=identity)
            return ii::core::fail(ErrorCode::busy,"Canvas changed while it was being opened");
        if (working) {
            iiSharedCanvas::DocumentFileInfo after;
            const auto checked=DocumentFile::readStorageInfo(utf8(path),after);
            if (!checked.ok()) return failure<CanvasFileResult>(checked);
            if (before.revision!=after.revision || before.storageVersion!=after.storageVersion ||
                iiFileProvider::ObjectSource::inspect(path)!=identity)
                return ii::core::fail(ErrorCode::busy,"Canvas changed while it was being opened");
        }
        return CanvasFileResult{std::move(document),CanvasFileVersion{path,identity,working?before.revision:0,working}};
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot open the canvas file",std::current_exception());}
}
ii::core::Result<CanvasFileResult> createCanvasFile(const std::filesystem::path &path,const iiSharedCanvas::Document &document) {
    if (!valid(path)) return ii::core::fail(ErrorCode::invalid_argument,"Choose an absolute .iisc file path");
    try {
        DocumentFile file;
        const auto created=file.create(utf8(path),document);
        if (!created.ok()) return failure<CanvasFileResult>(created);
        const auto revision=file.revision();
        auto saved=*file.document();
        file.close();
        auto version=currentVersion(path,revision);
        if (!version) return ii::core::fail(version.error().code,
            "Canvas was created, but its saved version could not be verified: "+version.error().message,version.error().cause);
        return CanvasFileResult{std::move(saved),std::move(*version)};
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot create the canvas file",std::current_exception());}
}
ii::core::Result<CanvasFileResult> saveCanvasFile(const CanvasFileVersion &version,const iiSharedCanvas::Document &document) {
    if (!valid(version.path)) return ii::core::fail(ErrorCode::invalid_argument,"Choose an absolute .iisc file path");
    if (!version.working) return ii::core::fail(ErrorCode::unsupported,"Save this packed canvas as a new working file");
    try {
        const auto before=iiFileProvider::ObjectSource::inspect(version.path);
        if (before!=version.identity) return ii::core::fail(ErrorCode::busy,"Canvas file was replaced or changed; reopen before saving");
        DocumentFile file;
        const auto opened=file.open(utf8(version.path));
        if (!opened.ok()) return failure<CanvasFileResult>(opened);
        if (file.revision()!=version.revision || iiFileProvider::ObjectSource::inspect(version.path)!=before)
            return ii::core::fail(ErrorCode::busy,"Canvas file changed; reopen before saving");
        const auto saved=file.edit([&](iiSharedCanvas::Document &draft){draft=document; return true;});
        if (!saved.ok()) return failure<CanvasFileResult>(saved);
        const auto revision=file.revision();
        auto confirmed=*file.document();
        file.close();
        auto current=currentVersion(version.path,revision);
        if (!current) return ii::core::fail(current.error().code,
            "Canvas was saved, but its saved version could not be verified: "+current.error().message,current.error().cause);
        return CanvasFileResult{std::move(confirmed),std::move(*current)};
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot save the canvas file",std::current_exception());}
}
}
