#include "MediaLibrary.h"

#include "ofxVlc4.h"
#include "ofxVlc4Impl.h"
#include "support/ofxVlc4PlaylistHelpers.h"
#include "support/ofxVlc4Utils.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>

using ofxVlc4Utils::isUri;
using ofxVlc4Utils::fileNameFromUri;
using ofxVlc4Utils::trimWhitespace;

namespace {

std::string readMediaMeta(libvlc_media_t * media, libvlc_meta_t metaType) {
	if (!media) {
		return "";
	}

	char * rawValue = libvlc_media_get_meta(media, metaType);
	if (!rawValue) {
		return "";
	}

	std::string value = trimWhitespace(rawValue);
	libvlc_free(rawValue);
	return value;
}

void appendMetadataValue(
	std::vector<std::pair<std::string, std::string>> & metadata,
	const std::string & label,
	const std::string & value) {
	const std::string trimmedValue = trimWhitespace(value);
	if (!trimmedValue.empty()) {
		metadata.emplace_back(label, trimmedValue);
	}
}

std::string codecFourccToString(uint32_t codec) {
	std::string out(4, ' ');
	out[0] = static_cast<char>(codec & 0xFF);
	out[1] = static_cast<char>((codec >> 8) & 0xFF);
	out[2] = static_cast<char>((codec >> 16) & 0xFF);
	out[3] = static_cast<char>((codec >> 24) & 0xFF);

	for (char & ch : out) {
		const unsigned char uchar = static_cast<unsigned char>(ch);
		if (!std::isprint(uchar) || std::isspace(uchar)) {
			ch = '.';
		}
	}

	return out;
}

std::string describeCodec(libvlc_track_type_t trackType, uint32_t codec) {
	if (codec == 0) {
		return "";
	}

	const char * description = libvlc_media_get_codec_description(trackType, codec);
	const std::string fourcc = codecFourccToString(codec);
	if (description && *description) {
		return std::string(description) + " (" + fourcc + ")";
	}

	return fourcc;
}

std::string formatBitrate(unsigned int bitsPerSecond) {
	if (bitsPerSecond == 0) {
		return "";
	}

	std::ostringstream stream;
	if (bitsPerSecond >= 1000000) {
		stream << std::fixed << std::setprecision(1)
			   << (static_cast<double>(bitsPerSecond) / 1000000.0) << " Mbps";
	} else {
		stream << std::fixed << std::setprecision(0)
			   << (static_cast<double>(bitsPerSecond) / 1000.0) << " kbps";
	}
	return stream.str();
}

std::string formatFrameRate(unsigned numerator, unsigned denominator) {
	if (numerator == 0 || denominator == 0) {
		return "";
	}

	std::ostringstream stream;
	stream << std::fixed << std::setprecision(2)
		   << (static_cast<double>(numerator) / static_cast<double>(denominator)) << " fps";
	return stream.str();
}

void appendTrackMetadata(
	std::vector<std::pair<std::string, std::string>> & metadata,
	const std::string & prefix,
	libvlc_track_type_t trackType,
	const libvlc_media_track_t * track) {
	if (!track || track->i_type != trackType) {
		return;
	}

	appendMetadataValue(metadata, prefix + " Codec", describeCodec(trackType, track->i_codec));
	appendMetadataValue(metadata, prefix + " Bitrate", formatBitrate(track->i_bitrate));
	appendMetadataValue(metadata, prefix + " Language", track->psz_language ? track->psz_language : "");
	appendMetadataValue(metadata, prefix + " Track", track->psz_name ? track->psz_name : "");

	if (trackType == libvlc_track_video && track->video) {
		const auto * video = track->video;
		if (video->i_width > 0 && video->i_height > 0) {
			appendMetadataValue(
				metadata,
				"Video Resolution",
				ofToString(video->i_width) + " x " + ofToString(video->i_height));
		}
		appendMetadataValue(metadata, "Frame Rate", formatFrameRate(video->i_frame_rate_num, video->i_frame_rate_den));
		if (video->i_sar_num > 0 && video->i_sar_den > 0 &&
			(video->i_sar_num != 1 || video->i_sar_den != 1)) {
			appendMetadataValue(
				metadata,
				"Pixel Aspect",
				ofToString(video->i_sar_num) + ":" + ofToString(video->i_sar_den));
		}
	} else if (trackType == libvlc_track_audio && track->audio) {
		const auto * audio = track->audio;
		if (audio->i_channels > 0) {
			appendMetadataValue(metadata, "Audio Channels", ofToString(audio->i_channels));
		}
		if (audio->i_rate > 0) {
			appendMetadataValue(metadata, "Audio Rate", ofToString(audio->i_rate) + " Hz");
		}
	} else if (trackType == libvlc_track_text && track->subtitle) {
		appendMetadataValue(
			metadata,
			"Subtitle Encoding",
			track->subtitle->psz_encoding ? track->subtitle->psz_encoding : "");
	}
}

void appendTrackMetadataFromMediaTracklist(
	std::vector<std::pair<std::string, std::string>> & metadata,
	libvlc_media_t * media,
	libvlc_track_type_t trackType,
	const std::string & prefix) {
	if (!media) {
		return;
	}

	libvlc_media_tracklist_t * tracklist = libvlc_media_get_tracklist(media, trackType);
	if (!tracklist) {
		return;
	}

	const size_t trackCount = libvlc_media_tracklist_count(tracklist);
	for (size_t i = 0; i < trackCount; ++i) {
		std::string indexedPrefix = prefix;
		if (trackCount > 1) {
			indexedPrefix += " " + ofToString(i + 1);
		}
		appendTrackMetadata(metadata, indexedPrefix, trackType, libvlc_media_tracklist_at(tracklist, i));
	}

	libvlc_media_tracklist_delete(tracklist);
}

std::set<std::string> normalizeExtensions(std::initializer_list<std::string> extensions) {
	std::set<std::string> out;
	for (const auto & extension : extensions) {
		std::string value = ofToLower(trimWhitespace(extension));
		if (value.empty()) {
			continue;
		}
		if (!value.empty() && value[0] != '.') {
			value = "." + value;
		}
		out.insert(value);
	}
	return out;
}

std::string bookmarkStableId(const std::string & path, int timeMs) {
	return ofToString(std::hash<std::string> {}(path + "|" + ofToString(timeMs))) + "_" + ofToString(timeMs);
}

std::string defaultBookmarkLabel(int timeMs) {
	const int totalSeconds = std::max(0, timeMs / 1000);
	const int hours = totalSeconds / 3600;
	const int minutes = (totalSeconds / 60) % 60;
	const int seconds = totalSeconds % 60;
	std::ostringstream stream;
	if (hours > 0) {
		stream << hours << ":" << std::setw(2) << std::setfill('0') << minutes
			   << ":" << std::setw(2) << std::setfill('0') << seconds;
		return stream.str();
	}
	stream << minutes << ":" << std::setw(2) << std::setfill('0') << seconds;
	return stream.str();
}

void waitForMediaParse(libvlc_media_t * media, int timeoutMs) {
	if (!media || timeoutMs <= 0) {
		return;
	}

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	while (std::chrono::steady_clock::now() < deadline) {
		const libvlc_media_parsed_status_t status = libvlc_media_get_parsed_status(media);
		if (status == libvlc_media_parsed_status_done ||
			status == libvlc_media_parsed_status_failed ||
			status == libvlc_media_parsed_status_timeout ||
			status == libvlc_media_parsed_status_skipped ||
			status == libvlc_media_parsed_status_cancelled) {
			return;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(25));
	}
}

bool hasDetailedTrackMetadata(const std::vector<std::pair<std::string, std::string>> & metadata) {
	for (const auto & [label, value] : metadata) {
		if (value.empty()) {
			continue;
		}

		if (label == "Video Codec" ||
			label == "Audio Codec" ||
			label == "Subtitle Codec" ||
			label == "Video Resolution" ||
			label == "Frame Rate" ||
			label == "Audio Channels" ||
			label == "Audio Rate") {
			return true;
		}
	}

	return false;
}

libvlc_meta_t toLibvlcMetaField(ofxVlc4::MediaMetaField field) {
	switch (field) {
	case ofxVlc4::MediaMetaField::Artist:
		return libvlc_meta_Artist;
	case ofxVlc4::MediaMetaField::Album:
		return libvlc_meta_Album;
	case ofxVlc4::MediaMetaField::Genre:
		return libvlc_meta_Genre;
	case ofxVlc4::MediaMetaField::Description:
		return libvlc_meta_Description;
	case ofxVlc4::MediaMetaField::Date:
		return libvlc_meta_Date;
	case ofxVlc4::MediaMetaField::Language:
		return libvlc_meta_Language;
	case ofxVlc4::MediaMetaField::Publisher:
		return libvlc_meta_Publisher;
	case ofxVlc4::MediaMetaField::NowPlaying:
		return libvlc_meta_NowPlaying;
	case ofxVlc4::MediaMetaField::Director:
		return libvlc_meta_Director;
	case ofxVlc4::MediaMetaField::ShowName:
		return libvlc_meta_ShowName;
	case ofxVlc4::MediaMetaField::ArtworkUrl:
		return libvlc_meta_ArtworkURL;
	case ofxVlc4::MediaMetaField::Url:
		return libvlc_meta_URL;
	case ofxVlc4::MediaMetaField::Title:
	default:
		return libvlc_meta_Title;
	}
}

ofxVlc4::MediaParseStatus toMediaParseStatus(libvlc_media_parsed_status_t status) {
	switch (status) {
	case libvlc_media_parsed_status_pending:
		return ofxVlc4::MediaParseStatus::Pending;
	case libvlc_media_parsed_status_skipped:
		return ofxVlc4::MediaParseStatus::Skipped;
	case libvlc_media_parsed_status_failed:
		return ofxVlc4::MediaParseStatus::Failed;
	case libvlc_media_parsed_status_timeout:
		return ofxVlc4::MediaParseStatus::Timeout;
	case libvlc_media_parsed_status_cancelled:
		return ofxVlc4::MediaParseStatus::Cancelled;
	case libvlc_media_parsed_status_done:
		return ofxVlc4::MediaParseStatus::Done;
	case libvlc_media_parsed_status_none:
	default:
		return ofxVlc4::MediaParseStatus::None;
	}
}

libvlc_media_parse_flag_t toLibvlcMediaParseFlags(const ofxVlc4::MediaParseOptions & options) {
	unsigned flags = 0;
	if (options.parseLocal) {
		flags |= libvlc_media_parse_local;
	}
	if (options.parseNetwork) {
		flags |= libvlc_media_parse_network;
	}
	if (options.forced) {
		flags |= libvlc_media_parse_forced;
	}
	if (options.fetchLocal) {
		flags |= libvlc_media_fetch_local;
	}
	if (options.fetchNetwork) {
		flags |= libvlc_media_fetch_network;
	}
	if (options.doInteract) {
		flags |= libvlc_media_do_interact;
	}
	return static_cast<libvlc_media_parse_flag_t>(flags);
}

libvlc_picture_type_t toLibvlcThumbnailImageType(ofxVlc4::ThumbnailImageType type) {
	switch (type) {
	case ofxVlc4::ThumbnailImageType::Jpg:
		return libvlc_picture_Jpg;
	case ofxVlc4::ThumbnailImageType::WebP:
		return libvlc_picture_WebP;
	case ofxVlc4::ThumbnailImageType::Png:
	default:
		return libvlc_picture_Png;
	}
}

libvlc_thumbnailer_seek_speed_t toLibvlcThumbnailSeekSpeed(ofxVlc4::ThumbnailSeekSpeed speed) {
	switch (speed) {
	case ofxVlc4::ThumbnailSeekSpeed::Fast:
		return libvlc_media_thumbnail_seek_fast;
	case ofxVlc4::ThumbnailSeekSpeed::Precise:
	default:
		return libvlc_media_thumbnail_seek_precise;
	}
}

std::string artworkFileExtension(libvlc_picture_type_t pictureType) {
	switch (pictureType) {
	case libvlc_picture_Png:
		return ".png";
	case libvlc_picture_Jpg:
		return ".jpg";
	case libvlc_picture_WebP:
		return ".webp";
	default:
		return ".img";
	}
}

std::string savePictureToTempFile(
	libvlc_picture_t * picture,
	const std::string & prefix,
	const std::string & stem) {
	if (!picture) {
		return "";
	}

	const std::string fileStem = stem.empty() ? "current_media" : stem;
	const auto uniqueId = static_cast<long long>(
		std::chrono::steady_clock::now().time_since_epoch().count());
	const std::string tempFileName =
		prefix + "_" +
		ofToString(std::hash<std::string>{}(fileStem)) + "_" +
		ofToString(uniqueId) +
		artworkFileExtension(libvlc_picture_type(picture));
	const std::filesystem::path tempPath = std::filesystem::temp_directory_path() / tempFileName;
	return libvlc_picture_save(picture, tempPath.string().c_str()) == 0 ? tempPath.string() : "";
}

uint64_t fileSizeIfAvailable(const std::string & path) {
	const std::string trimmedPath = trimWhitespace(path);
	if (trimmedPath.empty()) {
		return 0;
	}

	std::error_code ec;
	const uint64_t size = std::filesystem::file_size(std::filesystem::path(trimmedPath), ec);
	return ec ? 0 : size;
}

}

