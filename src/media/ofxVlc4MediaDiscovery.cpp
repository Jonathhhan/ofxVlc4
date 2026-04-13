#include "ofxVlc4.h"
#include "ofxVlc4Impl.h"
#include "audio/ofxVlc4Audio.h"
#include "media/MediaLibrary.h"
#include "ofxVlc4Media.h"
#include "playback/PlaybackController.h"
#include "video/ofxVlc4Video.h"
#include "support/ofxVlc4Utils.h"
#include "core/VlcCoreSession.h"
#include "core/VlcEventRouter.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <set>
#include <sstream>

using ofxVlc4Utils::clearAllocatedFbo;
using ofxVlc4Utils::fallbackIndexedLabel;
using ofxVlc4Utils::formatProgramName;
using ofxVlc4Utils::isUri;
using ofxVlc4Utils::isStoppedOrIdleState;
using ofxVlc4Utils::isTransientPlaybackState;
using ofxVlc4Utils::mediaLabelForPath;
using ofxVlc4Utils::normalizeOptionalPath;
using ofxVlc4Utils::sanitizeFileStem;
using ofxVlc4Utils::trimWhitespace;

namespace {

std::string rendererDisplayLabel(const ofxVlc4::RendererInfo & renderer) {
	std::string label = trimWhitespace(renderer.name);
	if (label.empty()) {
		label = "Renderer";
	}

	const std::string type = trimWhitespace(renderer.type);
	if (!type.empty()) {
		label += " (" + type + ")";
	}

	return label;
}

std::string rendererStableId(
	const std::string & discovererName,
	const std::string & rendererName,
	const std::string & rendererType,
	const std::string & iconUri,
	int flags) {
	return discovererName + "|" + rendererType + "|" + rendererName + "|" + iconUri + "|" + ofToString(flags);
}

libvlc_media_discoverer_category_t toLibvlcMediaDiscovererCategory(ofxVlc4::MediaDiscovererCategory category) {
	switch (category) {
	case ofxVlc4::MediaDiscovererCategory::Devices:
		return libvlc_media_discoverer_devices;
	case ofxVlc4::MediaDiscovererCategory::Podcasts:
		return libvlc_media_discoverer_podcasts;
	case ofxVlc4::MediaDiscovererCategory::LocalDirs:
		return libvlc_media_discoverer_localdirs;
	case ofxVlc4::MediaDiscovererCategory::Lan:
	default:
		return libvlc_media_discoverer_lan;
	}
}

ofxVlc4::MediaDiscovererCategory toMediaDiscovererCategory(libvlc_media_discoverer_category_t category) {
	switch (category) {
	case libvlc_media_discoverer_devices:
		return ofxVlc4::MediaDiscovererCategory::Devices;
	case libvlc_media_discoverer_podcasts:
		return ofxVlc4::MediaDiscovererCategory::Podcasts;
	case libvlc_media_discoverer_localdirs:
		return ofxVlc4::MediaDiscovererCategory::LocalDirs;
	case libvlc_media_discoverer_lan:
	default:
		return ofxVlc4::MediaDiscovererCategory::Lan;
	}
}

std::string mediaDisplayName(libvlc_media_t * media) {
	if (!media) {
		return "";
	}

	char * rawTitle = libvlc_media_get_meta(media, libvlc_meta_Title);
	if (rawTitle) {
		const std::string title = trimWhitespace(rawTitle);
		libvlc_free(rawTitle);
		if (!title.empty()) {
			return title;
		}
	}

	char * rawMrl = libvlc_media_get_mrl(media);
	if (!rawMrl) {
		return "";
	}

	const std::string mrl = trimWhitespace(rawMrl);
	libvlc_free(rawMrl);
	return mediaLabelForPath(mrl);
}

std::string mediaDiscovererDisplayLabel(const ofxVlc4::MediaDiscovererInfo & discoverer) {
	const std::string longName = trimWhitespace(discoverer.longName);
	if (!longName.empty()) {
		return longName;
	}

	const std::string name = trimWhitespace(discoverer.name);
	if (!name.empty()) {
		return name;
	}

	return "Discoverer";
}

}

std::vector<ofxVlc4::MediaDiscovererInfo> ofxVlc4::MediaComponent::getMediaDiscoverers(MediaDiscovererCategory category) const {
	std::vector<MediaDiscovererInfo> discoverers;
	libvlc_instance_t * instance = owner.sessionInstance();
	if (!instance) {
		return discoverers;
	}

	std::string currentDiscovererName;
	{
		std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.mediaDiscovererMutex);
		currentDiscovererName = owner.m_impl->mediaDiscoveryRuntime.discovererName;
	}

	libvlc_media_discoverer_description_t ** services = nullptr;
	const size_t serviceCount = libvlc_media_discoverer_list_get(
		instance,
		toLibvlcMediaDiscovererCategory(category),
		&services);
	if (serviceCount == 0 || !services) {
		return discoverers;
	}

	discoverers.reserve(serviceCount);
	for (size_t serviceIndex = 0; serviceIndex < serviceCount; ++serviceIndex) {
		const libvlc_media_discoverer_description_t * service = services[serviceIndex];
		if (!service) {
			continue;
		}

		MediaDiscovererInfo info;
		info.name = service->psz_name ? service->psz_name : "";
		info.longName = trimWhitespace(service->psz_longname ? service->psz_longname : "");
		info.category = toMediaDiscovererCategory(service->i_cat);
		info.current = info.name == currentDiscovererName;
		discoverers.push_back(std::move(info));
	}

	libvlc_media_discoverer_list_release(services, serviceCount);
	return discoverers;
}

