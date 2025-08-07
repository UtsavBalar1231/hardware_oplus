/*
 * SPDX-FileCopyrightText: 2025 UtsavBalar1231 <utsavbalar1231@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DYNAMIC_EFFECT_LOADER_H
#define DYNAMIC_EFFECT_LOADER_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include "effect.h"

namespace aidl {
namespace android {
namespace hardware {
namespace vibrator {

/**
 * Dynamic effect loader for custom vibration effects.
 * Supports loading effects from JSON configuration files at runtime.
 */
class DynamicEffectLoader {
public:
    static DynamicEffectLoader& getInstance();
    
    /**
     * Initialize the effect loader and scan for custom effects.
     * @param effectsDir Directory containing effect configuration files
     * @return true if initialization successful
     */
    bool initialize(const std::string& effectsDir = "/vendor/etc/vibrator_effects");
    
    /**
     * Get effect stream by effect ID.
     * First checks dynamic effects, then falls back to static effects.
     * @param effect_id Effect identifier
     * @return Pointer to effect_stream or nullptr if not found
     */
    const struct effect_stream* getEffectStream(uint32_t effect_id);
    
    /**
     * Reload effects from configuration files.
     * @return Number of effects loaded
     */
    int reloadEffects();
    
    /**
     * Get list of available effect IDs.
     * @return Vector of effect IDs (both static and dynamic)
     */
    std::vector<uint32_t> getAvailableEffects() const;
    
    /**
     * Check if an effect ID is available.
     * @param effect_id Effect identifier
     * @return true if effect exists
     */
    bool hasEffect(uint32_t effect_id) const;

private:
    DynamicEffectLoader() = default;
    ~DynamicEffectLoader();
    
    // Non-copyable
    DynamicEffectLoader(const DynamicEffectLoader&) = delete;
    DynamicEffectLoader& operator=(const DynamicEffectLoader&) = delete;
    
    /**
     * Load effects from a JSON configuration file.
     * @param filePath Path to JSON effect file
     * @return Number of effects loaded from file
     */
    int loadEffectFile(const std::string& filePath);
    
    /**
     * Parse JSON effect configuration.
     * @param jsonContent JSON string content
     * @return Number of effects parsed
     */
    int parseEffectJson(const std::string& jsonContent);
    
    /**
     * Clear all dynamic effects and free memory.
     */
    void clearDynamicEffects();
    
    /**
     * Validate effect configuration.
     * @param effect Effect stream to validate
     * @return true if valid
     */
    bool validateEffect(const struct effect_stream& effect) const;

    mutable std::mutex mEffectsMutex;
    std::map<uint32_t, std::unique_ptr<struct effect_stream>> mDynamicEffects;
    std::vector<std::unique_ptr<int8_t[]>> mEffectData; // Storage for waveform data
    std::string mEffectsDirectory;
    bool mInitialized = false;
    
    // Effect ID ranges
    static constexpr uint32_t STATIC_EFFECT_ID_MAX = 99;
    static constexpr uint32_t DYNAMIC_EFFECT_ID_MIN = 100;
    static constexpr uint32_t DYNAMIC_EFFECT_ID_MAX = 999;
};

} // namespace vibrator
} // namespace hardware
} // namespace android
} // namespace aidl

#endif // DYNAMIC_EFFECT_LOADER_H
