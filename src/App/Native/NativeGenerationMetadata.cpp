#include "NativeGenerationMetadata.h"
#include <iiSDKCore>
#include <zlib.h>
#include <algorithm>
#include <array>
#include <cstring>

namespace dreamscapes::native {
namespace {
constexpr std::size_t limit=64*1024*1024,recipeLimit=1024*1024;
constexpr std::string_view keyword="Dreamscapes.Parameters";
std::uint32_t read32(const std::uint8_t *data) {
    return (std::uint32_t(data[0])<<24)|(std::uint32_t(data[1])<<16)|(std::uint32_t(data[2])<<8)|data[3];
}
void write32(std::vector<std::uint8_t> &out,std::uint32_t value) {
    for (int i=3;i>=0;--i) out.push_back(std::uint8_t(value>>(i*8)));
}
}
ii::core::Result<std::vector<std::uint8_t>> addGenerationRecipe(std::span<const std::uint8_t> png,std::string_view json) {
    using ii::core::ErrorCode;
    static constexpr std::array<std::uint8_t,8> signature{137,80,78,71,13,10,26,10};
    if (png.size()>limit || json.size()>recipeLimit || png.size()<signature.size() ||
        !std::ranges::equal(png.first(signature.size()),signature) || !ii::core::valid_utf8(json) || json.find('\0')!=json.npos)
        return ii::core::fail(ErrorCode::invalid_argument,"Invalid PNG or generation recipe");
    if (json.size()+keyword.size()+17>limit-png.size())
        return ii::core::fail(ErrorCode::out_of_range,"Generation metadata exceeds the PNG size limit");
    std::vector<std::uint8_t> output(signature.begin(),signature.end());
    bool header=false,pixels=false,end=false;
    for (std::size_t offset=signature.size();offset<png.size();) {
        if (png.size()-offset<12) return ii::core::fail(ErrorCode::invalid_argument,"Truncated PNG chunk");
        const auto length=std::size_t(read32(png.data()+offset));
        if (length>png.size()-offset-12) return ii::core::fail(ErrorCode::invalid_argument,"Invalid PNG chunk length");
        const auto *type=png.data()+offset+4,*data=type+4;
        if (read32(data+length)!=std::uint32_t(crc32(0,type,uInt(length+4))))
            return ii::core::fail(ErrorCode::invalid_argument,"Invalid PNG chunk checksum");
        const auto is=[&](const char *name){return std::memcmp(type,name,4)==0;};
        if (!header) {
            if (!is("IHDR") || length!=13) return ii::core::fail(ErrorCode::invalid_argument,"Invalid PNG header");
            header=true;
        } else if (is("IHDR")) return ii::core::fail(ErrorCode::invalid_argument,"Duplicate PNG header");
        if (is("IDAT")) pixels=true;
        const bool text=is("tEXt") || is("zTXt") || is("iTXt");
        const bool replace=text && length>keyword.size() && data[keyword.size()]==0 &&
            std::memcmp(data,keyword.data(),keyword.size())==0;
        if (is("IEND")) {
            if (!pixels || length!=0 || offset+12!=png.size()) return ii::core::fail(ErrorCode::invalid_argument,"Invalid PNG end");
            const auto payload=keyword.size()+5+json.size();
            write32(output,std::uint32_t(payload));const auto start=output.size();
            output.insert(output.end(),{'i','T','X','t'});output.insert(output.end(),keyword.begin(),keyword.end());
            output.insert(output.end(),5,0);output.insert(output.end(),json.begin(),json.end());
            write32(output,std::uint32_t(crc32(0,output.data()+start,uInt(payload+4))));
            end=true;
        }
        if (!replace) output.insert(output.end(),png.begin()+offset,png.begin()+offset+length+12);
        offset+=length+12;
    }
    if (!end) return ii::core::fail(ErrorCode::invalid_argument,"PNG has no end chunk");
    return output;
}
}