MediaLibrary::MediaLibrary(ofxVlc4 & owner)
	: owner(owner) {
}

bool MediaLibrary::isPlaylistIndexLocked(int index) const {
	return index >= 0 && index < static_cast<int>(owner.m_impl->mediaLibrary.playlist.size());
}

std::string MediaLibrary::getPathAtIndexLocked(int index) const {
	return isPlaylistIndexLocked(index) ? owner.m_impl->mediaLibrary.playlist[static_cast<size_t>(index)] : "";
}

std::string MediaLibrary::getCurrentPathLocked() const {
	return getPathAtIndexLocked(owner.m_impl->mediaLibrary.currentIndex);
}

int MediaLibrary::getCurrentIndexLocked() const {
	return owner.m_impl->mediaLibrary.currentIndex;
}

size_t MediaLibrary::getPlaylistSizeLocked() const {
	return owner.m_impl->mediaLibrary.playlist.size();
}

bool MediaLibrary::hasPlaylistLocked() const {
	return !owner.m_impl->mediaLibrary.playlist.empty();
}

void MediaLibrary::setCurrentIndexLocked(int index) {
	owner.m_impl->mediaLibrary.currentIndex = index;
}

void MediaLibrary::clearCurrentIndexLocked() {
	setCurrentIndexLocked(-1);
}

void MediaLibrary::appendPlaylistPathLocked(const std::string & path) {
	owner.m_impl->mediaLibrary.playlist.push_back(path);
}

void MediaLibrary::insertPlaylistPathLocked(int index, const std::string & path) {
	owner.m_impl->mediaLibrary.playlist.insert(owner.m_impl->mediaLibrary.playlist.begin() + index, path);
}

void MediaLibrary::insertPlaylistItemsLocked(int index, std::vector<std::string> items) {
	owner.m_impl->mediaLibrary.playlist.insert(
		owner.m_impl->mediaLibrary.playlist.begin() + index,
		std::make_move_iterator(items.begin()),
		std::make_move_iterator(items.end()));
}

void MediaLibrary::clearPlaylistLocked() {
	owner.m_impl->mediaLibrary.playlist.clear();
	clearCurrentIndexLocked();
}

void MediaLibrary::erasePlaylistIndexLocked(int index) {
	owner.m_impl->mediaLibrary.playlist.erase(owner.m_impl->mediaLibrary.playlist.begin() + index);
}

void MediaLibrary::replacePlaylistLocked(std::vector<std::string> items) {
	owner.m_impl->mediaLibrary.playlist = std::move(items);
}

