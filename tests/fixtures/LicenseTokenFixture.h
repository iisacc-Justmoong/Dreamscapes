#pragma once
// Test-only Ed25519 seed; production trusts a different public key.
#include <openssl/evp.h>
#include <memory>
#include <string>
#include <string_view>
namespace dreamscapes_license_fixture {
inline auto key() { std::string seed(32, '1'); return std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>(
    EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, reinterpret_cast<const unsigned char *>(seed.data()), seed.size()), EVP_PKEY_free); }
inline std::string publicKey() { auto k = key(); std::string result(32, '\0'); size_t n = result.size();
    EVP_PKEY_get_raw_public_key(k.get(), reinterpret_cast<unsigned char *>(result.data()), &n); return result; }
inline std::string base64(std::string_view input) { std::string result((input.size()+2)/3*4+1, '\0');
    const auto n=EVP_EncodeBlock(reinterpret_cast<unsigned char *>(result.data()),reinterpret_cast<const unsigned char *>(input.data()),input.size());
    result.resize(n);for(auto &c:result){if(c=='+')c='-';if(c=='/')c='_';}while(result.ends_with('='))result.pop_back();return result; }
inline std::string sign(std::string_view payload) { auto k=key(); std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(),EVP_MD_CTX_free);
    EVP_DigestSignInit(context.get(),nullptr,nullptr,nullptr,k.get());
    auto bytes=base64(R"({"alg":"EdDSA","kid":"dreamscapes-fixture","typ":"IISACC-ACCOUNT-LICENSES"})")+'.'+base64(payload);
    std::string signature(64,'\0');size_t n=signature.size();EVP_DigestSign(context.get(),reinterpret_cast<unsigned char *>(signature.data()),&n,
        reinterpret_cast<const unsigned char *>(bytes.data()),bytes.size());return bytes+'.'+base64(signature); }
}
