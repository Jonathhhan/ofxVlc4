#pragma once

#include "ofxVlc4.h"

#include <initializer_list>
#include <set>
#include <string>
#include <utility>
#include <vector>

class MediaLibrary {
public:
	struct RemovePlaylistItemResult {
		bool removed = false;
		bool wasCurrent = false;
		bool playlistEmpty = false;
		int replacementIndex = -1;
	};

	explicit MediaLibrary(ofxVlc4 & owner);

	void addToPlaylistInternal(const std::string & path, bool preloadMetadata);
	void addToPlaylist(const std::string & path);
	int addPathToPlaylist(const std::string & path);
	int addPathToPlaylist(const std::string & path, std::initializer_list<std::string> extensions);
	void clearPlaylistState();
	void resetCurrentMediaParseState();
	void clearGeneratedThumbnailInfo();
	void clearMetadataCache();
	void cacheArtworkPathForMediaPath(const std::string & mediaPath, const std::string & artworkPath);
	libvlc_media_t * retainCurrentOrLoadedMedia() const;
	std::vector<std::pair<std::string, std::string>> buildMetadataForMedia(libvlc_media_t * sourceMedia) const;
	std::vector<ofxVlc4::PlaylistItemInfo> getPlaylistItems() const;
	ofxVlc4::PlaylistStateInfo getPlaylistStateInfo() const;
	ofxVlc4::PlaylistItemInfo getCurrentPlaylistItemInfo() const;
	std::string getPathAtIndex(int index) const;
	std::string getFileNameAtIndex(int index) const;
	std::string getCurrentPath() const;
	std::string getCurrentFileName() const;
	int getCurrentIndex() const;
	size_t getPlaylistSize() const;
	bool hasPlaylist() const;
	RemovePlaylistItemResult removePlaylistItem(int index);
	void movePlaylistItem(int fromIndex, int toIndex);
	void movePlaylistItems(const std::vector<int> & fromIndices, int toIndex);
	std::vector<std::pair<std::string, std::string>> getMetadataAtIndex(int index) const;
	std::vector<std::pair<std::string, std::string>> getCurrentMetadata() const;
	ofxVlc4::MediaParseInfo getCurrentMediaParseInfo() const;
	ofxVlc4::MediaParseStatus getCurrentMediaParseStatus() const;
	ofxVlc4::MediaParseOptions getMediaParseOptions() const;
	void setMediaParseOptions(const ofxVlc4::MediaParseOptions & options);
	bool requestCurrentMediaParse();
	void stopCurrentMediaParse();
	ofxVlc4::MediaStats getMediaStats() const;
	void handleMediaParsedChanged(libvlc_media_parsed_status_t status);
	ofxVlc4::ThumbnailInfo getLastGeneratedThumbnail() const;
	bool requestThumbnailByTime(
		int timeMs,
		unsigned width,
		unsigned height,
		bool crop,
		ofxVlc4::ThumbnailImageType type,
		ofxVlc4::ThumbnailSeekSpeed speed,
		int timeoutMs);
	bool requestThumbnailByPosition(
		float position,
		unsigned width,
		unsigned height,
		bool crop,
		ofxVlc4::ThumbnailImageType type,
		ofxVlc4::ThumbnailSeekSpeed speed,
		int timeoutMs);
	void cancelThumbnailRequest();
	void handleAttachedThumbnailsFound(libvlc_picture_list_t * thumbnails);
	void handleThumbnailGenerated(libvlc_picture_t * picture);
	void updateSnapshotStateOnRequest(const std::string & requestedPath);
	void updateSnapshotStateFromEvent(const std::string & savedPath);
	void clearPendingSnapshotState();
	void updateSnapshotFailureState(const std::string & failureReason);
	std::string getCurrentMediaMeta(ofxVlc4::MediaMetaField field) const;
	bool setCurrentMediaMeta(ofxVlc4::MediaMetaField field, const std::string & value);
	bool saveCurrentMediaMeta();
	std::vector<std::string> getCurrentMediaMetaExtraNames() const;
	std::string getCurrentMediaMetaExtra(const std::string & name) const;
	bool setCurrentMediaMetaExtra(const std::string & name, const std::string & value);
	bool removeCurrentMediaMetaExtra(const std::string & name);
	std::vector<ofxVlc4::BookmarkInfo> getBookmarksForPath(const std::string & path) const;
	std::vector<ofxVlc4::BookmarkInfo> getCurrentBookmarks() const;
	bool addBookmarkAtTime(int timeMs, const std::string & label);
	bool addCurrentBookmark(const std::string & label);
	bool seekToBookmark(const std::string & bookmarkId);
	bool removeBookmark(const std::string & bookmarkId);
	void clearBookmarksForPath(const std::string & path);
	void clearCurrentBookmarks();

private:
	bool isPlaylistIndexLocked(int index) const;
	std::string getPathAtIndexLocked(int index) const;
	std::string getCurrentPathLocked() const;
	int getCurrentIndexLocked() const;
	size_t getPlaylistSizeLocked() const;
	bool hasPlaylistLocked() const;
	void setCurrentIndexLocked(int index);
	void clearCurrentIndexLocked();
	void appendPlaylistPathLocked(const std::string & path);
	void insertPlaylistPathLocked(int index, const std::string & path);
	void insertPlaylistItemsLocked(int index, std::vector<std::string> items);
	void clearPlaylistLocked();
	void erasePlaylistIndexLocked(int index);
	void replacePlaylistLocked(std::vector<std::string> items);

	ofxVlc4 & owner;
};