void MediaLibrary::addToPlaylistInternal(const std::string & path, bool preloadMetadata) {
	if (!owner.sessionInstance()) {
		owner.setError("Initialize libvlc first.");
		return;
	}

	{
		std::lock_guard<std::mutex> lock(owner.m_impl->mediaLibrary.metadataCacheMutex);
		owner.m_impl->mediaLibrary.metadataCache.erase(path);
	}

	{
		std::lock_guard<std::mutex> lock(owner.m_impl->mediaLibrary.playlistMutex);
		appendPlaylistPathLocked(path);
		if (getCurrentIndexLocked() < 0 && hasPlaylistLocked()) {
			setCurrentIndexLocked(0);
		}
	}
	owner.setStatus("Added media to playlist.");
	owner.logNotice("Playlist item added: " + ofxVlc4Utils::mediaLabelForPath(path) + ".");

	if (preloadMetadata) {
		getMetadataAtIndex(static_cast<int>(getPlaylistSize()));
	}
}

void MediaLibrary::addToPlaylist(const std::string & path) {
	addToPlaylistInternal(path, true);
}

int MediaLibrary::addPathToPlaylist(const std::string & path) {
	return addPathToPlaylist(path, {});
}

int MediaLibrary::addPathToPlaylist(const std::string & path, std::initializer_list<std::string> extensions) {
	if (!owner.sessionInstance()) {
		owner.setError("Initialize libvlc first.");
		return 0;
	}

	const std::set<std::string> requestedExtensions =
		extensions.size() > 0 ? normalizeExtensions(extensions) : std::set<std::string>();
	const std::string trimmed = trimWhitespace(path);
	if (trimmed.empty()) {
		owner.setError("Path is empty.");
		return 0;
	}

	if (isUri(trimmed)) {
		addToPlaylist(trimmed);
		owner.setStatus("Added URI to playlist.");
		return 1;
	}

	const std::string resolvedPath = ofFilePath::getAbsolutePath(trimmed);
	ofFile file(resolvedPath);
	if (!file.exists()) {
		owner.setError("Path not found: " + trimmed);
		return 0;
	}

	if (file.isDirectory()) {
		ofDirectory dir(resolvedPath);
		dir.listDir();

		int added = 0;
		for (int i = 0; i < dir.size(); ++i) {
			if (owner.isSupportedMediaFile(dir.getFile(i), requestedExtensions.empty() ? nullptr : &requestedExtensions)) {
				addToPlaylistInternal(dir.getPath(i), false);
				++added;
			}
		}
		if (added == 0) {
			owner.setError("No supported media files found in folder.");
		} else {
			owner.setStatus("Added " + ofToString(added) + " media item(s) from folder.");
			const int current = getCurrentIndex();
			if (current >= 0) {
				getMetadataAtIndex(current);
			}
		}
		return added;
	}

	if (!owner.isSupportedMediaFile(file, requestedExtensions.empty() ? nullptr : &requestedExtensions)) {
		owner.setError("Unsupported media file type: " + resolvedPath);
		return 0;
	}

	addToPlaylistInternal(resolvedPath, true);
	owner.setStatus("Added media file to playlist.");
	return 1;
}

void MediaLibrary::clearPlaylistState() {
	{
		std::lock_guard<std::mutex> lock(owner.m_impl->mediaLibrary.playlistMutex);
		clearPlaylistLocked();
	}
	clearMetadataCache();
}

void MediaLibrary::resetCurrentMediaParseState() {
	owner.m_impl->mediaRuntime.currentMediaParseStatus.store(static_cast<int>(ofxVlc4::MediaParseStatus::None));
	owner.m_impl->mediaRuntime.lastCompletedMediaParseStatus.store(static_cast<int>(ofxVlc4::MediaParseStatus::None));
	owner.m_impl->mediaRuntime.mediaParseRequested.store(false);
	owner.m_impl->mediaRuntime.mediaParseActive.store(false);
}

libvlc_media_t * MediaLibrary::retainCurrentOrLoadedMedia() const {
	libvlc_media_player_t * player = owner.sessionPlayer();
	libvlc_media_t * currentMedia = owner.sessionMedia();
	if (player) {
		libvlc_media_t * playerMedia = libvlc_media_player_get_media(player);
		if (!playerMedia) {
			return nullptr;
		}

		libvlc_media_retain(playerMedia);
		return playerMedia;
	}

	if (!currentMedia) {
		return nullptr;
	}

	libvlc_media_retain(currentMedia);
	return currentMedia;
}

std::vector<std::pair<std::string, std::string>> MediaLibrary::buildMetadataForMedia(libvlc_media_t * sourceMedia) const {
	std::vector<std::pair<std::string, std::string>> metadata;
	if (!sourceMedia) {
		return metadata;
	}

	const std::pair<const char *, libvlc_meta_t> knownMetadata[] = {
		{ "Title", libvlc_meta_Title },
		{ "Artist", libvlc_meta_Artist },
		{ "Album", libvlc_meta_Album },
		{ "Artwork URL", libvlc_meta_ArtworkURL },
		{ "Album Artist", libvlc_meta_AlbumArtist },
		{ "Genre", libvlc_meta_Genre },
		{ "Date", libvlc_meta_Date },
		{ "Track", libvlc_meta_TrackNumber },
		{ "Track Total", libvlc_meta_TrackTotal },
		{ "Disc", libvlc_meta_DiscNumber },
		{ "Disc Total", libvlc_meta_DiscTotal },
		{ "Show", libvlc_meta_ShowName },
		{ "Season", libvlc_meta_Season },
		{ "Episode", libvlc_meta_Episode },
		{ "Director", libvlc_meta_Director },
		{ "Actors", libvlc_meta_Actors },
		{ "Publisher", libvlc_meta_Publisher },
		{ "Language", libvlc_meta_Language },
		{ "Now Playing", libvlc_meta_NowPlaying },
		{ "Description", libvlc_meta_Description },
		{ "URL", libvlc_meta_URL }
	};

	for (const auto & [label, metaType] : knownMetadata) {
		const std::string value = readMediaMeta(sourceMedia, metaType);
		if (!value.empty()) {
			metadata.emplace_back(label, value);
		}
	}

	const libvlc_time_t duration = libvlc_media_get_duration(sourceMedia);
	if (duration > 0) {
		appendMetadataValue(metadata, "Duration", ofToString(static_cast<int64_t>(duration / 1000)));
	}

	appendTrackMetadataFromMediaTracklist(metadata, sourceMedia, libvlc_track_video, "Video");
	appendTrackMetadataFromMediaTracklist(metadata, sourceMedia, libvlc_track_audio, "Audio");
	appendTrackMetadataFromMediaTracklist(metadata, sourceMedia, libvlc_track_text, "Subtitle");

	return metadata;
}

std::vector<std::string> MediaLibrary::getPlaylist() const {
	std::lock_guard<std::mutex> lock(owner.m_impl->mediaLibrary.playlistMutex);
	return owner.m_impl->mediaLibrary.playlist;
}

std::vector<ofxVlc4::PlaylistItemInfo> MediaLibrary::getPlaylistItems() const {
	std::vector<std::string> paths;
	int currentIndex = -1;
	{
		std::lock_guard<std::mutex> lock(owner.m_impl->mediaLibrary.playlistMutex);
		paths = owner.m_impl->mediaLibrary.playlist;
		currentIndex = owner.m_impl->mediaLibrary.currentIndex;
	}

	std::vector<ofxVlc4::PlaylistItemInfo> items;
	items.reserve(paths.size());

	std::lock_guard<std::mutex> metadataLock(owner.m_impl->mediaLibrary.metadataCacheMutex);
	for (size_t i = 0; i < paths.size(); ++i) {
		const std::string & path = paths[i];
		ofxVlc4::PlaylistItemInfo item;
		item.index = static_cast<int>(i);
		item.path = path;
		item.label = ofxVlc4Utils::mediaLabelForPath(path);
		item.current = item.index == currentIndex;
		item.uri = isUri(path);
		item.metadataCached = owner.m_impl->mediaLibrary.metadataCache.find(path) != owner.m_impl->mediaLibrary.metadataCache.end();
		items.push_back(std::move(item));
	}

	return items;
}

