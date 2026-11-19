#pragma once

#include "IConfigLoader.h"

namespace GlideWrapper {

class IniConfigLoader : public IConfigLoader {
public:
    bool Load(const std::string& configFilePath, WrapperConfig& outConfig) override;
    bool LoadWithDefaults(const std::string& configFilePath,
                          WrapperConfig& outConfig,
                          CardModel defaultModel,
                          ApiVersion defaultGlide2Api,
                          bool hasCustomDefaultGlide2Api = true);
};

} // namespace GlideWrapper
