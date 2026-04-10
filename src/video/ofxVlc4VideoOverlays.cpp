#include "ofxVlc4.h"
#include "ofxVlc4Impl.h"
#include "ofxVlc4Video.h"
#include "media/ofxVlc4Media.h"
#include "playback/PlaybackController.h"
#include "support/ofxVlc4GlOps.h"
#include "support/ofxVlc4Utils.h"

#ifdef TARGET_WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <d3d11.h>
#include "GLFW/glfw3native.h"
#pragma comment(lib, "d3d11.lib")
#elif defined(TARGET_OSX)
#define GLFW_EXPOSE_NATIVE_COCOA
#include <objc/message.h>
#include <objc/runtime.h>
#include "GLFW/glfw3native.h"
#elif defined(TARGET_LINUX)
#define GLFW_EXPOSE_NATIVE_X11
#include "GLFW/glfw3native.h"
#endif

#include <algorithm>
#include <fstream>
#include <sstream>

using ofxVlc4Utils::clearAllocatedFbo;
using ofxVlc4Utils::joinFilterChainEntries;
using ofxVlc4Utils::normalizeOptionalPath;
using ofxVlc4Utils::parseFilterChainEntries;
using ofxVlc4Utils::readTextFileIfPresent;
using ofxVlc4Utils::setInputHandlingEnabled;
using ofxVlc4Utils::trimWhitespace;

using ofxVlc4GlOps::bindFbo;
using ofxVlc4GlOps::clientWaitFenceSync;
using ofxVlc4GlOps::deleteFbo;
using ofxVlc4GlOps::deleteFenceSync;
using ofxVlc4GlOps::flushCommands;
using ofxVlc4GlOps::gpuWaitFenceSync;
using ofxVlc4GlOps::insertFenceSync;
using ofxVlc4GlOps::setTextureLinearFiltering;
using ofxVlc4GlOps::setupFboWithTexture;
using ofxVlc4GlOps::unbindFbo;


namespace {
libvlc_position_t toLibvlcOverlayPosition(ofxVlc4::OverlayPosition position) {
	switch (position) {
	case ofxVlc4::OverlayPosition::Left:
		return libvlc_position_left;
	case ofxVlc4::OverlayPosition::Right:
		return libvlc_position_right;
	case ofxVlc4::OverlayPosition::Top:
		return libvlc_position_top;
	case ofxVlc4::OverlayPosition::TopLeft:
		return libvlc_position_top_left;
	case ofxVlc4::OverlayPosition::TopRight:
		return libvlc_position_top_right;
	case ofxVlc4::OverlayPosition::Bottom:
		return libvlc_position_bottom;
	case ofxVlc4::OverlayPosition::BottomLeft:
		return libvlc_position_bottom_left;
	case ofxVlc4::OverlayPosition::BottomRight:
		return libvlc_position_bottom_right;
	case ofxVlc4::OverlayPosition::Center:
	default:
		return libvlc_position_center;
	}
}

libvlc_position_t toLibvlcVideoTitlePosition(ofxVlc4::OverlayPosition position) {
	return toLibvlcOverlayPosition(position);
}

libvlc_teletext_key_t toLibvlcTeletextKey(ofxVlc4::TeletextKey key) {
	switch (key) {
	case ofxVlc4::TeletextKey::Green:
		return libvlc_teletext_key_green;
	case ofxVlc4::TeletextKey::Yellow:
		return libvlc_teletext_key_yellow;
	case ofxVlc4::TeletextKey::Blue:
		return libvlc_teletext_key_blue;
	case ofxVlc4::TeletextKey::Index:
		return libvlc_teletext_key_index;
	case ofxVlc4::TeletextKey::Red:
	default:
		return libvlc_teletext_key_red;
	}
}

int clampPackedRgbColor(int color) {
	return ofClamp(color, 0x000000, 0xFFFFFF);
}

}

void ofxVlc4::applyVlcFullscreen() {
	m_impl->subsystemRuntime.videoComponent->applyVlcFullscreen();
}

void ofxVlc4::VideoComponent::applyVlcFullscreen() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_set_fullscreen(player, owner.m_impl->videoPresentationRuntime.vlcFullscreenEnabled);
}