std::string ofxVlc4::MediaComponent::getSelectedMediaDiscovererName() const {
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.mediaDiscovererMutex);
	return owner.m_impl->mediaDiscoveryRuntime.discovererName;
}

ofxVlc4::MediaDiscoveryStateInfo ofxVlc4::MediaComponent::getMediaDiscoveryState() const {
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.mediaDiscovererMutex);
	return buildMediaDiscoveryStateInfoLocked();
}

std::vector<ofxVlc4::DiscoveredMediaItemInfo> ofxVlc4::MediaComponent::getDiscoveredMediaItems() const {
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.mediaDiscovererMutex);
	return owner.m_impl->mediaDiscoveryRuntime.discoveredItems;
}

void ofxVlc4::MediaComponent::stopMediaDiscoveryInternal() {
	if (owner.m_impl->subsystemRuntime.coreSession->mediaDiscovererListEvents()) {
		auto * eventRouter = owner.m_impl->subsystemRuntime.eventRouter.get();
		owner.m_impl->subsystemRuntime.coreSession->detachMediaDiscovererListEvents(
			eventRouter ? static_cast<void *>(eventRouter) : static_cast<void *>(owner.m_controlBlock.get()),
			eventRouter ? VlcEventRouter::mediaDiscovererMediaListEventStatic : ofxVlc4::mediaDiscovererMediaListEventStatic);
		owner.m_impl->subsystemRuntime.coreSession->setMediaDiscovererListEvents(nullptr);
	}

	if (owner.m_impl->subsystemRuntime.coreSession->mediaDiscovererList()) {
		libvlc_media_list_release(owner.m_impl->subsystemRuntime.coreSession->mediaDiscovererList());
		owner.m_impl->subsystemRuntime.coreSession->setMediaDiscovererList(nullptr);
	}

	if (owner.m_impl->subsystemRuntime.coreSession->mediaDiscoverer()) {
		libvlc_media_discoverer_stop(owner.m_impl->subsystemRuntime.coreSession->mediaDiscoverer());
		libvlc_media_discoverer_release(owner.m_impl->subsystemRuntime.coreSession->mediaDiscoverer());
		owner.m_impl->subsystemRuntime.coreSession->setMediaDiscoverer(nullptr);
	}

	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.mediaDiscovererMutex);
	clearMediaDiscoveryStateLocked();
}

void ofxVlc4::MediaComponent::refreshDiscoveredMediaItems() {
	std::vector<DiscoveredMediaItemInfo> refreshedItems;
	std::set<std::string> seenKeys;
	if (owner.m_impl->subsystemRuntime.coreSession->mediaDiscovererList()) {
		libvlc_media_list_lock(owner.m_impl->subsystemRuntime.coreSession->mediaDiscovererList());
		const int itemCount = libvlc_media_list_count(owner.m_impl->subsystemRuntime.coreSession->mediaDiscovererList());
		if (itemCount > 0) {
			refreshedItems.reserve(static_cast<size_t>(itemCount));
		}

		for (int itemIndex = 0; itemIndex < itemCount; ++itemIndex) {
			libvlc_media_t * item = libvlc_media_list_item_at_index(owner.m_impl->subsystemRuntime.coreSession->mediaDiscovererList(), itemIndex);
			if (!item) {
				continue;
			}

			DiscoveredMediaItemInfo info;
			char * rawMrl = libvlc_media_get_mrl(item);
			info.mrl = rawMrl ? trimWhitespace(rawMrl) : "";
			if (rawMrl) {
				libvlc_free(rawMrl);
			}
			info.name = mediaDisplayName(item);
			info.isDirectory = libvlc_media_get_type(item) == libvlc_media_type_directory;
			if (info.name.empty()) {
				info.name = info.mrl.empty() ? ("Item " + ofToString(itemIndex + 1)) : mediaLabelForPath(info.mrl);
			}

			const std::string dedupeKey = !info.mrl.empty()
				? info.mrl
				: (info.name + "|" + (info.isDirectory ? "dir" : "file"));
			if (!dedupeKey.empty() && !seenKeys.insert(dedupeKey).second) {
				libvlc_media_release(item);
				continue;
			}

			refreshedItems.push_back(std::move(info));
			libvlc_media_release(item);
		}
		libvlc_media_list_unlock(owner.m_impl->subsystemRuntime.coreSession->mediaDiscovererList());
	}

	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.mediaDiscovererMutex);
	setDiscoveredMediaItemsLocked(std::move(refreshedItems));
}

