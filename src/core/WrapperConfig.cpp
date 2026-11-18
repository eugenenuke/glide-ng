#include "WrapperConfig.h"
#include "IniConfigLoader.h"
#include <string>
#include <unordered_map>

namespace GlideWrapper {

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

    // 2.43 Features (Voodoo2 Gold Standard)
    {"grGetProcAddress",                {ApiVersion::GLIDE_2_43, CardModel::VoodooGraphics}}, // Dynamic loading is an API feature
    {"grChromakeyRangeExt",             {ApiVersion::GLIDE_2_43, CardModel::Voodoo2}},        // Range match requires Voodoo2 in Glide 2.x
    {"grChromaRangeModeExt",            {ApiVersion::GLIDE_2_43, CardModel::VoodooGraphics}}, // Glide 3.x CHROMARANGE
    {"grChromaRangeExt",                {ApiVersion::GLIDE_2_43, CardModel::VoodooGraphics}}, // Glide 3.x CHROMARANGE
    {"grTexChromaModeExt",              {ApiVersion::GLIDE_2_43, CardModel::Voodoo2}},        // TEXCHROMA requires Voodoo2+
    {"grTexChromaRangeExt",             {ApiVersion::GLIDE_2_43, CardModel::Voodoo2}},        // TEXCHROMA requires Voodoo2+
    {"grAlphaControlsITRGBLighting",    {ApiVersion::GLIDE_2_43, CardModel::Voodoo2}},
    {"grCoordinateSpace",               {ApiVersion::GLIDE_2_43, CardModel::Voodoo2}},

    // 2.61 Features (Banshee / Voodoo3 H3 Legacy Interop)
    {"grSplashCb",                      {ApiVersion::GLIDE_2_61, CardModel::VoodooBanshee}},

    // VSA-100 / Napalm Features (Voodoo4 / Voodoo5 — PIXEXT, Spatial AA & T-Buffer)
    {"grSstWinOpenExt",                 {ApiVersion::GLIDE_2_61, CardModel::Voodoo4}},
    {"grTBufferWriteMaskExt",           {ApiVersion::GLIDE_2_61, CardModel::Voodoo4}},
    {"grColorMaskExt",                  {ApiVersion::GLIDE_2_61, CardModel::Voodoo4}},
    {"grStencilFuncExt",                {ApiVersion::GLIDE_2_61, CardModel::Voodoo4}},
    {"grStencilMaskExt",                {ApiVersion::GLIDE_2_61, CardModel::Voodoo4}},
    {"grStencilOpExt",                  {ApiVersion::GLIDE_2_61, CardModel::Voodoo4}},
    {"grBufferClearExt",                {ApiVersion::GLIDE_2_61, CardModel::Voodoo4}},
    {"grLfbConstantStencilExt",         {ApiVersion::GLIDE_2_61, CardModel::Voodoo4}},
    {"grAlphaBlendFunctionExt",         {ApiVersion::GLIDE_2_61, CardModel::Voodoo4}}
};

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