ofxVlc4::PlaylistStateInfo MediaLibrary::getPlaylistStateInfo() const {
	ofxVlc4::PlaylistStateInfo state;
	state.items = getPlaylistItems();
	state.size = state.items.size();
	state.empty = state.items.empty();
	{
		std::lock_guard<std::mutex> lock(owner.m_impl->mediaLibrary.playlistMutex);
		state.currentIndex = owner.m_impl->mediaLibrary.currentIndex;
	}
	state.hasCurrent = state.currentIndex >= 0 && state.currentIndex < static_cast<int>(state.size);
	return state;
}

ofxVlc4::PlaylistItemInfo MediaLibrary::getCurrentPlaylistItemInfo() const {
	const auto state = getPlaylistStateInfo();
	if (!state.hasCurrent) {
		return {};
	}
	return state.items[static_cast<size_t>(state.currentIndex)];
}

void MediaLibrary::clearGeneratedThumbnailInfo() {
	std::lock_guard<std::mutex> lock(owner.m_impl->mediaRuntime.thumbnailMutex);
	owner.m_impl->mediaRuntime.lastGeneratedThumbnail = ofxVlc4::ThumbnailInfo {};
}

void MediaLibrary::clearMetadataCache() {
	std::lock_guard<std::mutex> lock(owner.m_impl->mediaLibrary.metadataCacheMutex);
	owner.m_impl->mediaLibrary.metadataCache.clear();
}

void MediaLibrary::cacheArtworkPathForMediaPath(const std::string & mediaPath, const std::string & artworkPath) {
	const std::string currentPath = trimWhitespace(mediaPath);
	if (currentPath.empty() || artworkPath.empty()) {
		return;
	}

	std::lock_guard<std::mutex> lock(owner.m_impl->mediaLibrary.metadataCacheMutex);
	auto & metadata = owner.m_impl->mediaLibrary.metadataCache[currentPath];
	auto existing = std::find_if(
		metadata.begin(),
		metadata.end(),
		[](const auto & entry) { return entry.first == "Artwork URL"; });
	if (existing != metadata.end()) {
		existing->second = artworkPath;
	} else {
		metadata.emplace_back("Artwork URL", artworkPath);
	}
}

std::string MediaLibrary::getPathAtIndex(int index) const {
	std::lock_guard<std::mutex> lock(owner.m_impl->mediaLibrary.playlistMutex);
	return getPathAtIndexLocked(index);
}

std::string MediaLibrary::getFileNameAtIndex(int index) const {
	const std::string currentPath = getPathAtIndex(index);
	if (currentPath.empty()) {
		return "";
	}

	if (isUri(currentPath)) {
		const std::string uriFileName = fileNameFromUri(currentPath);
		return uriFileName.empty() ? currentPath : uriFileName;
	}

	return ofFilePath::getFileName(currentPath);
}

std::string MediaLibrary::getCurrentPath() const {
	std::lock_guard<std::mutex> lock(owner.m_impl->mediaLibrary.playlistMutex);
	return getCurrentPathLocked();
}

std::string MediaLibrary::getCurrentFileName() const {
	return getFileNameAtIndex(getCurrentIndex());
}

int MediaLibrary::getCurrentIndex() const {
	std::lock_guard<std::mutex> lock(owner.m_impl->mediaLibrary.playlistMutex);
	return getCurrentIndexLocked();
}

size_t MediaLibrary::getPlaylistSize() const {
	std::lock_guard<std::mutex> lock(owner.m_impl->mediaLibrary.playlistMutex);
	return getPlaylistSizeLocked();
}

bool MediaLibrary::hasPlaylist() const {
	std::lock_guard<std::mutex> lock(owner.m_impl->mediaLibrary.playlistMutex);
	return hasPlaylistLocked();
}

MediaLibrary::RemovePlaylistItemResult MediaLibrary::removePlaylistItem(int index) {
	RemovePlaylistItemResult result;
	{
		std::lock_guard<std::mutex> lock(owner.m_impl->mediaLibrary.playlistMutex);
		if (!isPlaylistIndexLocked(index)) {
			return result;
		}

		const int currentIndex = getCurrentIndexLocked();
		result.removed = true;
		result.wasCurrent = (index == currentIndex);

		erasePlaylistIndexLocked(index);
		if (!hasPlaylistLocked()) {
			clearCurrentIndexLocked();
			result.playlistEmpty = true;
		} else {
			if (index < currentIndex) {
				setCurrentIndexLocked(currentIndex - 1);
			} else if (index == currentIndex && currentIndex >= static_cast<int>(getPlaylistSizeLocked())) {
				setCurrentIndexLocked(static_cast<int>(getPlaylistSizeLocked()) - 1);
			}
			if (result.wasCurrent && getCurrentIndexLocked() >= 0) {
				result.replacementIndex = getCurrentIndexLocked();
			}
		}
	}

	clearMetadataCache();
	owner.logNotice("Playlist item removed.");
	return result;
}

void MediaLibrary::movePlaylistItem(int fromIndex, int toIndex) {
	{
		std::lock_guard<std::mutex> lock(owner.m_impl->mediaLibrary.playlistMutex);
		if (!isPlaylistIndexLocked(fromIndex)) {
			return;
		}
		if (toIndex < 0) {
			return;
		}
		if (toIndex > static_cast<int>(getPlaylistSizeLocked())) {
			toIndex = static_cast<int>(getPlaylistSizeLocked());
		}

		if (toIndex == fromIndex || toIndex == fromIndex + 1) {
			return;
		}

		const int originalCurrent = getCurrentIndexLocked();
		const std::string moved = getPathAtIndexLocked(fromIndex);
		const int insertIndex = (fromIndex < toIndex) ? (toIndex - 1) : toIndex;

		erasePlaylistIndexLocked(fromIndex);
		insertPlaylistPathLocked(insertIndex, moved);

		if (originalCurrent == fromIndex) {
			setCurrentIndexLocked(insertIndex);
		} else if (originalCurrent > fromIndex && originalCurrent <= insertIndex) {
			setCurrentIndexLocked(originalCurrent - 1);
		} else if (originalCurrent < fromIndex && originalCurrent >= insertIndex) {
			setCurrentIndexLocked(originalCurrent + 1);
		} else {
			setCurrentIndexLocked(originalCurrent);
		}
	}

	owner.logNotice("Playlist item moved.");
}

void MediaLibrary::movePlaylistItems(const std::vector<int> & fromIndices, int toIndex) {
	if (fromIndices.empty()) {
		return;
	}

	std::vector<int> indices = fromIndices;
	{
		std::lock_guard<std::mutex> lock(owner.m_impl->mediaLibrary.playlistMutex);
		if (!hasPlaylistLocked()) {
			return;
		}

		std::sort(indices.begin(), indices.end());
		indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

		indices.erase(
			std::remove_if(
				indices.begin(),
				indices.end(),
				[this](int index) { return !isPlaylistIndexLocked(index); }),
			indices.end());

		if (indices.empty()) {
			return;
		}

		if (toIndex < 0) {
			toIndex = 0;
		} else if (toIndex > static_cast<int>(getPlaylistSizeLocked())) {
			toIndex = static_cast<int>(getPlaylistSizeLocked());
		}

		std::vector<std::string> movedItems;
		movedItems.reserve(indices.size());
		std::vector<std::string> remaining;
		remaining.reserve(getPlaylistSizeLocked() - indices.size());

		size_t selectedCursor = 0;
		for (int i = 0; i < static_cast<int>(getPlaylistSizeLocked()); ++i) {
			if (selectedCursor < indices.size() && indices[selectedCursor] == i) {
				movedItems.push_back(getPathAtIndexLocked(i));
				++selectedCursor;
			} else {
				remaining.push_back(getPathAtIndexLocked(i));
			}
		}

		const int removedBeforeInsert =
			static_cast<int>(std::count_if(indices.begin(), indices.end(), [toIndex](int index) { return index < toIndex; }));
		int adjustedInsertIndex = toIndex - removedBeforeInsert;
		adjustedInsertIndex = ofClamp(adjustedInsertIndex, 0, static_cast<int>(remaining.size()));

		const int currentIndex = getCurrentIndexLocked();
		if (currentIndex >= 0) {
			const auto currentIt = std::find(indices.begin(), indices.end(), currentIndex);
			if (currentIt != indices.end()) {
				setCurrentIndexLocked(adjustedInsertIndex + static_cast<int>(std::distance(indices.begin(), currentIt)));
			} else {
				const int removedBeforeCurrent =
					static_cast<int>(std::count_if(indices.begin(), indices.end(), [currentIndex](int index) { return index < currentIndex; }));
				int remainingIndex = currentIndex - removedBeforeCurrent;
				if (remainingIndex >= adjustedInsertIndex) {
					remainingIndex += static_cast<int>(movedItems.size());
				}
				setCurrentIndexLocked(remainingIndex);
			}
		}

		replacePlaylistLocked(std::move(remaining));
		insertPlaylistItemsLocked(adjustedInsertIndex, std::move(movedItems));
	}
	clearMetadataCache();
	owner.logNotice("Playlist items moved.");
}