bool ofxVlc4::MediaComponent::startMediaDiscovery(const std::string & discovererName) {
	const std::string trimmedName = trimWhitespace(discovererName);
	if (trimmedName.empty()) {
		stopMediaDiscovery();
		return true;
	}

	libvlc_instance_t * instance = owner.sessionInstance();
	if (!instance) {
		owner.setError("Initialize libvlc before starting media discovery.");
		return false;
	}

	if (owner.m_impl->subsystemRuntime.coreSession->mediaDiscoverer() && owner.m_impl->mediaDiscoveryRuntime.discovererName == trimmedName) {
		return true;
	}

	stopMediaDiscoveryInternal();

	owner.m_impl->subsystemRuntime.coreSession->setMediaDiscoverer(libvlc_media_discoverer_new(instance, trimmedName.c_str()));
	if (!owner.m_impl->subsystemRuntime.coreSession->mediaDiscoverer()) {
		owner.setError("Media discovery could not be created.");
		return false;
	}

	owner.m_impl->subsystemRuntime.coreSession->setMediaDiscovererList(libvlc_media_discoverer_media_list(owner.m_impl->subsystemRuntime.coreSession->mediaDiscoverer()));
	if (!owner.m_impl->subsystemRuntime.coreSession->mediaDiscovererList()) {
		stopMediaDiscoveryInternal();
		owner.setError("Media discovery list is unavailable.");
		return false;
	}

	owner.m_impl->subsystemRuntime.coreSession->setMediaDiscovererListEvents(libvlc_media_list_event_manager(owner.m_impl->subsystemRuntime.coreSession->mediaDiscovererList()));
	if (owner.m_impl->subsystemRuntime.coreSession->mediaDiscovererListEvents()) {
		auto * eventRouter = owner.m_impl->subsystemRuntime.eventRouter.get();
		owner.m_impl->subsystemRuntime.coreSession->attachMediaDiscovererListEvents(
			eventRouter ? static_cast<void *>(eventRouter) : static_cast<void *>(owner.m_controlBlock.get()),
			eventRouter ? VlcEventRouter::mediaDiscovererMediaListEventStatic : ofxVlc4::mediaDiscovererMediaListEventStatic);
	}

	if (libvlc_media_discoverer_start(owner.m_impl->subsystemRuntime.coreSession->mediaDiscoverer()) != 0) {
		stopMediaDiscoveryInternal();
		owner.setError("Media discovery could not be started.");
		return false;
	}

	std::string discovererLongName;
	MediaDiscovererCategory discovererCategory = MediaDiscovererCategory::Lan;
	const auto allCategories = {
		MediaDiscovererCategory::Devices,
		MediaDiscovererCategory::Lan,
		MediaDiscovererCategory::Podcasts,
		MediaDiscovererCategory::LocalDirs
	};
	for (const MediaDiscovererCategory category : allCategories) {
		const std::vector<MediaDiscovererInfo> discoverers = getMediaDiscoverers(category);
		const auto it = std::find_if(
			discoverers.begin(),
			discoverers.end(),
			[&trimmedName](const MediaDiscovererInfo & info) { return info.name == trimmedName; });
		if (it != discoverers.end()) {
			discovererLongName = mediaDiscovererDisplayLabel(*it);
			discovererCategory = it->category;
			break;
		}
	}
	{
		std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.mediaDiscovererMutex);
		setMediaDiscoveryDescriptorLocked(trimmedName, discovererLongName, discovererCategory, false);
	}
	refreshDiscoveredMediaItems();
	owner.setStatus("Media discovery started.");
	const std::string discoveryLabel = discovererLongName.empty()
		? trimmedName
		: (discovererLongName + " (" + trimmedName + ")");
	owner.logNotice("Media discovery started: " + discoveryLabel + ".");
	return true;
}

void ofxVlc4::MediaComponent::stopMediaDiscovery() {
	if (!owner.m_impl->subsystemRuntime.coreSession->mediaDiscoverer() && owner.m_impl->mediaDiscoveryRuntime.discovererName.empty()) {
		return;
	}

	stopMediaDiscoveryInternal();
	owner.setStatus("Media discovery stopped.");
	owner.logNotice("Media discovery stopped.");
}

bool ofxVlc4::MediaComponent::isMediaDiscoveryActive() const {
	return owner.m_impl->subsystemRuntime.coreSession->mediaDiscoverer() != nullptr;
}

bool ofxVlc4::MediaComponent::addDiscoveredMediaItemToPlaylist(int index) {
	const std::vector<DiscoveredMediaItemInfo> items = getDiscoveredMediaItems();
	if (index < 0 || index >= static_cast<int>(items.size())) {
		return false;
	}

	const auto & item = items[static_cast<size_t>(index)];
	if (item.mrl.empty()) {
		return false;
	}

	addToPlaylist(item.mrl);
	return true;
}

bool ofxVlc4::MediaComponent::playDiscoveredMediaItem(int index) {
	const int previousCount = static_cast<int>(mediaLibrary().getPlaylistSize());
	if (!addDiscoveredMediaItemToPlaylist(index)) {
		return false;
	}

	const int newIndex = static_cast<int>(mediaLibrary().getPlaylistSize()) - 1;
	if (newIndex < previousCount || newIndex < 0) {
		return false;
	}

	playback().playIndex(newIndex);
	return true;
}

int ofxVlc4::MediaComponent::addAllDiscoveredMediaItemsToPlaylist() {
	const std::vector<DiscoveredMediaItemInfo> items = getDiscoveredMediaItems();
	int addedCount = 0;
	for (const auto & item : items) {
		if (item.mrl.empty()) {
			continue;
		}
		addToPlaylist(item.mrl);
		++addedCount;
	}
	return addedCount;
}


void ofxVlc4::MediaComponent::mediaDiscovererMediaListEvent(const libvlc_event_t * event) {
	if (!event) {
		return;
	}

	switch (event->type) {
	case libvlc_MediaListItemAdded:
	case libvlc_MediaListItemDeleted:
		{
			std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.mediaDiscovererMutex);
			setMediaDiscoveryEndReachedLocked(false);
		}
		refreshDiscoveredMediaItems();
		break;
	case libvlc_MediaListEndReached:
		{
			std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.mediaDiscovererMutex);
			setMediaDiscoveryEndReachedLocked(true);
		}
		refreshDiscoveredMediaItems();
		break;
	default:
		break;
	}
}

void ofxVlc4::MediaComponent::clearRendererItems() {
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.rendererMutex);
	for (auto & renderer : owner.m_impl->rendererDiscoveryRuntime.discoveredRenderers) {
		if (renderer.item) {
			libvlc_renderer_item_release(renderer.item);
			renderer.item = nullptr;
		}
	}
	owner.m_impl->rendererDiscoveryRuntime.discoveredRenderers.clear();
}

