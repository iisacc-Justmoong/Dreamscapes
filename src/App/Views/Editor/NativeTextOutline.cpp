#include "NativeTextTool.h"
#include <iiSDKCore>
#include <cmath>
#if defined(__APPLE__)
#include <CoreText/CoreText.h>
#include <CoreGraphics/CoreGraphics.h>
#endif

namespace dreamscapes::editor {
#if defined(__APPLE__)
namespace {
template<class T> class Owned final {
  public:
    explicit Owned(T value):value_(value) {}
    ~Owned() {if (value_) CFRelease(value_);}
    Owned(const Owned &)=delete;Owned &operator=(const Owned &)=delete;
    T get() const {return value_;}
  private:T value_;
};
void collect(void *info,const CGPathElement *element) {
    using namespace iiSharedCanvas;
    auto &commands=*static_cast<std::vector<PathCommand> *>(info);
    const auto point=[](CGPoint value){return iiSharedCanvas::Point{value.x,value.y};};
    switch (element->type) {
        case kCGPathElementMoveToPoint:commands.emplace_back(MoveTo{point(element->points[0])});break;
        case kCGPathElementAddLineToPoint:commands.emplace_back(LineTo{point(element->points[0])});break;
        case kCGPathElementAddQuadCurveToPoint:commands.emplace_back(QuadraticTo{point(element->points[0]),point(element->points[1])});break;
        case kCGPathElementAddCurveToPoint:commands.emplace_back(CubicTo{point(element->points[0]),point(element->points[1]),point(element->points[2])});break;
        case kCGPathElementCloseSubpath:commands.emplace_back(ClosePath{});break;
    }
}
}
#endif
bool nativeTextOutlineAvailable() noexcept {
#if defined(__APPLE__)
    return true;
#else
    return false;
#endif
}
ii::core::Result<std::vector<iiSharedCanvas::VectorPath>> nativeTextOutline(const TextOutlineRequest &request) {
    using ii::core::ErrorCode;
    if (request.content.empty() || request.content.size()>32000 || !ii::core::valid_utf8(request.content) ||
        request.content.find('\0')!=request.content.npos || request.family.empty() || !ii::core::valid_utf8(request.family) ||
        !std::isfinite(request.size) || request.size<=0 || !std::isfinite(request.origin.x) || !std::isfinite(request.origin.y) ||
        !std::isfinite(request.maxWidth) || !std::isfinite(request.maxHeight) || request.maxWidth<=0 || request.maxHeight<=0)
        return ii::core::fail(ErrorCode::invalid_argument,"Invalid text or text placement");
#if defined(__APPLE__)
    Owned text(CFStringCreateWithBytes(nullptr,reinterpret_cast<const UInt8 *>(request.content.data()),
        static_cast<CFIndex>(request.content.size()),kCFStringEncodingUTF8,false));
    Owned family(CFStringCreateWithBytes(nullptr,reinterpret_cast<const UInt8 *>(request.family.data()),
        static_cast<CFIndex>(request.family.size()),kCFStringEncodingUTF8,false));
    if (!text.get() || !family.get()) return ii::core::fail(ErrorCode::failed,"Cannot prepare the text");
    Owned font(CTFontCreateWithName(family.get(),request.size,nullptr));
    if (!font.get()) return ii::core::fail(ErrorCode::failed,"Cannot load the selected font");
    const void *keys[]={kCTFontAttributeName};const void *values[]={font.get()};
    Owned attributes(CFDictionaryCreate(nullptr,keys,values,1,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks));
    if (!attributes.get()) return ii::core::fail(ErrorCode::failed,"Cannot prepare text attributes");
    Owned attributed(CFAttributedStringCreate(nullptr,text.get(),attributes.get()));
    if (!attributed.get()) return ii::core::fail(ErrorCode::failed,"Cannot prepare the text layout");
    Owned framesetter(CTFramesetterCreateWithAttributedString(attributed.get()));
    Owned bounds(CGPathCreateWithRect(CGRectMake(0,0,request.maxWidth,request.maxHeight),nullptr));
    if (!framesetter.get() || !bounds.get()) return ii::core::fail(ErrorCode::failed,"Cannot prepare the text frame");
    Owned frame(CTFramesetterCreateFrame(framesetter.get(),CFRangeMake(0,0),bounds.get(),nullptr));
    if (!frame.get()) return ii::core::fail(ErrorCode::failed,"Cannot lay out the text");
    const auto visible=CTFrameGetVisibleStringRange(frame.get());
    if (visible.location+visible.length<CFStringGetLength(text.get()))
        return ii::core::fail(ErrorCode::out_of_range,"The text does not fit from the selected canvas position");
    const auto lines=CTFrameGetLines(frame.get());
    const auto count=CFArrayGetCount(lines);
    std::vector<CGPoint> origins(static_cast<std::size_t>(count));
    if (count) CTFrameGetLineOrigins(frame.get(),CFRangeMake(0,0),origins.data());
    std::vector<iiSharedCanvas::VectorPath> result;
    for (CFIndex lineIndex=0;lineIndex<count;++lineIndex) {
        const auto line=static_cast<CTLineRef>(CFArrayGetValueAtIndex(lines,lineIndex));
        const auto runs=CTLineGetGlyphRuns(line);
        for (CFIndex runIndex=0;runIndex<CFArrayGetCount(runs);++runIndex) {
            const auto run=static_cast<CTRunRef>(CFArrayGetValueAtIndex(runs,runIndex));
            const auto runFont=static_cast<CTFontRef>(CFDictionaryGetValue(CTRunGetAttributes(run),kCTFontAttributeName));
            if (!runFont) return ii::core::fail(ErrorCode::failed,"A shaped text run has no font");
            const auto glyphCount=CTRunGetGlyphCount(run);
            std::vector<CGGlyph> glyphs(static_cast<std::size_t>(glyphCount));
            std::vector<CGPoint> positions(static_cast<std::size_t>(glyphCount));
            if (glyphCount) {CTRunGetGlyphs(run,CFRangeMake(0,0),glyphs.data());CTRunGetPositions(run,CFRangeMake(0,0),positions.data());}
            for (CFIndex index=0;index<glyphCount;++index) {
                if (!glyphs[index]) return ii::core::fail(ErrorCode::unsupported,"A character is not available in the selected font or its fallbacks");
                const auto transform=CGAffineTransformMake(1,0,0,-1,
                    request.origin.x+origins[lineIndex].x+positions[index].x,
                    request.origin.y+request.maxHeight-origins[lineIndex].y-positions[index].y);
                Owned path(CTFontCreatePathForGlyph(runFont,glyphs[index],&transform));
                if (!path.get()) {
                    const auto rect=CTFontGetBoundingRectsForGlyphs(runFont,kCTFontOrientationDefault,&glyphs[index],nullptr,1);
                    if (!CGRectIsEmpty(rect)) return ii::core::fail(ErrorCode::unsupported,"The selected font contains a glyph without a vector outline");
                    continue;
                }
                iiSharedCanvas::VectorPath vector;vector.fill=iiSharedCanvas::SolidPaint{request.color};
                CGPathApply(path.get(),&vector.commands,collect);
                if (!vector.commands.empty()) {
                    if (result.size()>=128) return ii::core::fail(ErrorCode::out_of_range,"A text layer supports up to 128 outlined glyphs");
                    result.push_back(std::move(vector));
                }
            }
        }
    }
    if (result.empty()) return ii::core::fail(ErrorCode::invalid_argument,"The text has no visible glyphs");
    return result;
#else
    (void)request;
    return ii::core::fail(ErrorCode::unsupported,"Text outlines are unavailable on this platform");
#endif
}
}