void ofxVlc4::VideoComponent::applyVideoInputHandling() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_video_set_key_input(player, owner.m_impl->playerConfigRuntime.keyInputEnabled ? 1u : 0u);
	libvlc_video_set_mouse_input(player, owner.m_impl->playerConfigRuntime.mouseInputEnabled ? 1u : 0u);
}

void ofxVlc4::VideoComponent::applyVideoTitleDisplay() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_media_player_set_video_title_display(
		player,
		owner.m_impl->videoPresentationRuntime.videoTitleDisplayEnabled ? toLibvlcVideoTitlePosition(owner.m_impl->videoPresentationRuntime.videoTitleDisplayPosition) : libvlc_position_disable,
		owner.m_impl->videoPresentationRuntime.videoTitleDisplayTimeoutMs);
}

void ofxVlc4::VideoComponent::applyTeletextSettings() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_video_set_teletext_transparency(player, owner.m_impl->videoPresentationRuntime.teletextTransparencyEnabled);
	libvlc_video_set_teletext(player, owner.m_impl->videoPresentationRuntime.teletextPage);
}

void ofxVlc4::VideoComponent::applyVideoMarquee() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_video_set_marquee_int(player, libvlc_marquee_Enable, owner.m_impl->overlayRuntime.marqueeEnabled ? 1 : 0);
	if (!owner.m_impl->overlayRuntime.marqueeEnabled) {
		return;
	}

	libvlc_video_set_marquee_string(player, libvlc_marquee_Text, owner.m_impl->overlayRuntime.marqueeText.c_str());
	libvlc_video_set_marquee_int(player, libvlc_marquee_Position, toLibvlcOverlayPosition(owner.m_impl->overlayRuntime.marqueePosition));
	libvlc_video_set_marquee_int(player, libvlc_marquee_Opacity, owner.m_impl->overlayRuntime.marqueeOpacity);
	libvlc_video_set_marquee_int(player, libvlc_marquee_Size, owner.m_impl->overlayRuntime.marqueeSize);
	libvlc_video_set_marquee_int(player, libvlc_marquee_Color, owner.m_impl->overlayRuntime.marqueeColor);
	libvlc_video_set_marquee_int(player, libvlc_marquee_Refresh, owner.m_impl->overlayRuntime.marqueeRefresh);
	libvlc_video_set_marquee_int(player, libvlc_marquee_Timeout, owner.m_impl->overlayRuntime.marqueeTimeout);
	libvlc_video_set_marquee_int(player, libvlc_marquee_X, owner.m_impl->overlayRuntime.marqueeX);
	libvlc_video_set_marquee_int(player, libvlc_marquee_Y, owner.m_impl->overlayRuntime.marqueeY);
}

void ofxVlc4::VideoComponent::applyVideoLogo() {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	const std::string resolvedLogoPath = normalizeOptionalPath(owner.m_impl->overlayRuntime.logoPath);
	const bool canEnableLogo = owner.m_impl->overlayRuntime.logoEnabled && !trimWhitespace(resolvedLogoPath).empty();
	libvlc_video_set_logo_int(player, libvlc_logo_enable, canEnableLogo ? 1 : 0);
	if (!canEnableLogo) {
		return;
	}

	libvlc_video_set_logo_string(player, libvlc_logo_file, resolvedLogoPath.c_str());
	libvlc_video_set_logo_int(player, libvlc_logo_position, toLibvlcOverlayPosition(owner.m_impl->overlayRuntime.logoPosition));
	libvlc_video_set_logo_int(player, libvlc_logo_opacity, owner.m_impl->overlayRuntime.logoOpacity);
	libvlc_video_set_logo_int(player, libvlc_logo_x, owner.m_impl->overlayRuntime.logoX);
	libvlc_video_set_logo_int(player, libvlc_logo_y, owner.m_impl->overlayRuntime.logoY);
	libvlc_video_set_logo_int(player, libvlc_logo_delay, owner.m_impl->overlayRuntime.logoDelay);
	libvlc_video_set_logo_int(player, libvlc_logo_repeat, owner.m_impl->overlayRuntime.logoRepeat);
}