void ofxVlc4::MediaComponent::stopRendererDiscoveryInternal() {
	if (owner.m_impl->subsystemRuntime.coreSession->rendererDiscovererEvents()) {
		auto * eventRouter = owner.m_impl->subsystemRuntime.eventRouter.get();
		owner.m_impl->subsystemRuntime.coreSession->detachRendererEvents(
			eventRouter ? static_cast<void *>(eventRouter) : static_cast<void *>(owner.m_controlBlock.get()),
			eventRouter ? VlcEventRouter::rendererDiscovererEventStatic : ofxVlc4::rendererDiscovererEventStatic);
		owner.m_impl->subsystemRuntime.coreSession->setRendererDiscovererEvents(nullptr);
	}

	if (owner.m_impl->subsystemRuntime.coreSession->rendererDiscoverer()) {
		libvlc_renderer_discoverer_stop(owner.m_impl->subsystemRuntime.coreSession->rendererDiscoverer());
		libvlc_renderer_discoverer_release(owner.m_impl->subsystemRuntime.coreSession->rendererDiscoverer());
		owner.m_impl->subsystemRuntime.coreSession->setRendererDiscoverer(nullptr);
	}

	clearRendererItems();
}

void ofxVlc4::stopMediaDiscoveryInternal() {
	m_impl->subsystemRuntime.mediaComponent->stopMediaDiscoveryInternal();
}

void ofxVlc4::clearRendererItems() {
	m_impl->subsystemRuntime.mediaComponent->clearRendererItems();
}

void ofxVlc4::stopRendererDiscoveryInternal() {
	m_impl->subsystemRuntime.mediaComponent->stopRendererDiscoveryInternal();
}

void ofxVlc4::refreshDiscoveredMediaItems() {
	m_impl->subsystemRuntime.mediaComponent->refreshDiscoveredMediaItems();
}

bool ofxVlc4::MediaComponent::applySelectedRenderer() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return false;
	}

	libvlc_renderer_item_t * rendererItem = nullptr;
	libvlc_renderer_item_t * heldRendererItem = nullptr;
	std::string rendererLabel = "local";
	if (!owner.m_impl->rendererDiscoveryRuntime.selectedRendererId.empty()) {
		std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.rendererMutex);
		const RendererItemEntry * rendererEntry = findRendererEntryByIdLocked(owner.m_impl->rendererDiscoveryRuntime.selectedRendererId);
		if (!rendererEntry || !rendererEntry->item) {
			owner.logVerbose("Selected renderer is not currently available; using local output until it appears.");
			rendererLabel = "local";
		} else {
			heldRendererItem = libvlc_renderer_item_hold(rendererEntry->item);
			rendererItem = heldRendererItem;
			rendererLabel = rendererEntry->name.empty() ? rendererEntry->id : rendererEntry->name;
		}
	}

	const int result = libvlc_media_player_set_renderer(player, rendererItem);
	if (heldRendererItem) {
		libvlc_renderer_item_release(heldRendererItem);
	}

	if (result != 0) {
		refreshRendererStateInfo();
		owner.logWarning("Renderer could not be applied: " + rendererLabel + ".");
		return false;
	}

	refreshRendererStateInfo();
	owner.logVerbose("Renderer applied: " + rendererLabel + ".");
	return true;
}

bool ofxVlc4::applySelectedRenderer() {
	return m_impl->subsystemRuntime.mediaComponent->applySelectedRenderer();
}

std::vector<ofxVlc4::MediaDiscovererInfo> ofxVlc4::getMediaDiscoverers(MediaDiscovererCategory category) const {
	return m_impl->subsystemRuntime.mediaComponent->getMediaDiscoverers(category);
}

std::string ofxVlc4::getSelectedMediaDiscovererName() const {
	return m_impl->subsystemRuntime.mediaComponent->getSelectedMediaDiscovererName();
}

ofxVlc4::MediaDiscoveryStateInfo ofxVlc4::getMediaDiscoveryState() const {
	return m_impl->subsystemRuntime.mediaComponent->getMediaDiscoveryState();
}

bool ofxVlc4::startMediaDiscovery(const std::string & discovererName) {
	return m_impl->subsystemRuntime.mediaComponent->startMediaDiscovery(discovererName);
}

void ofxVlc4::stopMediaDiscovery() {
	m_impl->subsystemRuntime.mediaComponent->stopMediaDiscovery();
}

bool ofxVlc4::isMediaDiscoveryActive() const {
	return m_impl->subsystemRuntime.mediaComponent->isMediaDiscoveryActive();
}

std::vector<ofxVlc4::DiscoveredMediaItemInfo> ofxVlc4::getDiscoveredMediaItems() const {
	return m_impl->subsystemRuntime.mediaComponent->getDiscoveredMediaItems();
}

bool ofxVlc4::addDiscoveredMediaItemToPlaylist(int index) {
	return m_impl->subsystemRuntime.mediaComponent->addDiscoveredMediaItemToPlaylist(index);
}

bool ofxVlc4::playDiscoveredMediaItem(int index) {
	return m_impl->subsystemRuntime.mediaComponent->playDiscoveredMediaItem(index);
}

int ofxVlc4::addAllDiscoveredMediaItemsToPlaylist() {
	return m_impl->subsystemRuntime.mediaComponent->addAllDiscoveredMediaItemsToPlaylist();
}