std::vector<std::pair<std::string, std::string>> MediaLibrary::getMetadataAtIndex(int index) const {
	const std::string path = getPathAtIndex(index);
	if (path.empty()) {
		return {};
	}

	std::vector<std::pair<std::string, std::string>> currentMediaMetadata;
	libvlc_media_t * currentMedia = owner.sessionMedia();
	if (index == getCurrentIndex() && currentMedia) {
		currentMediaMetadata = buildMetadataForMedia(currentMedia);
		if (!currentMediaMetadata.empty()) {
			std::lock_guard<std::mutex> lock(owner.m_impl->mediaLibrary.metadataCacheMutex);
			const auto cached = owner.m_impl->mediaLibrary.metadataCache.find(path);
			if (cached != owner.m_impl->mediaLibrary.metadataCache.end() &&
				(cached->second.size() > currentMediaMetadata.size() ||
					(hasDetailedTrackMetadata(cached->second) && !hasDetailedTrackMetadata(currentMediaMetadata)))) {
				return cached->second;
			}
			owner.m_impl->mediaLibrary.metadataCache[path] = currentMediaMetadata;
			return currentMediaMetadata;
		}
	}

	std::vector<std::pair<std::string, std::string>> cachedMetadata;
	{
		std::lock_guard<std::mutex> lock(owner.m_impl->mediaLibrary.metadataCacheMutex);
		const auto cached = owner.m_impl->mediaLibrary.metadataCache.find(path);
		if (cached != owner.m_impl->mediaLibrary.metadataCache.end()) {
			cachedMetadata = cached->second;
			if (hasDetailedTrackMetadata(cachedMetadata)) {
				return cachedMetadata;
			}
		}
	}

	libvlc_instance_t * instance = owner.sessionInstance();
	if (!instance) {
		return cachedMetadata;
	}

	libvlc_media_t * inspectMedia = isUri(path)
		? libvlc_media_new_location(path.c_str())
		: libvlc_media_new_path(path.c_str());
	if (!inspectMedia) {
		return cachedMetadata.empty() ? currentMediaMetadata : cachedMetadata;
	}

	libvlc_media_parse_flag_t parseFlags = libvlc_media_parse_forced;
	if (isUri(path)) {
		parseFlags = static_cast<libvlc_media_parse_flag_t>(
			parseFlags | libvlc_media_parse_network | libvlc_media_fetch_network);
	} else {
		parseFlags = static_cast<libvlc_media_parse_flag_t>(
			parseFlags | libvlc_media_parse_local | libvlc_media_fetch_local);
	}
	if (libvlc_media_parse_request(instance, inspectMedia, parseFlags, 1500) == 0) {
		waitForMediaParse(inspectMedia, 1500);
	}

	std::vector<std::pair<std::string, std::string>> metadata = buildMetadataForMedia(inspectMedia);
	libvlc_media_release(inspectMedia);

	if (metadata.empty()) {
		if (!cachedMetadata.empty()) {
			return cachedMetadata;
		}
		return currentMediaMetadata;
	}

	{
		std::lock_guard<std::mutex> lock(owner.m_impl->mediaLibrary.metadataCacheMutex);
		const auto cached = owner.m_impl->mediaLibrary.metadataCache.find(path);
		if (cached == owner.m_impl->mediaLibrary.metadataCache.end() ||
			metadata.size() >= cached->second.size() ||
			hasDetailedTrackMetadata(metadata)) {
			owner.m_impl->mediaLibrary.metadataCache[path] = metadata;
		}
	}

	return metadata;
}

std::vector<std::pair<std::string, std::string>> MediaLibrary::getCurrentMetadata() const {
	return getMetadataAtIndex(getCurrentIndex());
}

ofxVlc4::MediaParseInfo MediaLibrary::getCurrentMediaParseInfo() const {
	ofxVlc4::MediaParseInfo info;
	info.status = static_cast<ofxVlc4::MediaParseStatus>(owner.m_impl->mediaRuntime.currentMediaParseStatus.load());
	info.lastCompletedStatus = static_cast<ofxVlc4::MediaParseStatus>(owner.m_impl->mediaRuntime.lastCompletedMediaParseStatus.load());
	info.requested = owner.m_impl->mediaRuntime.mediaParseRequested.load();
	info.active = owner.m_impl->mediaRuntime.mediaParseActive.load();

	if (!info.requested && !info.active && info.status == ofxVlc4::MediaParseStatus::None) {
		libvlc_media_t * sourceMedia = retainCurrentOrLoadedMedia();
		if (sourceMedia) {
			info.status = toMediaParseStatus(libvlc_media_get_parsed_status(sourceMedia));
			libvlc_media_release(sourceMedia);
			info.active = info.status == ofxVlc4::MediaParseStatus::Pending;
		}
	}

	return info;
}

ofxVlc4::MediaParseStatus MediaLibrary::getCurrentMediaParseStatus() const {
	return getCurrentMediaParseInfo().status;
}

ofxVlc4::MediaParseOptions MediaLibrary::getMediaParseOptions() const {
	return owner.m_impl->mediaRuntime.mediaParseOptions;
}

void MediaLibrary::setMediaParseOptions(const ofxVlc4::MediaParseOptions & options) {
	owner.m_impl->mediaRuntime.mediaParseOptions.parseLocal = options.parseLocal;
	owner.m_impl->mediaRuntime.mediaParseOptions.parseNetwork = options.parseNetwork;
	owner.m_impl->mediaRuntime.mediaParseOptions.forced = options.forced;
	owner.m_impl->mediaRuntime.mediaParseOptions.fetchLocal = options.fetchLocal;
	owner.m_impl->mediaRuntime.mediaParseOptions.fetchNetwork = options.fetchNetwork;
	owner.m_impl->mediaRuntime.mediaParseOptions.doInteract = options.doInteract;
	owner.m_impl->mediaRuntime.mediaParseOptions.timeoutMs = std::max(-1, options.timeoutMs);
}

bool MediaLibrary::requestCurrentMediaParse() {
	libvlc_instance_t * instance = owner.sessionInstance();
	if (!instance) {
		return false;
	}

	libvlc_media_t * sourceMedia = retainCurrentOrLoadedMedia();
	if (!sourceMedia) {
		return false;
	}

	const int result = libvlc_media_parse_request(
		instance,
		sourceMedia,
		toLibvlcMediaParseFlags(owner.m_impl->mediaRuntime.mediaParseOptions),
		owner.m_impl->mediaRuntime.mediaParseOptions.timeoutMs);

	libvlc_media_release(sourceMedia);

	if (result != 0) {
		return false;
	}

	owner.m_impl->mediaRuntime.currentMediaParseStatus.store(static_cast<int>(ofxVlc4::MediaParseStatus::Pending));
	owner.m_impl->mediaRuntime.mediaParseRequested.store(true);
	owner.m_impl->mediaRuntime.mediaParseActive.store(true);
	owner.setStatus("Media parse requested.");
	return true;
}