bool ofxVlc4::VideoComponent::isMarqueeEnabled() const {
	return owner.m_impl->overlayRuntime.marqueeEnabled;
}

void ofxVlc4::VideoComponent::setMarqueeEnabled(bool enabled) {
	if (owner.m_impl->overlayRuntime.marqueeEnabled == enabled) {
		return;
	}

	owner.m_impl->overlayRuntime.marqueeEnabled = enabled;
	applyVideoMarquee();
	owner.setStatus(std::string("Marquee ") + (owner.m_impl->overlayRuntime.marqueeEnabled ? "enabled." : "disabled."));
}

std::string ofxVlc4::VideoComponent::getMarqueeText() const {
	return owner.m_impl->overlayRuntime.marqueeText;
}

void ofxVlc4::VideoComponent::setMarqueeText(const std::string & text) {
	if (owner.m_impl->overlayRuntime.marqueeText == text) {
		return;
	}

	owner.m_impl->overlayRuntime.marqueeText = text;
	applyVideoMarquee();
}

ofxVlc4::OverlayPosition ofxVlc4::VideoComponent::getMarqueePosition() const {
	return owner.m_impl->overlayRuntime.marqueePosition;
}

void ofxVlc4::VideoComponent::setMarqueePosition(OverlayPosition position) {
	if (owner.m_impl->overlayRuntime.marqueePosition == position) {
		return;
	}

	owner.m_impl->overlayRuntime.marqueePosition = position;
	applyVideoMarquee();
}

int ofxVlc4::VideoComponent::getMarqueeOpacity() const {
	return owner.m_impl->overlayRuntime.marqueeOpacity;
}

void ofxVlc4::VideoComponent::setMarqueeOpacity(int opacity) {
	const int clampedOpacity = ofClamp(opacity, 0, 255);
	if (owner.m_impl->overlayRuntime.marqueeOpacity == clampedOpacity) {
		return;
	}

	owner.m_impl->overlayRuntime.marqueeOpacity = clampedOpacity;
	applyVideoMarquee();
}

int ofxVlc4::VideoComponent::getMarqueeSize() const {
	return owner.m_impl->overlayRuntime.marqueeSize;
}

void ofxVlc4::VideoComponent::setMarqueeSize(int size) {
	const int clampedSize = ofClamp(size, 6, 96);
	if (owner.m_impl->overlayRuntime.marqueeSize == clampedSize) {
		return;
	}

	owner.m_impl->overlayRuntime.marqueeSize = clampedSize;
	applyVideoMarquee();
}

int ofxVlc4::VideoComponent::getMarqueeColor() const {
	return owner.m_impl->overlayRuntime.marqueeColor;
}

void ofxVlc4::VideoComponent::setMarqueeColor(int color) {
	const int clampedColor = clampPackedRgbColor(color);
	if (owner.m_impl->overlayRuntime.marqueeColor == clampedColor) {
		return;
	}

	owner.m_impl->overlayRuntime.marqueeColor = clampedColor;
	applyVideoMarquee();
}

int ofxVlc4::VideoComponent::getMarqueeRefresh() const {
	return owner.m_impl->overlayRuntime.marqueeRefresh;
}

void ofxVlc4::VideoComponent::setMarqueeRefresh(int refreshMs) {
	const int clampedRefreshMs = ofClamp(refreshMs, 0, 10000);
	if (owner.m_impl->overlayRuntime.marqueeRefresh == clampedRefreshMs) {
		return;
	}

	owner.m_impl->overlayRuntime.marqueeRefresh = clampedRefreshMs;
	applyVideoMarquee();
}

int ofxVlc4::VideoComponent::getMarqueeTimeout() const {
	return owner.m_impl->overlayRuntime.marqueeTimeout;
}

void ofxVlc4::VideoComponent::setMarqueeTimeout(int timeoutMs) {
	const int clampedTimeoutMs = ofClamp(timeoutMs, 0, 10000);
	if (owner.m_impl->overlayRuntime.marqueeTimeout == clampedTimeoutMs) {
		return;
	}

	owner.m_impl->overlayRuntime.marqueeTimeout = clampedTimeoutMs;
	applyVideoMarquee();
}

