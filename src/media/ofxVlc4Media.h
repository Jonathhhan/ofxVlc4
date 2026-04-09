#pragma once

#include "core/ofxVlc4.h"

class MediaLibrary;

class ofxVlc4::MediaComponent {
public:
	explicit MediaComponent(ofxVlc4 & owner);

	void applyCurrentPlayerSettings();
	void applySafeLoadedMediaPlayerSettings();
	void prepareStartupMediaResources();
	bool reapplyCurrentMediaForFilterChainChange(const std::string & label);
	ofxVlc4::MediaPlayerRole getMediaPlayerRole() const;
	void setMediaPlayerRole(ofxVlc4::MediaPlayerRole role);
	bool isWatchTimeEnabled() const;
	void setWatchTimeEnabled(bool enabled);
	int64_t getWatchTimeMinPeriodUs() const;
	void setWatchTimeMinPeriodUs(int64_t minPeriodUs);
	void clearWatchTimeState();
	ofxVlc4::WatchTimeInfo getWatchTimeInfo() const;
	void applyMediaPlayerRole();
	void applyNativeRecording();
	void applyWatchTimeObserver();
	bool isNativeRecordingEnabled() const;
	void setNativeRecordingEnabled(bool enabled);
	std::string getNativeRecordDirectory() const;
	void setNativeRecordDirectory(const std::string & directory);
	void prepareForMediaDetach();
	void updateSnapshotStateOnRequest(const std::string & requestedPath);
	void updateSnapshotStateFromEvent(const std::string & savedPath);
	void clearPendingSnapshotState();
	void updateSnapshotFailureState(const std::string & failureReason);
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
	ofxVlc4::MediaParseInfo getCurrentMediaParseInfo() const;
	ofxVlc4::MediaParseStatus getCurrentMediaParseStatus() const;
	ofxVlc4::MediaParseOptions getMediaParseOptions() const;
	void setMediaParseOptions(const ofxVlc4::MediaParseOptions & options);
	bool requestCurrentMediaParse();
	void stopCurrentMediaParse();
	void handleMediaParsedChanged(libvlc_media_parsed_status_t status);
	void handleAttachedThumbnailsFound(libvlc_picture_list_t * thumbnails);
	void handleThumbnailGenerated(libvlc_picture_t * thumbnail);
	void updateNativeRecordingStateFromEvent(bool active, const std::string & recordedFilePath);
	void updateNativeRecordingFailureState(const std::string & failureReason);
	void dismissAllDialogs();
	bool postDialogLogin(std::uintptr_t token, const std::string & username, const std::string & password, bool store);
	bool postDialogAction(std::uintptr_t token, int action);
	bool dismissDialog(std::uintptr_t token);
	void upsertDialog(const ofxVlc4::DialogInfo & dialog);
	void removeDialog(std::uintptr_t token);
	const std::string & getLastStatusMessage() const;
	const std::string & getLastErrorMessage() const;
	void clearLastMessages();
	std::vector<ofxVlc4::DialogInfo> getActiveDialogs() const;
	ofxVlc4::DialogErrorInfo getLastDialogError() const;
	void clearLastDialogError();
	void clearMetadataCache();
	void cacheArtworkPathForMediaPath(const std::string & mediaPath, const std::string & artworkPath);
	void clearGeneratedThumbnailInfo();
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
	bool isLibVlcLoggingEnabled() const;
	void setLibVlcLoggingEnabled(bool enabled);
	bool isLibVlcLogFileEnabled() const;
	void setLibVlcLogFileEnabled(bool enabled);
	std::string getLibVlcLogFilePath() const;
	void setLibVlcLogFilePath(const std::string & path);
	std::vector<ofxVlc4::LibVlcLogEntry> getLibVlcLogEntries() const;
	void clearLibVlcLogEntries();
	void applyLibVlcLogging();
	void closeLibVlcLogFile();
	void appendLibVlcLog(const ofxVlc4::LibVlcLogEntry & entry);
	std::vector<ofxVlc4::MediaDiscovererInfo> getMediaDiscoverers(ofxVlc4::MediaDiscovererCategory category) const;
	std::string getSelectedMediaDiscovererName() const;
	ofxVlc4::MediaDiscoveryStateInfo getMediaDiscoveryState() const;
	std::vector<ofxVlc4::DiscoveredMediaItemInfo> getDiscoveredMediaItems() const;
	void stopMediaDiscoveryInternal();
	void refreshDiscoveredMediaItems();
	bool startMediaDiscovery(const std::string & discovererName);
	void stopMediaDiscovery();
	bool isMediaDiscoveryActive() const;
	bool addDiscoveredMediaItemToPlaylist(int index);
	bool playDiscoveredMediaItem(int index);
	int addAllDiscoveredMediaItemsToPlaylist();
	void mediaDiscovererMediaListEvent(const libvlc_event_t * event);
	void handleMediaEvent(const libvlc_event_t * event);
	std::vector<ofxVlc4::RendererDiscovererInfo> getRendererDiscoverers() const;
	std::string getSelectedRendererDiscovererName() const;
	void clearRendererItems();
	void stopRendererDiscoveryInternal();
	void resetRendererStateInfo();
	void refreshRendererStateInfo();
	ofxVlc4::RendererStateInfo getRendererStateInfo() const;
	void handleRendererDiscovererEvent(const libvlc_event_t * event);
	bool handleRendererItemAdded(libvlc_renderer_item_t * item, std::string & rendererLabel, bool & shouldReconnectSelectedRenderer);
	bool handleRendererItemDeleted(libvlc_renderer_item_t * item, bool & removedSelectedRenderer);
	bool applySelectedRenderer();
	bool startRendererDiscovery(const std::string & discovererName);
	void stopRendererDiscovery();
	bool isRendererDiscoveryActive() const;
	std::vector<ofxVlc4::RendererInfo> getDiscoveredRenderers() const;
	std::string getSelectedRendererId() const;
	bool selectRenderer(const std::string & rendererId);
	bool clearRenderer();
	ofxVlc4::MediaStats getMediaStats() const;
	std::vector<ofxVlc4::PlaylistItemInfo> getPlaylistItems() const;
	std::vector<std::string> getPlaylist() const;
	ofxVlc4::PlaylistStateInfo getPlaylistStateInfo() const;
	ofxVlc4::PlaylistItemInfo getCurrentPlaylistItemInfo() const;
	std::string getPathAtIndex(int index) const;
	std::string getFileNameAtIndex(int index) const;
	std::string getCurrentPath() const;
	int getCurrentIndex() const;
	std::string getCurrentFileName() const;
	std::vector<std::pair<std::string, std::string>> getMetadataAtIndex(int index) const;
	std::vector<std::pair<std::string, std::string>> getCurrentMetadata() const;
	bool hasPlaylist() const;
	std::vector<ofxVlc4::ProgramInfo> getPrograms() const;
	int getSelectedProgramId() const;
	bool selectProgramId(int programId);
	std::vector<ofxVlc4::TitleInfo> getTitles() const;
	int getCurrentTitleIndex() const;
	bool selectTitleIndex(int index);
	std::vector<ofxVlc4::ChapterInfo> getChapters(int titleIndex) const;
	int getCurrentChapterIndex() const;
	bool selectChapterIndex(int index);
	void previousChapter();
	void nextChapter();
	void nextFrame();
	void navigate(ofxVlc4::NavigationMode mode);
	ofxVlc4::AbLoopInfo getAbLoop() const;
	bool setAbLoopA();
	bool setAbLoopB();
	void clearAbLoop();
	std::vector<ofxVlc4::MediaTrackInfo> getTrackInfos(libvlc_track_type_t type) const;
	std::vector<ofxVlc4::MediaTrackInfo> getVideoTracks() const;
	std::vector<ofxVlc4::MediaTrackInfo> getAudioTracks() const;
	std::vector<ofxVlc4::MediaTrackInfo> getSubtitleTracks() const;
	bool selectTrackById(libvlc_track_type_t type, const std::string & trackId);
	bool selectAudioTrackById(const std::string & trackId);
	bool selectSubtitleTrackById(const std::string & trackId);
	std::vector<ofxVlc4::MediaSlaveInfo> getMediaSlaves() const;
	bool addMediaSlave(ofxVlc4::MediaSlaveType type, const std::string & uri, unsigned priority);
	void clearMediaSlaves();
	void resetSubtitleStateInfo();
	void resetNavigationStateInfo();
	void refreshPrimaryTrackStateInfo();
	void refreshSubtitleStateInfo();
	ofxVlc4::SubtitleStateInfo getSubtitleStateInfo() const;
	void refreshNavigationStateInfo();
	ofxVlc4::NavigationStateInfo getNavigationStateInfo() const;
	ofxVlc4::PlaybackStateInfo getPlaybackStateInfo() const;
	ofxVlc4::MediaReadinessInfo getMediaReadinessInfo() const;
	void prepareStartupPlaybackState();
	void addToPlaylistInternal(const std::string & path, bool preloadMetadata);
	void addToPlaylist(const std::string & path);
	int addPathToPlaylist(const std::string & path);
	int addPathToPlaylist(const std::string & path, std::initializer_list<std::string> extensions);
	bool loadMediaSource(
		const std::string & source,
		bool isLocation,
		const std::vector<std::string> & options,
		bool parseAsNetwork);
	bool loadMediaAtIndex(int index);
	void clearCurrentMedia(bool clearVideoResources = true);
	void clearPlaylist();
	void removeFromPlaylist(int index);
	void movePlaylistItem(int fromIndex, int toIndex);
	void movePlaylistItems(const std::vector<int> & fromIndices, int toIndex);

private:
	MediaLibrary & mediaLibrary() const;
	ofxVlc4::AudioComponent & audio() const;
	ofxVlc4::VideoComponent & video() const;
	PlaybackController & playback() const;
	ofxVlc4::MediaDiscoveryStateInfo buildMediaDiscoveryStateInfoLocked() const;
	void clearMediaDiscoveryStateLocked();
	void setMediaDiscoveryDescriptorLocked(
		const std::string & name,
		const std::string & longName,
		ofxVlc4::MediaDiscovererCategory category,
		bool endReached);
	void setMediaDiscoveryEndReachedLocked(bool endReached);
	void setDiscoveredMediaItemsLocked(std::vector<ofxVlc4::DiscoveredMediaItemInfo> items);
	void setLibVlcLoggingEnabledValue(bool enabled);
	void setLibVlcLogFileEnabledValue(bool enabled);
	void setLibVlcLogFilePathValue(const std::string & path);
	void setNativeRecordingEnabledValue(bool enabled);
	void setNativeRecordDirectoryValue(const std::string & directory);
	const ofxVlc4::RendererItemEntry * findRendererEntryByIdLocked(const std::string & rendererId) const;
	ofxVlc4::RendererStateInfo buildRendererStateInfoLocked() const;
	ofxVlc4::RendererInfo buildRendererInfoLocked(const ofxVlc4::RendererItemEntry & entry, bool selected) const;
	std::vector<ofxVlc4::RendererInfo> buildDiscoveredRendererInfosLocked() const;
	bool canApplyRendererImmediately() const;
	void clearSelectedRendererLocked();
	void setSelectedRendererIdLocked(const std::string & rendererId);
	void setRendererDiscovererNameLocked(const std::string & discovererName);
	ofxVlc4::SubtitleStateInfo buildSubtitleStateInfo(bool includeTrackDetails) const;
	ofxVlc4::NavigationStateInfo buildNavigationStateInfo() const;
	ofxVlc4::SnapshotStateInfo buildSnapshotStateInfoLocked() const;
	ofxVlc4::NativeRecordingStateInfo buildNativeRecordingStateInfoLocked() const;
	ofxVlc4::WatchTimeInfo buildWatchTimeInfo() const;
	ofxVlc4::SubtitleStateInfo getCachedSubtitleStateInfo() const;
	ofxVlc4::NavigationStateInfo getCachedNavigationStateInfo() const;
	void setNativeRecordingStatusLocked(const std::string & eventMessage, const std::string & failureReason);
	void clearNativeRecordingOutputLocked();
	void setNativeRecordingOutputLocked(const std::string & path, uint64_t bytes, const std::string & timestamp);
	std::vector<std::uintptr_t> getActiveDialogTokensLocked() const;
	void upsertActiveDialogLocked(const ofxVlc4::DialogInfo & dialog);
	void removeActiveDialogLocked(std::uintptr_t token);
	void clearLastDialogErrorLocked();

	ofxVlc4 & owner;
};
