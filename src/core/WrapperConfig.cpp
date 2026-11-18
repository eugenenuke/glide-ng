#include "WrapperConfig.h"
#include "IniConfigLoader.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>

namespace GlideWrapper {

const char* FormatSwSimdLevel(SwSimdLevel level) {
    switch (level) {
        case SwSimdLevel::Auto:   return "auto";
        case SwSimdLevel::Scalar: return "scalar";
        case SwSimdLevel::Avx2:   return "avx2";
        case SwSimdLevel::Avx512: return "avx512";
    }
    return "avx2";
}

bool ParseSwSimdLevel(const std::string& raw, SwSimdLevel& out) {
    std::string s;
    s.reserve(raw.size());
    for (unsigned char ch : raw) {
        if (!std::isspace(ch)) {
            s.push_back(static_cast<char>(std::tolower(ch)));
        }
    }
    if (s.empty()) return false;
    if (s == "0" || s == "false" || s == "off" || s == "no" || s == "none" ||
        s == "scalar") {
        out = SwSimdLevel::Scalar;
        return true;
    }
    if (s == "1" || s == "true" || s == "on" || s == "yes" || s == "avx2" ||
        s == "avx2+fma" || s == "avx2_fma") {
        out = SwSimdLevel::Avx2;
        return true;
    }
    if (s == "avx512" || s == "avx-512" || s == "avx_512" || s == "avx512f") {
        out = SwSimdLevel::Avx512;
        return true;
    }
    if (s == "auto" || s == "best" || s == "max") {
        out = SwSimdLevel::Auto;
        return true;
    }
    return false;
}

struct GatingRule {
    ApiVersion minVersion;
    CardModel minCard;
};

// Autonomic, spec-faithful feature gating registry
static const std::unordered_map<std::string, GatingRule> s_gatingRegistry = {
    // 2.42 Features (Voodoo2 Multitexturing)
    {"grTexMultibase",                  {ApiVersion::GLIDE_2_42, CardModel::Voodoo2}},
    {"grTexMultibaseAddress",           {ApiVersion::GLIDE_2_42, CardModel::Voodoo2}},
    {"grTexDownloadMipMapLevelPartial", {ApiVersion::GLIDE_2_42, CardModel::Voodoo2}},
    {"grTexDownloadTablePartial",       {ApiVersion::GLIDE_2_42, CardModel::Voodoo2}},

    // 2.43 Features (Voodoo2 / Late SST-1 Extensions)
    {"grChromakeyRangeExt",             {ApiVersion::GLIDE_2_43, CardModel::Voodoo2}},        // Range match requires Voodoo2 in Glide 2.x
    {"grChromaRangeModeExt",            {ApiVersion::GLIDE_2_43, CardModel::VoodooGraphics}}, // Glide 3.x CHROMARANGE
    {"grChromaRangeExt",                {ApiVersion::GLIDE_2_43, CardModel::VoodooGraphics}}, // Glide 3.x CHROMARANGE
    {"grTexChromaModeExt",              {ApiVersion::GLIDE_2_43, CardModel::Voodoo2}},        // TEXCHROMA requires Voodoo2+
    {"grTexChromaRangeExt",             {ApiVersion::GLIDE_2_43, CardModel::Voodoo2}},        // TEXCHROMA requires Voodoo2+
    {"grAlphaControlsITRGBLighting",    {ApiVersion::GLIDE_2_43, CardModel::Voodoo2}},
    {"grCoordinateSpace",               {ApiVersion::GLIDE_2_43, CardModel::Voodoo2}},

    // 2.61 Features (Banshee / Voodoo3 H3 Legacy Interop)
    {"grSplashCb",                      {ApiVersion::GLIDE_2_61, CardModel::VoodooBanshee}},
    {"grGetGammaTableExt",              {ApiVersion::GLIDE_2_61, CardModel::VoodooBanshee}},
    {"grTextureBufferExt",              {ApiVersion::GLIDE_2_61, CardModel::VoodooBanshee}},
    {"grTextureAuxBufferExt",           {ApiVersion::GLIDE_2_61, CardModel::VoodooBanshee}},
    {"grAuxBufferExt",                  {ApiVersion::GLIDE_2_61, CardModel::VoodooBanshee}},

    // VSA-100 / Napalm Features (Voodoo4 / Voodoo5 — PIXEXT, COMBINE, Spatial AA & T-Buffer)
    {"grSstWinOpenExt",                 {ApiVersion::GLIDE_2_61, CardModel::Voodoo4}},
    {"grTBufferWriteMaskExt",           {ApiVersion::GLIDE_2_61, CardModel::Voodoo4}},
    {"grColorMaskExt",                  {ApiVersion::GLIDE_2_61, CardModel::Voodoo4}},
    {"grStencilFuncExt",                {ApiVersion::GLIDE_2_61, CardModel::Voodoo4}},
    {"grStencilMaskExt",                {ApiVersion::GLIDE_2_61, CardModel::Voodoo4}},
    {"grStencilOpExt",                  {ApiVersion::GLIDE_2_61, CardModel::Voodoo4}},
    {"grBufferClearExt",                {ApiVersion::GLIDE_2_61, CardModel::Voodoo4}},
    {"grLfbConstantStencilExt",         {ApiVersion::GLIDE_2_61, CardModel::Voodoo4}},
    {"grAlphaBlendFunctionExt",         {ApiVersion::GLIDE_2_61, CardModel::Voodoo4}},
    {"grColorCombineExt",               {ApiVersion::GLIDE_2_61, CardModel::Voodoo4}},
    {"grAlphaCombineExt",               {ApiVersion::GLIDE_2_61, CardModel::Voodoo4}},
    {"grTexColorCombineExt",            {ApiVersion::GLIDE_2_61, CardModel::Voodoo4}},
    {"grTexAlphaCombineExt",            {ApiVersion::GLIDE_2_61, CardModel::Voodoo4}},
    {"grConstantColorValueExt",         {ApiVersion::GLIDE_2_61, CardModel::Voodoo4}}
};

ApiVersion GetDefaultGlide2Version(CardModel model) {
    switch (model) {
        case CardModel::VoodooGraphics:
        case CardModel::VoodooRush:
            return ApiVersion::GLIDE_2_48;
        case CardModel::Voodoo2:
            return ApiVersion::GLIDE_2_56;
        case CardModel::VoodooBanshee:
        case CardModel::Voodoo3:
        case CardModel::Voodoo4:
        case CardModel::Voodoo5:
            return ApiVersion::GLIDE_2_61;
    }
    return ApiVersion::GLIDE_2_56;
}

ApiVersion GetDefaultGlide3Version(CardModel model) {
    switch (model) {
        case CardModel::VoodooGraphics:
        case CardModel::VoodooRush:
            return ApiVersion::GLIDE_3_04;
        case CardModel::Voodoo2:
            return ApiVersion::GLIDE_3_03;
        case CardModel::VoodooBanshee:
        case CardModel::Voodoo3:
        case CardModel::Voodoo4:
        case CardModel::Voodoo5:
            return ApiVersion::GLIDE_3_10;
    }
    return ApiVersion::GLIDE_3_03;
}

const char* FormatApiVersionString(ApiVersion ver) {
    switch (ver) {
        case ApiVersion::GLIDE_2_11: return "2.11";
        case ApiVersion::GLIDE_2_30: return "2.30";
        case ApiVersion::GLIDE_2_40: return "2.40";
        case ApiVersion::GLIDE_2_42: return "2.42";
        case ApiVersion::GLIDE_2_43: return "2.43";
        case ApiVersion::GLIDE_2_45: return "2.45";
        case ApiVersion::GLIDE_2_46: return "2.46";
        case ApiVersion::GLIDE_2_48: return "2.48";
        case ApiVersion::GLIDE_2_50: return "2.50";
        case ApiVersion::GLIDE_2_51: return "2.51";
        case ApiVersion::GLIDE_2_53: return "2.53";
        case ApiVersion::GLIDE_2_54: return "2.54";
        case ApiVersion::GLIDE_2_56: return "2.56";
        case ApiVersion::GLIDE_2_60: return "2.60";
        case ApiVersion::GLIDE_2_61: return "2.61";
        case ApiVersion::GLIDE_3_00: return "3.00";
        case ApiVersion::GLIDE_3_01: return "3.01";
        case ApiVersion::GLIDE_3_02: return "3.02";
        case ApiVersion::GLIDE_3_03: return "3.03";
        case ApiVersion::GLIDE_3_04: return "3.04";
        case ApiVersion::GLIDE_3_10: return "3.10";
    }
    return "2.56";
}

bool IsVersionHistoricallyValidForCard(CardModel model, ApiVersion ver) {
    switch (model) {
        case CardModel::VoodooGraphics:
            switch (ver) {
                case ApiVersion::GLIDE_2_11:
                case ApiVersion::GLIDE_2_30:
                case ApiVersion::GLIDE_2_40:
                case ApiVersion::GLIDE_2_43:
                case ApiVersion::GLIDE_2_46:
                case ApiVersion::GLIDE_2_48:
                case ApiVersion::GLIDE_3_00:
                case ApiVersion::GLIDE_3_02:
                case ApiVersion::GLIDE_3_04:
                    return true;
                default:
                    return false;
            }
        case CardModel::VoodooRush:
            switch (ver) {
                case ApiVersion::GLIDE_2_40:
                case ApiVersion::GLIDE_2_42:
                case ApiVersion::GLIDE_2_43:
                case ApiVersion::GLIDE_2_45:
                case ApiVersion::GLIDE_2_46:
                case ApiVersion::GLIDE_2_48:
                case ApiVersion::GLIDE_3_00:
                case ApiVersion::GLIDE_3_02:
                case ApiVersion::GLIDE_3_04:
                    return true;
                default:
                    return false;
            }
        case CardModel::Voodoo2:
            switch (ver) {
                case ApiVersion::GLIDE_2_50:
                case ApiVersion::GLIDE_2_51:
                case ApiVersion::GLIDE_2_53:
                case ApiVersion::GLIDE_2_54:
                case ApiVersion::GLIDE_2_56:
                case ApiVersion::GLIDE_3_01:
                case ApiVersion::GLIDE_3_03:
                    return true;
                default:
                    return false;
            }
        case CardModel::VoodooBanshee:
        case CardModel::Voodoo3:
            switch (ver) {
                case ApiVersion::GLIDE_2_60:
                case ApiVersion::GLIDE_2_61:
                case ApiVersion::GLIDE_3_10:
                    return true;
                default:
                    return false;
            }
        case CardModel::Voodoo4:
        case CardModel::Voodoo5:
            switch (ver) {
                case ApiVersion::GLIDE_2_61:
                case ApiVersion::GLIDE_3_10:
                    return true;
                default:
                    return false;
            }
    }
    return false;
}

const char* DescribeAuthenticVersionsForCard(CardModel model, bool glide3) {
    switch (model) {
        case CardModel::VoodooGraphics:
            return glide3 ? "3.00, 3.02, 3.04"
                          : "2.11, 2.30, 2.40, 2.43, 2.46, 2.48";
        case CardModel::VoodooRush:
            return glide3 ? "3.00, 3.02, 3.04"
                          : "2.40, 2.42, 2.43, 2.45, 2.46, 2.48";
        case CardModel::Voodoo2:
            return glide3 ? "3.01, 3.03"
                          : "2.50, 2.51, 2.53, 2.54, 2.56";
        case CardModel::VoodooBanshee:
        case CardModel::Voodoo3:
            return glide3 ? "3.10" : "2.60, 2.61";
        case CardModel::Voodoo4:
        case CardModel::Voodoo5:
            return glide3 ? "3.10" : "2.61";
    }
    return glide3 ? "3.01, 3.03" : "2.50, 2.51, 2.53, 2.54, 2.56";
}

EmulationRegistry& EmulationRegistry::GetInstance() {
    static EmulationRegistry instance;
    return instance;
}

EmulationRegistry::EmulationRegistry() {
    // Load baseline defaults + optional glide-ng.ini + FX_GLIDE_* + GLIDE_NG_*
    // through the single authoritative config loader.
    IniConfigLoader loader;
    loader.Load(DEFAULT_CONFIG_FILE, m_config);
}

bool EmulationRegistry::IsFunctionAvailable(const std::string& funcName) const {
    auto it = s_gatingRegistry.find(funcName);
    if (it == s_gatingRegistry.end()) {
        // Core baseline Glide 2.1 / 2.40 function: always available
        return true;
    }
    
    const auto& rule = it->second;
    
    // Check API Version
    if (m_config.apiVersion < rule.minVersion) {
        return false;
    }
    
    // Check Card Model
    if (static_cast<int>(m_config.model) < static_cast<int>(rule.minCard)) {
        return false;
    }
    
    return true;
}

bool EmulationRegistry::IsCardFeatureAvailable(const std::string& funcName) const {
    auto it = s_gatingRegistry.find(funcName);
    if (it == s_gatingRegistry.end()) {
        return true;
    }
    return static_cast<int>(m_config.model) >= static_cast<int>(it->second.minCard);
}

} // namespace GlideWrapper