void MediaLibrary::stopCurrentMediaParse() {
	libvlc_instance_t * instance = owner.sessionInstance();
	if (!instance) {
		return;
	}

	libvlc_media_t * sourceMedia = retainCurrentOrLoadedMedia();
	if (!sourceMedia) {
		return;
	}

	libvlc_media_parse_stop(instance, sourceMedia);
	libvlc_media_release(sourceMedia);
	owner.m_impl->mediaRuntime.mediaParseRequested.store(false);
	owner.setStatus("Media parse stop requested.");
}

ofxVlc4::MediaStats MediaLibrary::getMediaStats() const {
	ofxVlc4::MediaStats stats;

	libvlc_media_t * sourceMedia = retainCurrentOrLoadedMedia();
	if (!sourceMedia) {
		return stats;
	}

	libvlc_media_stats_t rawStats {};
	stats.available = libvlc_media_get_stats(sourceMedia, &rawStats);
	if (stats.available) {
		stats.readBytes = rawStats.i_read_bytes;
		stats.inputBitrate = rawStats.f_input_bitrate;
		stats.demuxReadBytes = rawStats.i_demux_read_bytes;
		stats.demuxBitrate = rawStats.f_demux_bitrate;
		stats.demuxCorrupted = rawStats.i_demux_corrupted;
		stats.demuxDiscontinuity = rawStats.i_demux_discontinuity;
		stats.decodedVideo = rawStats.i_decoded_video;
		stats.decodedAudio = rawStats.i_decoded_audio;
		stats.displayedPictures = rawStats.i_displayed_pictures;
		stats.latePictures = rawStats.i_late_pictures;
		stats.lostPictures = rawStats.i_lost_pictures;
		stats.playedAudioBuffers = rawStats.i_played_abuffers;
		stats.lostAudioBuffers = rawStats.i_lost_abuffers;
	}

	libvlc_media_release(sourceMedia);

	return stats;
}

void MediaLibrary::handleMediaParsedChanged(libvlc_media_parsed_status_t status) {
	if (!owner.sessionMedia()) {
		return;
	}

	const ofxVlc4::MediaParseStatus publicStatus =
		static_cast<ofxVlc4::MediaParseStatus>(status);
	owner.m_impl->mediaRuntime.currentMediaParseStatus.store(static_cast<int>(publicStatus));
	owner.m_impl->mediaRuntime.mediaParseRequested.store(false);
	owner.m_impl->mediaRuntime.mediaParseActive.store(publicStatus == ofxVlc4::MediaParseStatus::Pending);
	if (publicStatus == ofxVlc4::MediaParseStatus::Skipped ||
		publicStatus == ofxVlc4::MediaParseStatus::Failed ||
		publicStatus == ofxVlc4::MediaParseStatus::Timeout ||
		publicStatus == ofxVlc4::MediaParseStatus::Cancelled ||
		publicStatus == ofxVlc4::MediaParseStatus::Done) {
		owner.m_impl->mediaRuntime.lastCompletedMediaParseStatus.store(static_cast<int>(publicStatus));
	}
	owner.refreshPixelAspectRatio();
	owner.refreshDisplayAspectRatio();
}

ofxVlc4::ThumbnailInfo MediaLibrary::getLastGeneratedThumbnail() const {
	std::lock_guard<std::mutex> lock(owner.m_impl->mediaRuntime.thumbnailMutex);
	return owner.m_impl->mediaRuntime.lastGeneratedThumbnail;
}

bool MediaLibrary::requestThumbnailByTime(
	int timeMs,
	unsigned width,
	unsigned height,
	bool crop,
	ofxVlc4::ThumbnailImageType type,
	ofxVlc4::ThumbnailSeekSpeed speed,
	int timeoutMs) {
	libvlc_instance_t * instance = owner.sessionInstance();
	if (!instance) {
		owner.setError("Initialize libvlc first.");
		return false;
	}

	libvlc_media_t * sourceMedia = retainCurrentOrLoadedMedia();
	if (!sourceMedia) {
		owner.setError("Load media before requesting a thumbnail.");
		return false;
	}

	cancelThumbnailRequest();
	clearGeneratedThumbnailInfo();

	libvlc_media_thumbnail_request_t * request = libvlc_media_thumbnail_request_by_time(
		instance,
		sourceMedia,
		std::max(0, timeMs),
		toLibvlcThumbnailSeekSpeed(speed),
		width,
		height,
		crop,
		toLibvlcThumbnailImageType(type),
		std::max(0, timeoutMs));

	libvlc_media_release(sourceMedia);

	if (!request) {
		owner.setError("Thumbnail request could not be created.");
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(owner.m_impl->mediaRuntime.thumbnailMutex);
		owner.m_impl->mediaRuntime.thumbnailRequest = request;
		owner.m_impl->mediaRuntime.lastGeneratedThumbnail.requestActive = true;
	}

	owner.setStatus("Thumbnail request queued.");
	return true;
}

bool MediaLibrary::requestThumbnailByPosition(
	float position,
	unsigned width,
	unsigned height,
	bool crop,
	ofxVlc4::ThumbnailImageType type,
	ofxVlc4::ThumbnailSeekSpeed speed,
	int timeoutMs) {
	libvlc_instance_t * instance = owner.sessionInstance();
	if (!instance) {
		owner.setError("Initialize libvlc first.");
		return false;
	}

	libvlc_media_t * sourceMedia = retainCurrentOrLoadedMedia();
	if (!sourceMedia) {
		owner.setError("Load media before requesting a thumbnail.");
		return false;
	}

	cancelThumbnailRequest();
	clearGeneratedThumbnailInfo();

	libvlc_media_thumbnail_request_t * request = libvlc_media_thumbnail_request_by_pos(
		instance,
		sourceMedia,
		ofClamp(position, 0.0f, 1.0f),
		toLibvlcThumbnailSeekSpeed(speed),
		width,
		height,
		crop,
		toLibvlcThumbnailImageType(type),
		std::max(0, timeoutMs));

	libvlc_media_release(sourceMedia);

	if (!request) {
		owner.setError("Thumbnail request could not be created.");
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(owner.m_impl->mediaRuntime.thumbnailMutex);
		owner.m_impl->mediaRuntime.thumbnailRequest = request;
		owner.m_impl->mediaRuntime.lastGeneratedThumbnail.requestActive = true;
	}

	owner.setStatus("Thumbnail request queued.");
	return true;
}

void MediaLibrary::cancelThumbnailRequest() {
	std::lock_guard<std::mutex> lock(owner.m_impl->mediaRuntime.thumbnailMutex);
	if (owner.m_impl->mediaRuntime.thumbnailRequest) {
		libvlc_media_thumbnail_request_destroy(owner.m_impl->mediaRuntime.thumbnailRequest);
		owner.m_impl->mediaRuntime.thumbnailRequest = nullptr;
	}
	owner.m_impl->mediaRuntime.lastGeneratedThumbnail.requestActive = false;
}

void MediaLibrary::handleAttachedThumbnailsFound(libvlc_picture_list_t * thumbnails) {
	if (!thumbnails || libvlc_picture_list_count(thumbnails) == 0) {
		return;
	}

	libvlc_picture_t * picture = libvlc_picture_list_at(thumbnails, 0);
	if (!picture) {
		return;
	}

	const std::string currentPath = getCurrentPath();
	const std::string artworkStem = currentPath.empty()
		? "current_media"
		: ofxVlc4Utils::mediaLabelForPath(currentPath);
	const std::string artworkPath = savePictureToTempFile(picture, "ofxvlc4_artwork", artworkStem);
	if (!artworkPath.empty()) {
		cacheArtworkPathForMediaPath(currentPath, artworkPath);
	}
}