std::vector<ofxVlc4::RendererDiscovererInfo> ofxVlc4::getRendererDiscoverers() const {
	return m_impl->subsystemRuntime.mediaComponent->getRendererDiscoverers();
}

std::vector<ofxVlc4::RendererDiscovererInfo> ofxVlc4::MediaComponent::getRendererDiscoverers() const {
	std::vector<RendererDiscovererInfo> discoverers;
	libvlc_instance_t * instance = owner.sessionInstance();
	if (!instance) {
		return discoverers;
	}

	libvlc_rd_description_t ** services = nullptr;
	const size_t serviceCount = libvlc_renderer_discoverer_list_get(instance, &services);
	if (serviceCount == 0 || !services) {
		return discoverers;
	}

	discoverers.reserve(serviceCount);
	for (size_t serviceIndex = 0; serviceIndex < serviceCount; ++serviceIndex) {
		const libvlc_rd_description_t * service = services[serviceIndex];
		if (!service) {
			continue;
		}

		RendererDiscovererInfo info;
		info.name = service->psz_name ? service->psz_name : "";
		info.longName = trimWhitespace(service->psz_longname ? service->psz_longname : "");
		discoverers.push_back(std::move(info));
	}

	libvlc_renderer_discoverer_list_release(services, serviceCount);
	return discoverers;
}

std::string ofxVlc4::getSelectedRendererDiscovererName() const {
	return m_impl->subsystemRuntime.mediaComponent->getSelectedRendererDiscovererName();
}

std::string ofxVlc4::MediaComponent::getSelectedRendererDiscovererName() const {
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.rendererMutex);
	return owner.m_impl->rendererDiscoveryRuntime.discovererName;
}

void ofxVlc4::resetRendererStateInfo() {
	m_impl->subsystemRuntime.mediaComponent->resetRendererStateInfo();
}

void ofxVlc4::MediaComponent::resetRendererStateInfo() {
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.rendererMutex);
	owner.m_impl->rendererDiscoveryRuntime.stateInfo = buildRendererStateInfoLocked();
}

void ofxVlc4::refreshRendererStateInfo() {
	m_impl->subsystemRuntime.mediaComponent->refreshRendererStateInfo();
}

ofxVlc4::MediaDiscoveryStateInfo ofxVlc4::MediaComponent::buildMediaDiscoveryStateInfoLocked() const {
	MediaDiscoveryStateInfo state;
	state.discovererName = owner.m_impl->mediaDiscoveryRuntime.discovererName;
	state.discovererLongName = owner.m_impl->mediaDiscoveryRuntime.discovererLongName;
	state.category = owner.m_impl->mediaDiscoveryRuntime.category;
	state.active = !state.discovererName.empty();
	state.endReached = owner.m_impl->mediaDiscoveryRuntime.endReached;
	state.itemCount = owner.m_impl->mediaDiscoveryRuntime.discoveredItems.size();
	state.directoryCount = static_cast<size_t>(std::count_if(
		owner.m_impl->mediaDiscoveryRuntime.discoveredItems.begin(),
		owner.m_impl->mediaDiscoveryRuntime.discoveredItems.end(),
		[](const DiscoveredMediaItemInfo & item) { return item.isDirectory; }));
	return state;
}

void ofxVlc4::MediaComponent::clearMediaDiscoveryStateLocked() {
	owner.m_impl->mediaDiscoveryRuntime.discoveredItems.clear();
	owner.m_impl->mediaDiscoveryRuntime.discovererName.clear();
	owner.m_impl->mediaDiscoveryRuntime.discovererLongName.clear();
	owner.m_impl->mediaDiscoveryRuntime.category = MediaDiscovererCategory::Lan;
	owner.m_impl->mediaDiscoveryRuntime.endReached = false;
}

void ofxVlc4::MediaComponent::setMediaDiscoveryDescriptorLocked(
	const std::string & name,
	const std::string & longName,
	MediaDiscovererCategory category,
	bool endReached) {
	owner.m_impl->mediaDiscoveryRuntime.discovererName = name;
	owner.m_impl->mediaDiscoveryRuntime.discovererLongName = longName;
	owner.m_impl->mediaDiscoveryRuntime.category = category;
	owner.m_impl->mediaDiscoveryRuntime.endReached = endReached;
}

void ofxVlc4::MediaComponent::setMediaDiscoveryEndReachedLocked(bool endReached) {
	owner.m_impl->mediaDiscoveryRuntime.endReached = endReached;
}

void ofxVlc4::MediaComponent::setDiscoveredMediaItemsLocked(std::vector<DiscoveredMediaItemInfo> items) {
	owner.m_impl->mediaDiscoveryRuntime.discoveredItems = std::move(items);
}

const ofxVlc4::RendererItemEntry * ofxVlc4::MediaComponent::findRendererEntryByIdLocked(
	const std::string & rendererId) const {
	if (rendererId.empty()) {
		return nullptr;
	}

	const auto it = std::find_if(
		owner.m_impl->rendererDiscoveryRuntime.discoveredRenderers.begin(),
		owner.m_impl->rendererDiscoveryRuntime.discoveredRenderers.end(),
		[&rendererId](const RendererItemEntry & entry) { return entry.id == rendererId; });
	return it != owner.m_impl->rendererDiscoveryRuntime.discoveredRenderers.end() ? &(*it) : nullptr;
}