int ofxVlc4::VideoComponent::getMarqueeX() const {
	return owner.m_impl->overlayRuntime.marqueeX;
}

void ofxVlc4::VideoComponent::setMarqueeX(int x) {
	const int clampedX = ofClamp(x, -4096, 4096);
	if (owner.m_impl->overlayRuntime.marqueeX == clampedX) {
		return;
	}

	owner.m_impl->overlayRuntime.marqueeX = clampedX;
	applyVideoMarquee();
}

int ofxVlc4::VideoComponent::getMarqueeY() const {
	return owner.m_impl->overlayRuntime.marqueeY;
}

void ofxVlc4::VideoComponent::setMarqueeY(int y) {
	const int clampedY = ofClamp(y, -4096, 4096);
	if (owner.m_impl->overlayRuntime.marqueeY == clampedY) {
		return;
	}

	owner.m_impl->overlayRuntime.marqueeY = clampedY;
	applyVideoMarquee();
}

bool ofxVlc4::VideoComponent::isLogoEnabled() const {
	return owner.m_impl->overlayRuntime.logoEnabled;
}

void ofxVlc4::VideoComponent::setLogoEnabled(bool enabled) {
	if (owner.m_impl->overlayRuntime.logoEnabled == enabled) {
		return;
	}

	owner.m_impl->overlayRuntime.logoEnabled = enabled;
	applyVideoLogo();
	owner.setStatus(std::string("Logo ") + (owner.m_impl->overlayRuntime.logoEnabled ? "enabled." : "disabled."));
}

std::string ofxVlc4::VideoComponent::getLogoPath() const {
	return owner.m_impl->overlayRuntime.logoPath;
}

void ofxVlc4::VideoComponent::setLogoPath(const std::string & path) {
	if (owner.m_impl->overlayRuntime.logoPath == path) {
		return;
	}

	owner.m_impl->overlayRuntime.logoPath = path;
	applyVideoLogo();
}

ofxVlc4::OverlayPosition ofxVlc4::VideoComponent::getLogoPosition() const {
	return owner.m_impl->overlayRuntime.logoPosition;
}

void ofxVlc4::VideoComponent::setLogoPosition(OverlayPosition position) {
	if (owner.m_impl->overlayRuntime.logoPosition == position) {
		return;
	}

	owner.m_impl->overlayRuntime.logoPosition = position;
	applyVideoLogo();
}

int ofxVlc4::VideoComponent::getLogoOpacity() const {
	return owner.m_impl->overlayRuntime.logoOpacity;
}

void ofxVlc4::VideoComponent::setLogoOpacity(int opacity) {
	const int clampedOpacity = ofClamp(opacity, 0, 255);
	if (owner.m_impl->overlayRuntime.logoOpacity == clampedOpacity) {
		return;
	}

	owner.m_impl->overlayRuntime.logoOpacity = clampedOpacity;
	applyVideoLogo();
}

int ofxVlc4::VideoComponent::getLogoX() const {
	return owner.m_impl->overlayRuntime.logoX;
}

void ofxVlc4::VideoComponent::setLogoX(int x) {
	const int clampedX = ofClamp(x, -4096, 4096);
	if (owner.m_impl->overlayRuntime.logoX == clampedX) {
		return;
	}

	owner.m_impl->overlayRuntime.logoX = clampedX;
	applyVideoLogo();
}

int ofxVlc4::VideoComponent::getLogoY() const {
	return owner.m_impl->overlayRuntime.logoY;
}

void ofxVlc4::VideoComponent::setLogoY(int y) {
	const int clampedY = ofClamp(y, -4096, 4096);
	if (owner.m_impl->overlayRuntime.logoY == clampedY) {
		return;
	}

	owner.m_impl->overlayRuntime.logoY = clampedY;
	applyVideoLogo();
}

int ofxVlc4::VideoComponent::getLogoDelay() const {
	return owner.m_impl->overlayRuntime.logoDelay;
}