void MediaLibrary::handleThumbnailGenerated(libvlc_picture_t * picture) {
	std::lock_guard<std::mutex> lock(owner.m_impl->mediaRuntime.thumbnailMutex);
	if (owner.m_impl->mediaRuntime.thumbnailRequest) {
		libvlc_media_thumbnail_request_destroy(owner.m_impl->mediaRuntime.thumbnailRequest);
		owner.m_impl->mediaRuntime.thumbnailRequest = nullptr;
	}
	owner.m_impl->mediaRuntime.lastGeneratedThumbnail.requestActive = false;
	if (!picture) {
		return;
	}

	const std::string currentPath = getCurrentPath();
	const std::string thumbnailStem = currentPath.empty()
		? "current_media"
		: ofxVlc4Utils::mediaLabelForPath(currentPath);
	const std::string thumbnailPath = savePictureToTempFile(
		picture,
		"ofxvlc4_thumbnail",
		thumbnailStem);
	if (thumbnailPath.empty()) {
		return;
	}

	owner.m_impl->mediaRuntime.lastGeneratedThumbnail.available = true;
	owner.m_impl->mediaRuntime.lastGeneratedThumbnail.path = thumbnailPath;
	owner.m_impl->mediaRuntime.lastGeneratedThumbnail.timeMs = libvlc_picture_get_time(picture);
	owner.m_impl->mediaRuntime.lastGeneratedThumbnail.width = libvlc_picture_get_width(picture);
	owner.m_impl->mediaRuntime.lastGeneratedThumbnail.height = libvlc_picture_get_height(picture);
}

void MediaLibrary::updateSnapshotStateOnRequest(const std::string & requestedPath) {
	const std::string trimmedPath = trimWhitespace(requestedPath);
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.playbackStateMutex);
	owner.m_impl->mediaRuntime.snapshotPending = !trimmedPath.empty();
	owner.m_impl->mediaRuntime.snapshotAvailable = !owner.m_impl->mediaRuntime.lastSnapshotPath.empty();
	owner.m_impl->mediaRuntime.pendingSnapshotPath = trimmedPath;
	owner.m_impl->mediaRuntime.lastSnapshotEventMessage = trimmedPath.empty() ? "" : ("Requested: " + trimmedPath);
	owner.m_impl->mediaRuntime.lastSnapshotFailureReason.clear();
}

void MediaLibrary::updateSnapshotStateFromEvent(const std::string & savedPath) {
	std::string resolvedPath;
	{
		std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.playbackStateMutex);
		resolvedPath = trimWhitespace(savedPath);
		if (resolvedPath.empty()) {
			resolvedPath = owner.m_impl->mediaRuntime.pendingSnapshotPath;
		}
		owner.m_impl->mediaRuntime.snapshotPending = false;
		owner.m_impl->mediaRuntime.pendingSnapshotPath.clear();
		owner.m_impl->mediaRuntime.snapshotAvailable = !resolvedPath.empty();
		if (!resolvedPath.empty()) {
			owner.m_impl->mediaRuntime.lastSnapshotPath = resolvedPath;
			owner.m_impl->mediaRuntime.lastSnapshotBytes = fileSizeIfAvailable(resolvedPath);
			owner.m_impl->mediaRuntime.lastSnapshotTimestamp = ofGetTimestampString("%Y-%m-%d %H:%M:%S");
			owner.m_impl->mediaRuntime.lastSnapshotEventMessage = "Saved: " + resolvedPath;
			owner.m_impl->mediaRuntime.lastSnapshotFailureReason.clear();
		}
	}

	if (!resolvedPath.empty()) {
		owner.setStatus("Snapshot saved: " + resolvedPath);
		owner.logNotice("Snapshot saved: " + resolvedPath);
	}
}

void MediaLibrary::clearPendingSnapshotState() {
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.playbackStateMutex);
	owner.m_impl->mediaRuntime.snapshotPending = false;
	owner.m_impl->mediaRuntime.snapshotAvailable = !owner.m_impl->mediaRuntime.lastSnapshotPath.empty();
	owner.m_impl->mediaRuntime.pendingSnapshotPath.clear();
}

void MediaLibrary::updateSnapshotFailureState(const std::string & failureReason) {
	const std::string trimmedReason = trimWhitespace(failureReason);
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.playbackStateMutex);
	owner.m_impl->mediaRuntime.snapshotPending = false;
	owner.m_impl->mediaRuntime.snapshotAvailable = !owner.m_impl->mediaRuntime.lastSnapshotPath.empty();
	owner.m_impl->mediaRuntime.pendingSnapshotPath.clear();
	owner.m_impl->mediaRuntime.lastSnapshotFailureReason = trimmedReason;
	owner.m_impl->mediaRuntime.lastSnapshotEventMessage = trimmedReason.empty() ? "" : ("Failed: " + trimmedReason);
}

std::string MediaLibrary::getCurrentMediaMeta(ofxVlc4::MediaMetaField field) const {
	std::string value;
	libvlc_media_t * sourceMedia = retainCurrentOrLoadedMedia();
	if (!sourceMedia) {
		return value;
	}

	char * rawValue = libvlc_media_get_meta(sourceMedia, toLibvlcMetaField(field));
	if (rawValue) {
		value = trimWhitespace(rawValue);
		libvlc_free(rawValue);
	}

	libvlc_media_release(sourceMedia);
	return value;
}

bool MediaLibrary::setCurrentMediaMeta(ofxVlc4::MediaMetaField field, const std::string & value) {
	libvlc_media_t * sourceMedia = retainCurrentOrLoadedMedia();
	if (!sourceMedia) {
		owner.setError("Load media before editing metadata.");
		return false;
	}

	const std::string trimmedValue = trimWhitespace(value);
	libvlc_media_set_meta(sourceMedia, toLibvlcMetaField(field), trimmedValue.c_str());
	clearMetadataCache();
	libvlc_media_release(sourceMedia);

	owner.setStatus("Media metadata updated.");
	return true;
}

bool MediaLibrary::saveCurrentMediaMeta() {
	libvlc_instance_t * instance = owner.sessionInstance();
	if (!instance) {
		return false;
	}

	libvlc_media_t * sourceMedia = retainCurrentOrLoadedMedia();
	if (!sourceMedia) {
		owner.setError("Load media before saving metadata.");
		return false;
	}

	const bool saved = libvlc_media_save_meta(instance, sourceMedia) == 0;
	libvlc_media_release(sourceMedia);

	if (!saved) {
		owner.setError("Media metadata could not be saved.");
		return false;
	}

	clearMetadataCache();
	owner.setStatus("Media metadata saved.");
	owner.logNotice("Media metadata saved.");
	return true;
}

std::vector<std::string> MediaLibrary::getCurrentMediaMetaExtraNames() const {
	std::vector<std::string> names;
	libvlc_media_t * sourceMedia = retainCurrentOrLoadedMedia();
	if (!sourceMedia) {
		return names;
	}

	char ** rawNames = nullptr;
	const unsigned count = libvlc_media_get_meta_extra_names(sourceMedia, &rawNames);
	libvlc_media_release(sourceMedia);
	if (!rawNames || count == 0) {
		return names;
	}

	names.reserve(count);
	for (unsigned i = 0; i < count; ++i) {
		if (!rawNames[i]) {
			continue;
		}

		const std::string name = trimWhitespace(rawNames[i]);
		if (!name.empty()) {
			names.push_back(name);
		}
	}

	libvlc_media_meta_extra_names_release(rawNames, count);
	std::sort(names.begin(), names.end());
	names.erase(std::unique(names.begin(), names.end()), names.end());
	return names;
}

std::string MediaLibrary::getCurrentMediaMetaExtra(const std::string & name) const {
	const std::string trimmedName = trimWhitespace(name);
	if (trimmedName.empty()) {
		return "";
	}

	libvlc_media_t * sourceMedia = retainCurrentOrLoadedMedia();
	if (!sourceMedia) {
		return "";
	}

	std::string value;
	char * rawValue = libvlc_media_get_meta_extra(sourceMedia, trimmedName.c_str());
	if (rawValue) {
		value = trimWhitespace(rawValue);
		libvlc_free(rawValue);
	}

	libvlc_media_release(sourceMedia);
	return value;
}

bool MediaLibrary::setCurrentMediaMetaExtra(const std::string & name, const std::string & value) {
	const std::string trimmedName = trimWhitespace(name);
	if (trimmedName.empty()) {
		owner.setError("Meta extra name is empty.");
		return false;
	}

	libvlc_media_t * sourceMedia = retainCurrentOrLoadedMedia();
	if (!sourceMedia) {
		owner.setError("Load media before editing metadata.");
		return false;
	}

	const std::string trimmedValue = trimWhitespace(value);
	libvlc_media_set_meta_extra(sourceMedia, trimmedName.c_str(), trimmedValue.c_str());
	clearMetadataCache();
	libvlc_media_release(sourceMedia);

	owner.setStatus("Media extra metadata updated.");
	return true;
}