ofxVlc4::RendererStateInfo ofxVlc4::MediaComponent::buildRendererStateInfoLocked() const {
	RendererStateInfo info;
	info.discoveryActive = owner.m_impl->subsystemRuntime.coreSession->rendererDiscoverer() != nullptr;
	info.discovererName = owner.m_impl->rendererDiscoveryRuntime.discovererName;
	info.discoveredRendererCount = owner.m_impl->rendererDiscoveryRuntime.discoveredRenderers.size();
	info.requestedRendererId = owner.m_impl->rendererDiscoveryRuntime.selectedRendererId;
	info.usingLocalFallback = true;

	if (!owner.m_impl->rendererDiscoveryRuntime.selectedRendererId.empty()) {
		info.selectedRendererKnown = true;
		info.selectedRenderer.id = owner.m_impl->rendererDiscoveryRuntime.selectedRendererId;
		info.selectedRenderer.name = owner.m_impl->rendererDiscoveryRuntime.selectedRendererId;
		info.selectedRenderer.selected = true;
		info.reconnectPending = true;

		if (const RendererItemEntry * selectedEntry = findRendererEntryByIdLocked(owner.m_impl->rendererDiscoveryRuntime.selectedRendererId)) {
			info.selectedRenderer = buildRendererInfoLocked(*selectedEntry, true);
			info.selectedRendererAvailable = true;
			info.usingLocalFallback = false;
			info.reconnectPending = false;
		}
	}

	return info;
}

ofxVlc4::RendererInfo ofxVlc4::MediaComponent::buildRendererInfoLocked(
	const RendererItemEntry & entry,
	bool selected) const {
	RendererInfo info;
	info.id = entry.id;
	info.name = entry.name;
	info.type = entry.type;
	info.iconUri = entry.iconUri;
	info.canAudio = entry.canAudio;
	info.canVideo = entry.canVideo;
	info.selected = selected;
	return info;
}

std::vector<ofxVlc4::RendererInfo> ofxVlc4::MediaComponent::buildDiscoveredRendererInfosLocked() const {
	std::vector<RendererInfo> renderers;
	renderers.reserve(owner.m_impl->rendererDiscoveryRuntime.discoveredRenderers.size());
	for (const RendererItemEntry & entry : owner.m_impl->rendererDiscoveryRuntime.discoveredRenderers) {
		renderers.push_back(buildRendererInfoLocked(
			entry,
			!owner.m_impl->rendererDiscoveryRuntime.selectedRendererId.empty() && entry.id == owner.m_impl->rendererDiscoveryRuntime.selectedRendererId));
	}
	return renderers;
}

bool ofxVlc4::MediaComponent::canApplyRendererImmediately() const {
	libvlc_media_player_t * player = owner.sessionPlayer();
	return player && isStoppedOrIdleState(libvlc_media_player_get_state(player));
}

void ofxVlc4::MediaComponent::clearSelectedRendererLocked() {
	owner.m_impl->rendererDiscoveryRuntime.selectedRendererId.clear();
}

void ofxVlc4::MediaComponent::setSelectedRendererIdLocked(const std::string & rendererId) {
	owner.m_impl->rendererDiscoveryRuntime.selectedRendererId = rendererId;
}

void ofxVlc4::MediaComponent::setRendererDiscovererNameLocked(const std::string & discovererName) {
	owner.m_impl->rendererDiscoveryRuntime.discovererName = discovererName;
}

ofxVlc4::SubtitleStateInfo ofxVlc4::MediaComponent::getCachedSubtitleStateInfo() const {
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.subtitleStateMutex);
	return owner.m_impl->stateCacheRuntime.subtitle;
}

ofxVlc4::NavigationStateInfo ofxVlc4::MediaComponent::getCachedNavigationStateInfo() const {
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.navigationStateMutex);
	return owner.m_impl->stateCacheRuntime.navigation;
}

void ofxVlc4::MediaComponent::refreshRendererStateInfo() {
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.rendererMutex);
	owner.m_impl->rendererDiscoveryRuntime.stateInfo = buildRendererStateInfoLocked();
}

ofxVlc4::RendererStateInfo ofxVlc4::getRendererStateInfo() const {
	if (!m_impl || !m_impl->subsystemRuntime.mediaComponent) return {};
	return m_impl->subsystemRuntime.mediaComponent->getRendererStateInfo();
}

ofxVlc4::RendererStateInfo ofxVlc4::MediaComponent::getRendererStateInfo() const {
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.rendererMutex);
	return buildRendererStateInfoLocked();
}

bool ofxVlc4::startRendererDiscovery(const std::string & discovererName) {
	return m_impl->subsystemRuntime.mediaComponent->startRendererDiscovery(discovererName);
}

