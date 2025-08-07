/*
 * SPDX-FileCopyrightText: 2025 UtsavBalar1231 <utsavbalar1231@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 */

#include "DynamicEffectLoader.h"
#include "VibrationEffectConfig.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include <json/json.h>
#include <dirent.h>
#include <algorithm>

using android::base::ReadFileToString;

namespace aidl
{
namespace android
{
namespace hardware
{
namespace vibrator
{

DynamicEffectLoader &DynamicEffectLoader::getInstance()
{
	static DynamicEffectLoader instance;
	return instance;
}

DynamicEffectLoader::~DynamicEffectLoader()
{
	clearDynamicEffects();
}

bool DynamicEffectLoader::initialize(const std::string &effectsDir)
{
	std::lock_guard<std::mutex> lock(mEffectsMutex);

	mEffectsDirectory = effectsDir;
	clearDynamicEffects();

	// Check if effects directory exists
	DIR *dir = opendir(effectsDir.c_str());
	if (!dir) {
		LOG(WARNING) << "Effects directory not found: " << effectsDir;
		mInitialized =
			true; // Still initialized, just no custom effects
		return true;
	}
	closedir(dir);

	int effectCount = reloadEffects();
	LOG(INFO) << "Dynamic effect loader initialized with " << effectCount
		  << " custom effects";

	mInitialized = true;
	return true;
}

const struct effect_stream *
DynamicEffectLoader::getEffectStream(uint32_t effect_id)
{
	std::lock_guard<std::mutex> lock(mEffectsMutex);

	// First check dynamic effects
	auto it = mDynamicEffects.find(effect_id);
	if (it != mDynamicEffects.end()) {
		return it->second.get();
	}

	// Fall back to static effects
	return get_effect_stream(effect_id);
}

int DynamicEffectLoader::reloadEffects()
{
	if (!mInitialized) {
		LOG(ERROR) << "Effect loader not initialized";
		return 0;
	}

	clearDynamicEffects();

	DIR *dir = opendir(mEffectsDirectory.c_str());
	if (!dir) {
		LOG(WARNING) << "Cannot open effects directory: "
			     << mEffectsDirectory;
		return 0;
	}

	int totalEffects = 0;
	struct dirent *entry;

	while ((entry = readdir(dir)) != nullptr) {
		std::string filename = entry->d_name;

		// Only process .json files
		if (filename.length() < 5 ||
		    filename.substr(filename.length() - 5) != ".json") {
			continue;
		}

		std::string filePath = mEffectsDirectory + "/" + filename;
		int effectCount = loadEffectFile(filePath);

		if (effectCount > 0) {
			LOG(INFO) << "Loaded " << effectCount
				  << " effects from " << filename;
			totalEffects += effectCount;
		} else {
			LOG(WARNING)
				<< "Failed to load effects from " << filename;
		}
	}

	closedir(dir);
	return totalEffects;
}

int DynamicEffectLoader::loadEffectFile(const std::string &filePath)
{
	std::string jsonContent;
	if (!ReadFileToString(filePath, &jsonContent)) {
		LOG(ERROR) << "Failed to read effect file: " << filePath;
		return 0;
	}

	return parseEffectJson(jsonContent);
}

int DynamicEffectLoader::parseEffectJson(const std::string &jsonContent)
{
	Json::Value root;
	Json::CharReaderBuilder builder;
	std::string errors;

	std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
	if (!reader->parse(jsonContent.c_str(),
			   jsonContent.c_str() + jsonContent.length(), &root,
			   &errors)) {
		LOG(ERROR) << "Failed to parse JSON: " << errors;
		return 0;
	}

	if (!root.isObject() || !root.isMember("effects") ||
	    !root["effects"].isArray()) {
		LOG(ERROR) << "Invalid JSON format: missing 'effects' array";
		return 0;
	}

	int loadedCount = 0;
	const Json::Value &effects = root["effects"];

	for (const Json::Value &effectJson : effects) {
		if (!effectJson.isObject()) {
			LOG(WARNING) << "Skipping invalid effect entry";
			continue;
		}

		// Parse effect properties
		if (!effectJson.isMember("id") ||
		    !effectJson.isMember("data") ||
		    !effectJson.isMember("play_rate_hz")) {
			LOG(WARNING)
				<< "Skipping effect missing required fields";
			continue;
		}

		uint32_t effect_id = effectJson["id"].asUInt();
		uint32_t play_rate_hz = effectJson["play_rate_hz"].asUInt();

		// Validate effect ID range
		if (effect_id < DYNAMIC_EFFECT_ID_MIN ||
		    effect_id > DYNAMIC_EFFECT_ID_MAX) {
			LOG(WARNING) << "Effect ID " << effect_id
				     << " outside valid range ["
				     << DYNAMIC_EFFECT_ID_MIN << "-"
				     << DYNAMIC_EFFECT_ID_MAX << "]";
			continue;
		}

		// Check for duplicate IDs
		if (mDynamicEffects.find(effect_id) != mDynamicEffects.end()) {
			LOG(WARNING) << "Duplicate effect ID " << effect_id
				     << ", skipping";
			continue;
		}

		// Parse waveform data
		const Json::Value &dataArray = effectJson["data"];
		if (!dataArray.isArray() || dataArray.size() == 0) {
			LOG(WARNING)
				<< "Invalid or empty data array for effect "
				<< effect_id;
			continue;
		}

		// Allocate waveform data
		size_t dataLength = dataArray.size();
		auto waveformData = std::make_unique<int8_t[]>(dataLength);

		bool validData = true;
		for (size_t i = 0; i < dataLength; i++) {
			if (!dataArray[static_cast<int>(i)].isInt()) {
				LOG(WARNING)
					<< "Invalid data value at index " << i
					<< " for effect " << effect_id;
				validData = false;
				break;
			}

			int value = dataArray[static_cast<int>(i)].asInt();
			if (value < -128 || value > 127) {
				LOG(WARNING) << "Data value " << value
					     << " out of range [-128, 127] "
					     << "at index " << i
					     << " for effect " << effect_id;
				validData = false;
				break;
			}

			waveformData[i] = static_cast<int8_t>(value);
		}

		if (!validData) {
			continue;
		}

		// Create effect stream
		auto effect = std::make_unique<struct effect_stream>();
		effect->effect_id = effect_id;
		effect->length = static_cast<uint32_t>(dataLength);
		effect->play_rate_hz = play_rate_hz;
		effect->data = waveformData.get();

		// Validate effect
		if (!validateEffect(*effect)) {
			LOG(WARNING) << "Effect " << effect_id
				     << " failed validation";
			continue;
		}

		// Store effect and data
		mEffectData.push_back(std::move(waveformData));
		mDynamicEffects[effect_id] = std::move(effect);
		loadedCount++;

		LOG(INFO)
			<< "Loaded effect " << effect_id << " with "
			<< dataLength << " samples at " << play_rate_hz << "Hz";
	}

	return loadedCount;
}

std::vector<uint32_t> DynamicEffectLoader::getAvailableEffects() const
{
	std::lock_guard<std::mutex> lock(mEffectsMutex);

	std::vector<uint32_t> effectIds;

	// Add static effects (from original implementation)
	for (int i = 0; i < ARRAY_SIZE(effects); i++) {
		effectIds.push_back(effects[i].effect_id);
	}

	// Add dynamic effects
	for (const auto &pair : mDynamicEffects) {
		effectIds.push_back(pair.first);
	}

	std::sort(effectIds.begin(), effectIds.end());
	return effectIds;
}

bool DynamicEffectLoader::hasEffect(uint32_t effect_id) const
{
	std::lock_guard<std::mutex> lock(mEffectsMutex);

	// Check dynamic effects
	if (mDynamicEffects.find(effect_id) != mDynamicEffects.end()) {
		return true;
	}

	// Check static effects
	return get_effect_stream(effect_id) != nullptr;
}

void DynamicEffectLoader::clearDynamicEffects()
{
	mDynamicEffects.clear();
	mEffectData.clear();
}

bool DynamicEffectLoader::validateEffect(
	const struct effect_stream &effect) const
{
	// Validate play rate
	if (effect.play_rate_hz < 1000 || effect.play_rate_hz > 48000) {
		LOG(WARNING) << "Invalid play rate: " << effect.play_rate_hz
			     << "Hz (valid range: 1000-48000Hz)";
		return false;
	}

	// Validate data length
	if (effect.length == 0 || effect.length > 100000) {
		LOG(WARNING) << "Invalid data length: " << effect.length
			     << " (valid range: 1-100000 samples)";
		return false;
	}

	// Validate data pointer
	if (effect.data == nullptr) {
		LOG(WARNING)
			<< "Null data pointer for effect " << effect.effect_id;
		return false;
	}

	return true;
}

} // namespace vibrator
} // namespace hardware
} // namespace android
} // namespace aidl
