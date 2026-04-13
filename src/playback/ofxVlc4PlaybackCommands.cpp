#include "playback/ofxVlc4PlaybackCommands.h"

#include <algorithm>

namespace ofxVlc4Playback {
bool execute(ofxVlc4 & player, ofxVlc4::PlayerCommand command) {
	const auto seekByMilliseconds = [&player](int deltaMs) {
		const int currentTimeMs = std::max(0, player.getTime());
		player.setTime(std::max(0, currentTimeMs + deltaMs));
	};

	switch (command) {
	case ofxVlc4::PlayerCommand::PlayPause:
		if (player.isPlaying()) {
			player.pause();
		} else {
			player.play();
		}
		return true;
	case ofxVlc4::PlayerCommand::Play:
		player.play();
		return true;
	case ofxVlc4::PlayerCommand::Pause:
		player.pause();
		return true;
	case ofxVlc4::PlayerCommand::Stop:
		player.stop();
		return true;
	case ofxVlc4::PlayerCommand::NextItem:
		player.nextMediaListItem();
		return true;
	case ofxVlc4::PlayerCommand::PreviousItem:
		player.previousMediaListItem();
		return true;
	case ofxVlc4::PlayerCommand::SeekForwardSmall:
		seekByMilliseconds(5000);
		return true;
	case ofxVlc4::PlayerCommand::SeekBackwardSmall:
		seekByMilliseconds(-5000);
		return true;
	case ofxVlc4::PlayerCommand::SeekForwardLarge:
		seekByMilliseconds(30000);
		return true;
	case ofxVlc4::PlayerCommand::SeekBackwardLarge:
		seekByMilliseconds(-30000);
		return true;
	case ofxVlc4::PlayerCommand::VolumeUp:
		player.setVolume(player.getVolume() + 5);
		return true;
	case ofxVlc4::PlayerCommand::VolumeDown:
		player.setVolume(player.getVolume() - 5);
		return true;
	case ofxVlc4::PlayerCommand::ToggleMute:
		player.toggleMute();
		return true;
	case ofxVlc4::PlayerCommand::NextFrame:
		player.nextFrame();
		return true;
	case ofxVlc4::PlayerCommand::PreviousChapter:
		player.previousChapter();
		return true;
	case ofxVlc4::PlayerCommand::NextChapter:
		player.nextChapter();
		return true;
	case ofxVlc4::PlayerCommand::MenuActivate:
		player.navigate(ofxVlc4::NavigationMode::Activate);
		return true;
	case ofxVlc4::PlayerCommand::MenuUp:
		player.navigate(ofxVlc4::NavigationMode::Up);
		return true;
	case ofxVlc4::PlayerCommand::MenuDown:
		player.navigate(ofxVlc4::NavigationMode::Down);
		return true;
	case ofxVlc4::PlayerCommand::MenuLeft:
		player.navigate(ofxVlc4::NavigationMode::Left);
		return true;
	case ofxVlc4::PlayerCommand::MenuRight:
		player.navigate(ofxVlc4::NavigationMode::Right);
		return true;
	case ofxVlc4::PlayerCommand::MenuPopup:
		player.navigate(ofxVlc4::NavigationMode::Popup);
		return true;
	case ofxVlc4::PlayerCommand::TeletextRed:
		player.sendTeletextKey(ofxVlc4::TeletextKey::Red);
		return true;
	case ofxVlc4::PlayerCommand::TeletextGreen:
		player.sendTeletextKey(ofxVlc4::TeletextKey::Green);
		return true;
	case ofxVlc4::PlayerCommand::TeletextYellow:
		player.sendTeletextKey(ofxVlc4::TeletextKey::Yellow);
		return true;
	case ofxVlc4::PlayerCommand::TeletextBlue:
		player.sendTeletextKey(ofxVlc4::TeletextKey::Blue);
		return true;
	case ofxVlc4::PlayerCommand::TeletextIndex:
		player.sendTeletextKey(ofxVlc4::TeletextKey::Index);
		return true;
	case ofxVlc4::PlayerCommand::ToggleTeletextTransparency:
		player.setTeletextTransparencyEnabled(!player.isTeletextTransparencyEnabled());
		return true;
	}

	return false;
}
}