bool ofxVlc4::MediaComponent::startRendererDiscovery(const std::string & discovererName) {
	const std::string trimmedName = trimWhitespace(discovererName);
	if (trimmedName.empty()) {
		stopRendererDiscovery();
		return true;
	}

	libvlc_instance_t * instance = owner.sessionInstance();
	if (!instance) {
		owner.setError("Initialize libvlc before starting renderer discovery.");
		return false;
	}

	if (owner.m_impl->subsystemRuntime.coreSession->rendererDiscoverer() && getSelectedRendererDiscovererName() == trimmedName) {
		refreshRendererStateInfo();
		return true;
	}

	if (getSelectedRendererDiscovererName() != trimmedName) {
		{
			std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.rendererMutex);
			clearSelectedRendererLocked();
		}
		if (canApplyRendererImmediately()) {
			applySelectedRenderer();
		}
	}

	stopRendererDiscoveryInternal();

	owner.m_impl->subsystemRuntime.coreSession->setRendererDiscoverer(libvlc_renderer_discoverer_new(instance, trimmedName.c_str()));
	if (!owner.m_impl->subsystemRuntime.coreSession->rendererDiscoverer()) {
		owner.setError("Renderer discovery could not be created.");
		return false;
	}

	owner.m_impl->subsystemRuntime.coreSession->setRendererDiscovererEvents(libvlc_renderer_discoverer_event_manager(owner.m_impl->subsystemRuntime.coreSession->rendererDiscoverer()));
	if (owner.m_impl->subsystemRuntime.coreSession->rendererDiscovererEvents()) {
		auto * eventRouter = owner.m_impl->subsystemRuntime.eventRouter.get();
		owner.m_impl->subsystemRuntime.coreSession->attachRendererEvents(
			eventRouter ? static_cast<void *>(eventRouter) : static_cast<void *>(owner.m_controlBlock.get()),
			eventRouter ? VlcEventRouter::rendererDiscovererEventStatic : ofxVlc4::rendererDiscovererEventStatic);
	}

	if (libvlc_renderer_discoverer_start(owner.m_impl->subsystemRuntime.coreSession->rendererDiscoverer()) != 0) {
		stopRendererDiscoveryInternal();
		refreshRendererStateInfo();
		owner.setError("Renderer discovery could not be started.");
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.rendererMutex);
		setRendererDiscovererNameLocked(trimmedName);
	}
	refreshRendererStateInfo();
	owner.setStatus("Renderer discovery started.");
	owner.logNotice("Renderer discovery started: " + trimmedName + ".");
	return true;
}

void ofxVlc4::stopRendererDiscovery() {
	m_impl->subsystemRuntime.mediaComponent->stopRendererDiscovery();
}

void ofxVlc4::MediaComponent::stopRendererDiscovery() {
	if (!owner.m_impl->subsystemRuntime.coreSession->rendererDiscoverer() && getSelectedRendererDiscovererName().empty()) {
		return;
	}

	stopRendererDiscoveryInternal();
	{
		std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.rendererMutex);
		clearSelectedRendererLocked();
		setRendererDiscovererNameLocked("");
	}
	if (canApplyRendererImmediately()) {
		applySelectedRenderer();
	}
	refreshRendererStateInfo();
	owner.setStatus("Renderer discovery stopped.");
	owner.logNotice("Renderer discovery stopped.");
}

bool ofxVlc4::isRendererDiscoveryActive() const {
	return m_impl->subsystemRuntime.mediaComponent->isRendererDiscoveryActive();
}

std::vector<ofxVlc4::RendererInfo> ofxVlc4::getDiscoveredRenderers() const {
	return m_impl->subsystemRuntime.mediaComponent->getDiscoveredRenderers();
}

bool ofxVlc4::MediaComponent::isRendererDiscoveryActive() const {
	return owner.m_impl->subsystemRuntime.coreSession->rendererDiscoverer() != nullptr;
}

std::vector<ofxVlc4::RendererInfo> ofxVlc4::MediaComponent::getDiscoveredRenderers() const {
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.rendererMutex);
	return buildDiscoveredRendererInfosLocked();
}

void ofxVlc4::MediaComponent::handleRendererDiscovererEvent(const libvlc_event_t * event) {
	if (!event) {
		return;
	}

	if (event->type == libvlc_RendererDiscovererItemAdded) {
		bool shouldReconnectSelectedRenderer = false;
		std::string rendererLabel;
		if (!handleRendererItemAdded(
				event->u.renderer_discoverer_item_added.item,
				rendererLabel,
				shouldReconnectSelectedRenderer)) {
			return;
		}

		refreshRendererStateInfo();
		if (shouldReconnectSelectedRenderer && owner.sessionPlayer()) {
			applySelectedRenderer();
			refreshRendererStateInfo();
			owner.setStatus("Selected renderer is available again.");
			owner.logNotice("Renderer available again: " + rendererLabel + ".");
		}
		return;
	}

	if (event->type == libvlc_RendererDiscovererItemDeleted) {
		bool removedSelectedRenderer = false;
		if (!handleRendererItemDeleted(
				event->u.renderer_discoverer_item_deleted.item,
				removedSelectedRenderer)) {
			return;
		}

		refreshRendererStateInfo();
		if (removedSelectedRenderer && owner.sessionPlayer()) {
			applySelectedRenderer();
			refreshRendererStateInfo();
			owner.setStatus("Selected renderer unavailable; using local output until it returns.");
			owner.logNotice("Selected renderer became unavailable; using local output.");
		}
	}
}

bool ofxVlc4::MediaComponent::handleRendererItemAdded(
	libvlc_renderer_item_t * item,
	std::string & rendererLabel,
	bool & shouldReconnectSelectedRenderer) {
	rendererLabel.clear();
	shouldReconnectSelectedRenderer = false;
	if (!item) {
		return false;
	}

	libvlc_renderer_item_t * heldItem = libvlc_renderer_item_hold(item);
	if (!heldItem) {
		return false;
	}

	RendererItemEntry entry;
	entry.name = trimWhitespace(libvlc_renderer_item_name(heldItem) ? libvlc_renderer_item_name(heldItem) : "");
	entry.type = trimWhitespace(libvlc_renderer_item_type(heldItem) ? libvlc_renderer_item_type(heldItem) : "");
	entry.iconUri = trimWhitespace(libvlc_renderer_item_icon_uri(heldItem) ? libvlc_renderer_item_icon_uri(heldItem) : "");
	const int flags = libvlc_renderer_item_flags(heldItem);
	entry.canAudio = (flags & LIBVLC_RENDERER_CAN_AUDIO) != 0;
	entry.canVideo = (flags & LIBVLC_RENDERER_CAN_VIDEO) != 0;
	entry.id = rendererStableId(owner.m_impl->rendererDiscoveryRuntime.discovererName, entry.name, entry.type, entry.iconUri, flags);
	entry.item = heldItem;

	{
		std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.rendererMutex);
		if (findRendererEntryByIdLocked(entry.id)) {
			libvlc_renderer_item_release(heldItem);
			return false;
		}

		rendererLabel = entry.name.empty() ? entry.id : entry.name;
		shouldReconnectSelectedRenderer = !owner.m_impl->rendererDiscoveryRuntime.selectedRendererId.empty() && entry.id == owner.m_impl->rendererDiscoveryRuntime.selectedRendererId;
		owner.m_impl->rendererDiscoveryRuntime.discoveredRenderers.push_back(std::move(entry));
	}

	return true;
}