bool MediaLibrary::removeCurrentMediaMetaExtra(const std::string & name) {
	const std::string trimmedName = trimWhitespace(name);
	if (trimmedName.empty()) {
		owner.setError("Meta extra name is empty.");
		return false;
	}

	libvlc_media_t * sourceMedia = retainCurrentOrLoadedMedia();
	if (!sourceMedia) {
		owner.setError("Load media before editing metadata.");
		return false;
	}

	libvlc_media_set_meta_extra(sourceMedia, trimmedName.c_str(), nullptr);
	clearMetadataCache();
	libvlc_media_release(sourceMedia);

	owner.setStatus("Media extra metadata removed.");
	return true;
}

std::vector<ofxVlc4::BookmarkInfo> MediaLibrary::getBookmarksForPath(const std::string & path) const {
	const std::string trimmedPath = trimWhitespace(path);
	if (trimmedPath.empty()) {
		return {};
	}

	std::vector<ofxVlc4::BookmarkInfo> bookmarks;
	{
		std::lock_guard<std::mutex> lock(owner.m_impl->bookmarkState.mutex);
		const auto it = owner.m_impl->bookmarkState.entries.find(trimmedPath);
		if (it == owner.m_impl->bookmarkState.entries.end()) {
			return {};
		}
		bookmarks = it->second;
	}

	const bool isCurrentPath = trimmedPath == getCurrentPath();
	const int currentTimeMs = isCurrentPath ? owner.getTime() : -1;
	for (ofxVlc4::BookmarkInfo & bookmark : bookmarks) {
		bookmark.current = isCurrentPath && std::abs(bookmark.timeMs - currentTimeMs) <= 500;
	}
	return bookmarks;
}

std::vector<ofxVlc4::BookmarkInfo> MediaLibrary::getCurrentBookmarks() const {
	return getBookmarksForPath(getCurrentPath());
}

bool MediaLibrary::addBookmarkAtTime(int timeMs, const std::string & label) {
	const std::string currentPath = getCurrentPath();
	if (currentPath.empty()) {
		owner.setError("Load media before adding a bookmark.");
		return false;
	}

	ofxVlc4::BookmarkInfo bookmark;
	bookmark.timeMs = std::max(0, timeMs);
	bookmark.label = trimWhitespace(label);
	if (bookmark.label.empty()) {
		bookmark.label = defaultBookmarkLabel(bookmark.timeMs);
	}
	bookmark.id = bookmarkStableId(currentPath, bookmark.timeMs);

	{
		std::lock_guard<std::mutex> lock(owner.m_impl->bookmarkState.mutex);
		auto & bookmarks = owner.m_impl->bookmarkState.entries[currentPath];
		const auto duplicate = std::find_if(
			bookmarks.begin(),
			bookmarks.end(),
			[&bookmark](const ofxVlc4::BookmarkInfo & existing) { return existing.id == bookmark.id; });
		if (duplicate != bookmarks.end()) {
			duplicate->label = bookmark.label;
		} else {
			bookmarks.push_back(bookmark);
			std::sort(
				bookmarks.begin(),
				bookmarks.end(),
				[](const ofxVlc4::BookmarkInfo & a, const ofxVlc4::BookmarkInfo & b) { return a.timeMs < b.timeMs; });
		}
	}

	owner.setStatus("Bookmark added.");
	return true;
}

bool MediaLibrary::addCurrentBookmark(const std::string & label) {
	return addBookmarkAtTime(owner.getTime(), label);
}

bool MediaLibrary::seekToBookmark(const std::string & bookmarkId) {
	const std::string currentPath = getCurrentPath();
	if (currentPath.empty()) {
		return false;
	}

	int bookmarkTimeMs = -1;
	{
		std::lock_guard<std::mutex> lock(owner.m_impl->bookmarkState.mutex);
		const auto it = owner.m_impl->bookmarkState.entries.find(currentPath);
		if (it == owner.m_impl->bookmarkState.entries.end()) {
			return false;
		}

		const auto bookmarkIt = std::find_if(
			it->second.begin(),
			it->second.end(),
			[&bookmarkId](const ofxVlc4::BookmarkInfo & bookmark) { return bookmark.id == bookmarkId; });
		if (bookmarkIt == it->second.end()) {
			return false;
		}
		bookmarkTimeMs = bookmarkIt->timeMs;
	}

	owner.setTime(bookmarkTimeMs);
	owner.setStatus("Jumped to bookmark.");
	return true;
}

bool MediaLibrary::removeBookmark(const std::string & bookmarkId) {
	const std::string currentPath = getCurrentPath();
	if (currentPath.empty()) {
		return false;
	}

	std::lock_guard<std::mutex> lock(owner.m_impl->bookmarkState.mutex);
	auto it = owner.m_impl->bookmarkState.entries.find(currentPath);
	if (it == owner.m_impl->bookmarkState.entries.end()) {
		return false;
	}

	auto newEnd = std::remove_if(
		it->second.begin(),
		it->second.end(),
		[&bookmarkId](const ofxVlc4::BookmarkInfo & bookmark) { return bookmark.id == bookmarkId; });
	if (newEnd == it->second.end()) {
		return false;
	}

	it->second.erase(newEnd, it->second.end());
	if (it->second.empty()) {
		owner.m_impl->bookmarkState.entries.erase(it);
	}

	owner.setStatus("Bookmark removed.");
	return true;
}

void MediaLibrary::clearBookmarksForPath(const std::string & path) {
	const std::string trimmedPath = trimWhitespace(path);
	if (trimmedPath.empty()) {
		return;
	}

	std::lock_guard<std::mutex> lock(owner.m_impl->bookmarkState.mutex);
	owner.m_impl->bookmarkState.entries.erase(trimmedPath);
}

void MediaLibrary::clearCurrentBookmarks() {
	clearBookmarksForPath(getCurrentPath());
	owner.setStatus("Bookmarks cleared.");
}

bool MediaLibrary::savePlaylistM3U(const std::string & filePath) const {
	if (filePath.empty()) {
		return false;
	}
	const std::vector<std::string> paths = getPlaylist();
	if (paths.empty()) {
		return false;
	}
	const std::string content = ofxVlc4PlaylistHelpers::serializeM3U(paths);
	std::ofstream file(filePath, std::ios::out | std::ios::trunc);
	if (!file.is_open()) {
		return false;
	}
	file << content;
	return file.good();
}

bool MediaLibrary::savePlaylistXSPF(const std::string & filePath, const std::string & title) const {
	if (filePath.empty()) {
		return false;
	}
	const std::vector<std::string> paths = getPlaylist();
	if (paths.empty()) {
		return false;
	}
	const std::string content = ofxVlc4PlaylistHelpers::serializeXSPF(paths, title);
	std::ofstream file(filePath, std::ios::out | std::ios::trunc);
	if (!file.is_open()) {
		return false;
	}
	file << content;
	return file.good();
}

std::vector<std::string> MediaLibrary::loadPlaylistM3U(const std::string & filePath) const {
	if (filePath.empty()) {
		return {};
	}
	std::ifstream file(filePath, std::ios::in | std::ios::binary);
	if (!file.is_open()) {
		return {};
	}
	std::ostringstream contents;
	contents << file.rdbuf();
	return ofxVlc4PlaylistHelpers::deserializeM3U(contents.str());
}

std::vector<std::string> MediaLibrary::loadPlaylistXSPF(const std::string & filePath) const {
	if (filePath.empty()) {
		return {};
	}
	std::ifstream file(filePath, std::ios::in | std::ios::binary);
	if (!file.is_open()) {
		return {};
	}
	std::ostringstream contents;
	contents << file.rdbuf();
	return ofxVlc4PlaylistHelpers::deserializeXSPF(contents.str());
}