void ofxVlc4::VideoComponent::setLogoDelay(int delayMs) {
	const int clampedDelayMs = ofClamp(delayMs, 0, 10000);
	if (owner.m_impl->overlayRuntime.logoDelay == clampedDelayMs) {
		return;
	}

	owner.m_impl->overlayRuntime.logoDelay = clampedDelayMs;
	applyVideoLogo();
}

int ofxVlc4::VideoComponent::getLogoRepeat() const {
	return owner.m_impl->overlayRuntime.logoRepeat;
}

void ofxVlc4::VideoComponent::setLogoRepeat(int repeat) {
	const int clampedRepeat = ofClamp(repeat, -1, 100);
	if (owner.m_impl->overlayRuntime.logoRepeat == clampedRepeat) {
		return;
	}

	owner.m_impl->overlayRuntime.logoRepeat = clampedRepeat;
	applyVideoLogo();
}

int ofxVlc4::VideoComponent::getTeletextPage() const {
	if (libvlc_media_player_t * player = owner.sessionPlayer()) {
		return libvlc_video_get_teletext(player);
	}
	return owner.m_impl->videoPresentationRuntime.teletextPage;
}

void ofxVlc4::VideoComponent::setTeletextPage(int page) {
	const int clampedPage = ofClamp(page, 0, 999);
	if (owner.m_impl->videoPresentationRuntime.teletextPage == clampedPage) {
		return;
	}

	owner.m_impl->videoPresentationRuntime.teletextPage = clampedPage;
	applyTeletextSettings();
	media().refreshSubtitleStateInfo();
}

bool ofxVlc4::VideoComponent::isTeletextTransparencyEnabled() const {
	if (libvlc_media_player_t * player = owner.sessionPlayer()) {
		return libvlc_video_get_teletext_transparency(player);
	}
	return owner.m_impl->videoPresentationRuntime.teletextTransparencyEnabled;
}

void ofxVlc4::VideoComponent::setTeletextTransparencyEnabled(bool enabled) {
	if (owner.m_impl->videoPresentationRuntime.teletextTransparencyEnabled == enabled) {
		return;
	}

	owner.m_impl->videoPresentationRuntime.teletextTransparencyEnabled = enabled;
	applyTeletextSettings();
	media().refreshSubtitleStateInfo();
}

void ofxVlc4::VideoComponent::sendTeletextKey(TeletextKey key) {
	libvlc_media_player_t * player = owner.sessionPlayer();
	if (!player) {
		return;
	}

	libvlc_video_set_teletext(player, static_cast<int>(toLibvlcTeletextKey(key)));
}

bool ofxVlc4::VideoComponent::isKeyInputEnabled() const {
	return owner.m_impl->playerConfigRuntime.keyInputEnabled;
}

void ofxVlc4::VideoComponent::setKeyInputEnabled(bool enabled) {
	libvlc_media_player_t * player = owner.sessionPlayer();
	setInputHandlingEnabled(
		player,
		owner.m_impl->playerConfigRuntime.keyInputEnabled,
		enabled,
		"Video key input ",
		libvlc_video_set_key_input,
		[](const std::string & message) { ofxVlc4::logVerbose(message); });
}

bool ofxVlc4::VideoComponent::isMouseInputEnabled() const {
	return owner.m_impl->playerConfigRuntime.mouseInputEnabled;
}

void ofxVlc4::VideoComponent::setMouseInputEnabled(bool enabled) {
	libvlc_media_player_t * player = owner.sessionPlayer();
	setInputHandlingEnabled(
		player,
		owner.m_impl->playerConfigRuntime.mouseInputEnabled,
		enabled,
		"Video mouse input ",
		libvlc_video_set_mouse_input,
		[](const std::string & message) { ofxVlc4::logVerbose(message); });
}

bool ofxVlc4::VideoComponent::isVlcFullscreenEnabled() const {
	return owner.m_impl->videoPresentationRuntime.vlcFullscreenEnabled;
}