bool ofxVlc4::MediaComponent::handleRendererItemDeleted(
	libvlc_renderer_item_t * item,
	bool & removedSelectedRenderer) {
	removedSelectedRenderer = false;
	if (!item) {
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.rendererMutex);
		auto it = std::find_if(
			owner.m_impl->rendererDiscoveryRuntime.discoveredRenderers.begin(),
			owner.m_impl->rendererDiscoveryRuntime.discoveredRenderers.end(),
			[item](const RendererItemEntry & existing) { return existing.item == item; });
		if (it == owner.m_impl->rendererDiscoveryRuntime.discoveredRenderers.end()) {
			return false;
		}

		removedSelectedRenderer = !owner.m_impl->rendererDiscoveryRuntime.selectedRendererId.empty() && it->id == owner.m_impl->rendererDiscoveryRuntime.selectedRendererId;
		if (it->item) {
			libvlc_renderer_item_release(it->item);
		}
		owner.m_impl->rendererDiscoveryRuntime.discoveredRenderers.erase(it);
	}

	return true;
}

void ofxVlc4::mediaDiscovererMediaListEventStatic(const libvlc_event_t * event, void * data) {
	auto * cb = static_cast<ControlBlock *>(data);
	if (!cb || cb->expired.load(std::memory_order_acquire)) {
		return;
	}
	ofxVlc4 * owner = cb->owner;
	CallbackScope scope = owner->enterCallbackScope();
	if (!scope || !event) {
		return;
	}
	scope.get()->mediaDiscovererMediaListEvent(event);
}

void ofxVlc4::rendererDiscovererEventStatic(const libvlc_event_t * event, void * data) {
	auto * cb = static_cast<ControlBlock *>(data);
	if (!cb || cb->expired.load(std::memory_order_acquire)) {
		return;
	}
	ofxVlc4 * owner = cb->owner;
	CallbackScope scope = owner->enterCallbackScope();
	if (!scope || !event) {
		return;
	}
	scope.get()->rendererDiscovererEvent(event);
}

void ofxVlc4::mediaDiscovererMediaListEvent(const libvlc_event_t * event) {
	m_impl->subsystemRuntime.mediaComponent->mediaDiscovererMediaListEvent(event);
}

void ofxVlc4::rendererDiscovererEvent(const libvlc_event_t * event) {
	m_impl->subsystemRuntime.mediaComponent->handleRendererDiscovererEvent(event);
}

std::string ofxVlc4::getSelectedRendererId() const {
	return m_impl->subsystemRuntime.mediaComponent->getSelectedRendererId();
}

std::string ofxVlc4::MediaComponent::getSelectedRendererId() const {
	std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.rendererMutex);
	return owner.m_impl->rendererDiscoveryRuntime.selectedRendererId;
}

bool ofxVlc4::selectRenderer(const std::string & rendererId) {
	return m_impl->subsystemRuntime.mediaComponent->selectRenderer(rendererId);
}

bool ofxVlc4::MediaComponent::selectRenderer(const std::string & rendererId) {
	const std::string trimmedId = trimWhitespace(rendererId);
	if (trimmedId.empty()) {
		return clearRenderer();
	}

	RendererInfo selectedRenderer;
	bool foundRenderer = false;
	{
		std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.rendererMutex);
		if (const RendererItemEntry * selectedEntry = findRendererEntryByIdLocked(trimmedId)) {
			selectedRenderer = buildRendererInfoLocked(*selectedEntry, true);
			foundRenderer = true;
		}
	}

	if (!foundRenderer) {
		owner.setError("Selected renderer is not currently available.");
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.rendererMutex);
		setSelectedRendererIdLocked(trimmedId);
	}
	const bool canApplyImmediately = canApplyRendererImmediately();
	if (canApplyImmediately) {
		applySelectedRenderer();
	}
	refreshRendererStateInfo();

	owner.setStatus(canApplyImmediately ? "Renderer selected." : "Renderer will apply on next play.");
	owner.logNotice("Renderer selected: " + rendererDisplayLabel(selectedRenderer) + ".");
	return true;
}

bool ofxVlc4::clearRenderer() {
	return m_impl->subsystemRuntime.mediaComponent->clearRenderer();
}

bool ofxVlc4::MediaComponent::clearRenderer() {
	{
		std::lock_guard<std::mutex> lock(owner.m_impl->synchronizationRuntime.rendererMutex);
		clearSelectedRendererLocked();
	}
	const bool canApplyImmediately = canApplyRendererImmediately();
	if (canApplyImmediately) {
		applySelectedRenderer();
	}
	refreshRendererStateInfo();

	owner.setStatus(canApplyImmediately ? "Renderer reset to local output." : "Local output will apply on next play.");
	owner.logNotice("Renderer reset to local output.");
	return true;
}
