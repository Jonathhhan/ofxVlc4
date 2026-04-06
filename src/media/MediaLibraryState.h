#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct MediaLibraryState {
	mutable std::mutex playlistMutex;
	mutable std::mutex metadataCacheMutex;
	std::vector<std::string> playlist;
	std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>> metadataCache;
	int currentIndex = -1;
};