void ofxVlc4::VideoComponent::setVlcFullscreenEnabled(bool enabled) {
	if (owner.m_impl->videoPresentationRuntime.vlcFullscreenEnabled == enabled) {
		return;
	}

	owner.m_impl->videoPresentationRuntime.vlcFullscreenEnabled = enabled;
	applyVlcFullscreen();
	owner.logNotice(std::string("libVLC fullscreen ") + (owner.m_impl->videoPresentationRuntime.vlcFullscreenEnabled ? "enabled." : "disabled."));
}

void ofxVlc4::VideoComponent::toggleVlcFullscreen() {
	setVlcFullscreenEnabled(!owner.m_impl->videoPresentationRuntime.vlcFullscreenEnabled);
}

bool ofxVlc4::VideoComponent::isVideoTitleDisplayEnabled() const {
	return owner.m_impl->videoPresentationRuntime.videoTitleDisplayEnabled;
}

void ofxVlc4::VideoComponent::setVideoTitleDisplayEnabled(bool enabled) {
	if (owner.m_impl->videoPresentationRuntime.videoTitleDisplayEnabled == enabled) {
		return;
	}

	owner.m_impl->videoPresentationRuntime.videoTitleDisplayEnabled = enabled;
	applyVideoTitleDisplay();
}

ofxVlc4::OverlayPosition ofxVlc4::VideoComponent::getVideoTitleDisplayPosition() const {
	return owner.m_impl->videoPresentationRuntime.videoTitleDisplayPosition;
}

void ofxVlc4::VideoComponent::setVideoTitleDisplayPosition(OverlayPosition position) {
	if (owner.m_impl->videoPresentationRuntime.videoTitleDisplayPosition == position) {
		return;
	}

	owner.m_impl->videoPresentationRuntime.videoTitleDisplayPosition = position;
	applyVideoTitleDisplay();
}

unsigned ofxVlc4::VideoComponent::getVideoTitleDisplayTimeoutMs() const {
	return owner.m_impl->videoPresentationRuntime.videoTitleDisplayTimeoutMs;
}

void ofxVlc4::VideoComponent::setVideoTitleDisplayTimeoutMs(unsigned timeoutMs) {
	const unsigned clampedTimeoutMs = std::min(timeoutMs, 60000u);
	if (owner.m_impl->videoPresentationRuntime.videoTitleDisplayTimeoutMs == clampedTimeoutMs) {
		return;
	}

	owner.m_impl->videoPresentationRuntime.videoTitleDisplayTimeoutMs = clampedTimeoutMs;
	applyVideoTitleDisplay();
}

void ofxVlc4::applyVideoInputHandling() {
	m_impl->subsystemRuntime.videoComponent->applyVideoInputHandling();
}

void ofxVlc4::applyVideoTitleDisplay() {
	m_impl->subsystemRuntime.videoComponent->applyVideoTitleDisplay();
}

void ofxVlc4::applyTeletextSettings() {
	m_impl->subsystemRuntime.videoComponent->applyTeletextSettings();
}

void ofxVlc4::applyVideoMarquee() {
	m_impl->subsystemRuntime.videoComponent->applyVideoMarquee();
}

void ofxVlc4::applyVideoLogo() {
	m_impl->subsystemRuntime.videoComponent->applyVideoLogo();
}

int ofxVlc4::getTeletextPage() const {
	return m_impl->subsystemRuntime.videoComponent->getTeletextPage();
}

void ofxVlc4::setTeletextPage(int page) {
	m_impl->subsystemRuntime.videoComponent->setTeletextPage(page);
}

bool ofxVlc4::isTeletextTransparencyEnabled() const {
	return m_impl->subsystemRuntime.videoComponent->isTeletextTransparencyEnabled();
}

void ofxVlc4::setTeletextTransparencyEnabled(bool enabled) {
	m_impl->subsystemRuntime.videoComponent->setTeletextTransparencyEnabled(enabled);
}

void ofxVlc4::sendTeletextKey(TeletextKey key) {
	m_impl->subsystemRuntime.videoComponent->sendTeletextKey(key);
}

void ofxVlc4::sendTeletextKeyRed() {
	sendTeletextKey(TeletextKey::Red);
}

void ofxVlc4::sendTeletextKeyGreen() {
	sendTeletextKey(TeletextKey::Green);
}

void ofxVlc4::sendTeletextKeyYellow() {
	sendTeletextKey(TeletextKey::Yellow);
}

void ofxVlc4::sendTeletextKeyBlue() {
	sendTeletextKey(TeletextKey::Blue);
}

void ofxVlc4::sendTeletextKeyIndex() {
	sendTeletextKey(TeletextKey::Index);
}

void ofxVlc4::toggleTeletextTransparency() {
	setTeletextTransparencyEnabled(!isTeletextTransparencyEnabled());
}

bool ofxVlc4::isKeyInputEnabled() const {
	return m_impl->subsystemRuntime.videoComponent->isKeyInputEnabled();
}

void ofxVlc4::setKeyInputEnabled(bool enabled) {
	m_impl->subsystemRuntime.videoComponent->setKeyInputEnabled(enabled);
}

bool ofxVlc4::isMouseInputEnabled() const {
	return m_impl->subsystemRuntime.videoComponent->isMouseInputEnabled();
}

void ofxVlc4::setMouseInputEnabled(bool enabled) {
	m_impl->subsystemRuntime.videoComponent->setMouseInputEnabled(enabled);
}

bool ofxVlc4::isVlcFullscreenEnabled() const {
	return m_impl->subsystemRuntime.videoComponent->isVlcFullscreenEnabled();
}

void ofxVlc4::setVlcFullscreenEnabled(bool enabled) {
	m_impl->subsystemRuntime.videoComponent->setVlcFullscreenEnabled(enabled);
}

void ofxVlc4::toggleVlcFullscreen() {
	m_impl->subsystemRuntime.videoComponent->toggleVlcFullscreen();
}

bool ofxVlc4::isVideoTitleDisplayEnabled() const {
	return m_impl->subsystemRuntime.videoComponent->isVideoTitleDisplayEnabled();
}

void ofxVlc4::setVideoTitleDisplayEnabled(bool enabled) {
	m_impl->subsystemRuntime.videoComponent->setVideoTitleDisplayEnabled(enabled);
}

ofxVlc4::OverlayPosition ofxVlc4::getVideoTitleDisplayPosition() const {
	return m_impl->subsystemRuntime.videoComponent->getVideoTitleDisplayPosition();
}

void ofxVlc4::setVideoTitleDisplayPosition(OverlayPosition position) {
	m_impl->subsystemRuntime.videoComponent->setVideoTitleDisplayPosition(position);
}

unsigned ofxVlc4::getVideoTitleDisplayTimeoutMs() const {
	return m_impl->subsystemRuntime.videoComponent->getVideoTitleDisplayTimeoutMs();
}

void ofxVlc4::setVideoTitleDisplayTimeoutMs(unsigned timeoutMs) {
	m_impl->subsystemRuntime.videoComponent->setVideoTitleDisplayTimeoutMs(timeoutMs);
}

bool ofxVlc4::isMarqueeEnabled() const {
	return m_impl->subsystemRuntime.videoComponent->isMarqueeEnabled();
}

void ofxVlc4::setMarqueeEnabled(bool enabled) {
	m_impl->subsystemRuntime.videoComponent->setMarqueeEnabled(enabled);
}

std::string ofxVlc4::getMarqueeText() const {
	return m_impl->subsystemRuntime.videoComponent->getMarqueeText();
}

void ofxVlc4::setMarqueeText(const std::string & text) {
	m_impl->subsystemRuntime.videoComponent->setMarqueeText(text);
}

ofxVlc4::OverlayPosition ofxVlc4::getMarqueePosition() const {
	return m_impl->subsystemRuntime.videoComponent->getMarqueePosition();
}

void ofxVlc4::setMarqueePosition(OverlayPosition position) {
	m_impl->subsystemRuntime.videoComponent->setMarqueePosition(position);
}

int ofxVlc4::getMarqueeOpacity() const {
	return m_impl->subsystemRuntime.videoComponent->getMarqueeOpacity();
}

void ofxVlc4::setMarqueeOpacity(int opacity) {
	m_impl->subsystemRuntime.videoComponent->setMarqueeOpacity(opacity);
}

int ofxVlc4::getMarqueeSize() const {
	return m_impl->subsystemRuntime.videoComponent->getMarqueeSize();
}

void ofxVlc4::setMarqueeSize(int size) {
	m_impl->subsystemRuntime.videoComponent->setMarqueeSize(size);
}

int ofxVlc4::getMarqueeColor() const {
	return m_impl->subsystemRuntime.videoComponent->getMarqueeColor();
}

void ofxVlc4::setMarqueeColor(int color) {
	m_impl->subsystemRuntime.videoComponent->setMarqueeColor(color);
}

int ofxVlc4::getMarqueeRefresh() const {
	return m_impl->subsystemRuntime.videoComponent->getMarqueeRefresh();
}

void ofxVlc4::setMarqueeRefresh(int refreshMs) {
	m_impl->subsystemRuntime.videoComponent->setMarqueeRefresh(refreshMs);
}

int ofxVlc4::getMarqueeTimeout() const {
	return m_impl->subsystemRuntime.videoComponent->getMarqueeTimeout();
}

void ofxVlc4::setMarqueeTimeout(int timeoutMs) {
	m_impl->subsystemRuntime.videoComponent->setMarqueeTimeout(timeoutMs);
}

int ofxVlc4::getMarqueeX() const {
	return m_impl->subsystemRuntime.videoComponent->getMarqueeX();
}

void ofxVlc4::setMarqueeX(int x) {
	m_impl->subsystemRuntime.videoComponent->setMarqueeX(x);
}

int ofxVlc4::getMarqueeY() const {
	return m_impl->subsystemRuntime.videoComponent->getMarqueeY();
}

void ofxVlc4::setMarqueeY(int y) {
	m_impl->subsystemRuntime.videoComponent->setMarqueeY(y);
}

bool ofxVlc4::isLogoEnabled() const {
	return m_impl->subsystemRuntime.videoComponent->isLogoEnabled();
}

void ofxVlc4::setLogoEnabled(bool enabled) {
	m_impl->subsystemRuntime.videoComponent->setLogoEnabled(enabled);
}

std::string ofxVlc4::getLogoPath() const {
	return m_impl->subsystemRuntime.videoComponent->getLogoPath();
}

void ofxVlc4::setLogoPath(const std::string & path) {
	m_impl->subsystemRuntime.videoComponent->setLogoPath(path);
}

ofxVlc4::OverlayPosition ofxVlc4::getLogoPosition() const {
	return m_impl->subsystemRuntime.videoComponent->getLogoPosition();
}

void ofxVlc4::setLogoPosition(OverlayPosition position) {
	m_impl->subsystemRuntime.videoComponent->setLogoPosition(position);
}

int ofxVlc4::getLogoOpacity() const {
	return m_impl->subsystemRuntime.videoComponent->getLogoOpacity();
}

void ofxVlc4::setLogoOpacity(int opacity) {
	m_impl->subsystemRuntime.videoComponent->setLogoOpacity(opacity);
}

int ofxVlc4::getLogoX() const {
	return m_impl->subsystemRuntime.videoComponent->getLogoX();
}

void ofxVlc4::setLogoX(int x) {
	m_impl->subsystemRuntime.videoComponent->setLogoX(x);
}

int ofxVlc4::getLogoY() const {
	return m_impl->subsystemRuntime.videoComponent->getLogoY();
}

void ofxVlc4::setLogoY(int y) {
	m_impl->subsystemRuntime.videoComponent->setLogoY(y);
}

int ofxVlc4::getLogoDelay() const {
	return m_impl->subsystemRuntime.videoComponent->getLogoDelay();
}

void ofxVlc4::setLogoDelay(int delayMs) {
	m_impl->subsystemRuntime.videoComponent->setLogoDelay(delayMs);
}

int ofxVlc4::getLogoRepeat() const {
	return m_impl->subsystemRuntime.videoComponent->getLogoRepeat();
}

void ofxVlc4::setLogoRepeat(int repeat) {
	m_impl->subsystemRuntime.videoComponent->setLogoRepeat(repeat);
}

