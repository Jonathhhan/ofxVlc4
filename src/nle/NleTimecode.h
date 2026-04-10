#pragma once

// ---------------------------------------------------------------------------
// NleTimecode — SMPTE timecode engine for the Avid-oriented NLE.
//
// Wraps a frame count with an associated frame rate.  Supports conversion
// to/from milliseconds (for ofxVlc4 setTime()/getTime()), SMPTE string
// (HH:MM:SS:FF), and raw frame numbers.  Handles drop-frame arithmetic
// for 29.97 fps and 59.94 fps.
//
// Pure logic — no dependencies on OF, GLFW, or VLC.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace nle {

// ---------------------------------------------------------------------------
// FrameRate — supported SMPTE frame rates.
// ---------------------------------------------------------------------------

enum class FrameRate {
	Fps23_976,  // 24000/1001  (23.976 NDF)
	Fps24,      // 24/1
	Fps25,      // 25/1        (PAL)
	Fps29_97_DF,// 30000/1001  (drop-frame)
	Fps29_97_NDF,// 30000/1001 (non-drop-frame)
	Fps30,      // 30/1
	Fps50,      // 50/1
	Fps59_94_DF,// 60000/1001  (drop-frame)
	Fps59_94_NDF,// 60000/1001 (non-drop-frame)
	Fps60       // 60/1
};

// ---------------------------------------------------------------------------
// FrameRate helpers
// ---------------------------------------------------------------------------

/// True if the rate uses drop-frame counting.
inline bool isDropFrame(FrameRate rate) {
	return rate == FrameRate::Fps29_97_DF || rate == FrameRate::Fps59_94_DF;
}

/// Nominal integer frames-per-second (30 for 29.97, 60 for 59.94, etc.).
inline int nominalFps(FrameRate rate) {
	switch (rate) {
	case FrameRate::Fps23_976:   return 24;
	case FrameRate::Fps24:       return 24;
	case FrameRate::Fps25:       return 25;
	case FrameRate::Fps29_97_DF: return 30;
	case FrameRate::Fps29_97_NDF:return 30;
	case FrameRate::Fps30:       return 30;
	case FrameRate::Fps50:       return 50;
	case FrameRate::Fps59_94_DF: return 60;
	case FrameRate::Fps59_94_NDF:return 60;
	case FrameRate::Fps60:       return 60;
	}
	return 30;
}

/// Exact rational rate as numerator/denominator.
inline void rationalFps(FrameRate rate, int & num, int & den) {
	switch (rate) {
	case FrameRate::Fps23_976:    num = 24000; den = 1001; return;
	case FrameRate::Fps24:        num = 24;    den = 1;    return;
	case FrameRate::Fps25:        num = 25;    den = 1;    return;
	case FrameRate::Fps29_97_DF:  num = 30000; den = 1001; return;
	case FrameRate::Fps29_97_NDF: num = 30000; den = 1001; return;
	case FrameRate::Fps30:        num = 30;    den = 1;    return;
	case FrameRate::Fps50:        num = 50;    den = 1;    return;
	case FrameRate::Fps59_94_DF:  num = 60000; den = 1001; return;
	case FrameRate::Fps59_94_NDF: num = 60000; den = 1001; return;
	case FrameRate::Fps60:        num = 60;    den = 1;    return;
	}
	num = 30; den = 1;
}

/// Exact floating-point rate.
inline double exactFps(FrameRate rate) {
	int num = 0;
	int den = 0;
	rationalFps(rate, num, den);
	return static_cast<double>(num) / static_cast<double>(den);
}

/// Number of frames dropped per minute for drop-frame rates.
/// 29.97 DF drops 2 frames; 59.94 DF drops 4 frames.
inline int droppedFramesPerMinute(FrameRate rate) {
	if (rate == FrameRate::Fps29_97_DF) return 2;
	if (rate == FrameRate::Fps59_94_DF) return 4;
	return 0;
}

/// Human-readable label for the frame rate.
inline std::string frameRateLabel(FrameRate rate) {
	switch (rate) {
	case FrameRate::Fps23_976:    return "23.976";
	case FrameRate::Fps24:        return "24";
	case FrameRate::Fps25:        return "25";
	case FrameRate::Fps29_97_DF:  return "29.97 DF";
	case FrameRate::Fps29_97_NDF: return "29.97 NDF";
	case FrameRate::Fps30:        return "30";
	case FrameRate::Fps50:        return "50";
	case FrameRate::Fps59_94_DF:  return "59.94 DF";
	case FrameRate::Fps59_94_NDF: return "59.94 NDF";
	case FrameRate::Fps60:        return "60";
	}
	return "??";
}

// ---------------------------------------------------------------------------
// Timecode
// ---------------------------------------------------------------------------

class Timecode {
public:
	/// Default: frame 0 at 24 fps.
	Timecode() = default;

	/// Construct from an absolute frame count and rate.
	Timecode(int64_t totalFrames, FrameRate rate)
		: m_frames(totalFrames < 0 ? 0 : totalFrames)
		, m_rate(rate) {}

	// -- Accessors --

	int64_t   totalFrames() const { return m_frames; }
	FrameRate rate()        const { return m_rate; }

	// -- Conversions --

	/// Convert to milliseconds (for ofxVlc4 setTime).
	int toMilliseconds() const {
		const double fps = exactFps(m_rate);
		return static_cast<int>(std::round(
			(static_cast<double>(m_frames) / fps) * 1000.0));
	}

	/// Construct a Timecode from milliseconds and a frame rate.
	static Timecode fromMilliseconds(int ms, FrameRate rate) {
		if (ms < 0) ms = 0;
		const double fps = exactFps(rate);
		const int64_t frames = static_cast<int64_t>(
			std::round(static_cast<double>(ms) * fps / 1000.0));
		return Timecode(frames, rate);
	}

	/// Construct from HH:MM:SS:FF (or HH:MM:SS;FF for drop-frame).
	/// The separator before FF must be ':' for NDF and ';' for DF.
	/// This constructor accepts either separator but the rate determines
	/// whether drop-frame accounting is applied.
	static Timecode fromSmpte(int hours, int minutes, int seconds,
							  int frames, FrameRate rate) {
		const int fps = nominalFps(rate);

		// Clamp individual fields.
		if (hours < 0)   hours = 0;
		if (minutes < 0) minutes = 0;
		if (seconds < 0) seconds = 0;
		if (frames < 0)  frames = 0;
		if (minutes > 59) minutes = 59;
		if (seconds > 59) seconds = 59;
		if (frames >= fps) frames = fps - 1;

		int64_t totalFrames = 0;

		if (isDropFrame(rate)) {
			// Standard drop-frame conversion: SMPTE → absolute frame count.
			const int d = droppedFramesPerMinute(rate); // 2 or 4
			const int totalMinutes = hours * 60 + minutes;
			const int dropCorrection = d * (totalMinutes - totalMinutes / 10);

			totalFrames =
				static_cast<int64_t>(hours) * 3600 * fps +
				static_cast<int64_t>(minutes) * 60 * fps +
				static_cast<int64_t>(seconds) * fps +
				frames -
				dropCorrection;
		} else {
			// Non-drop-frame: straightforward arithmetic.
			totalFrames =
				static_cast<int64_t>(hours) * 3600 * fps +
				static_cast<int64_t>(minutes) * 60 * fps +
				static_cast<int64_t>(seconds) * fps +
				frames;
		}

		return Timecode(totalFrames, rate);
	}

	/// Parse a SMPTE string: "HH:MM:SS:FF" (NDF) or "HH:MM:SS;FF" (DF).
	static Timecode fromSmpteString(const std::string & str, FrameRate rate) {
		int hh = 0, mm = 0, ss = 0, ff = 0;
		// Accept both ':' and ';' as the last separator.
		if (std::sscanf(str.c_str(), "%d:%d:%d%*[;:]%d", &hh, &mm, &ss, &ff) == 4) {
			return fromSmpte(hh, mm, ss, ff, rate);
		}
		return Timecode(0, rate);
	}

	/// Convert to SMPTE components.
	void toSmpte(int & hours, int & minutes, int & seconds, int & frames) const {
		const int fps = nominalFps(m_rate);

		if (isDropFrame(m_rate)) {
			// Standard frames → SMPTE drop-frame conversion.
			const int d = droppedFramesPerMinute(m_rate);
			const int framesPerMinute     = fps * 60 - d;
			const int firstMinuteFrames   = fps * 60;
			const int framesPerTenMinutes = firstMinuteFrames + framesPerMinute * 9;

			int64_t remaining = m_frames;

			const int tenMinBlocks = static_cast<int>(remaining / framesPerTenMinutes);
			remaining %= framesPerTenMinutes;

			int minuteInGroup = 0;
			if (remaining >= firstMinuteFrames) {
				// Past the first minute of the 10-min block.
				remaining -= firstMinuteFrames;
				minuteInGroup = static_cast<int>(remaining / framesPerMinute) + 1;
				remaining = remaining % framesPerMinute + d;
			}

			const int totalMinutes = tenMinBlocks * 10 + minuteInGroup;
			hours   = totalMinutes / 60;
			minutes = totalMinutes % 60;
			seconds = static_cast<int>(remaining / fps);
			frames  = static_cast<int>(remaining % fps);
		} else {
			int64_t remaining = m_frames;
			frames  = static_cast<int>(remaining % fps);
			remaining /= fps;
			seconds = static_cast<int>(remaining % 60);
			remaining /= 60;
			minutes = static_cast<int>(remaining % 60);
			hours   = static_cast<int>(remaining / 60);
		}
	}

	/// Format as SMPTE string.  DF uses ';' before the frame field.
	std::string toSmpteString() const {
		int hh = 0, mm = 0, ss = 0, ff = 0;
		toSmpte(hh, mm, ss, ff);
		char buf[32];
		const char sep = isDropFrame(m_rate) ? ';' : ':';
		std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d%c%02d",
					  hh, mm, ss, sep, ff);
		return std::string(buf);
	}

	// -- Arithmetic --

	Timecode operator+(const Timecode & rhs) const {
		return Timecode(m_frames + rhs.m_frames, m_rate);
	}

	Timecode operator-(const Timecode & rhs) const {
		const int64_t diff = m_frames - rhs.m_frames;
		return Timecode(diff < 0 ? 0 : diff, m_rate);
	}

	Timecode & operator+=(const Timecode & rhs) {
		m_frames += rhs.m_frames;
		return *this;
	}

	Timecode & operator-=(const Timecode & rhs) {
		m_frames -= rhs.m_frames;
		if (m_frames < 0) m_frames = 0;
		return *this;
	}

	// -- Comparison --

	bool operator==(const Timecode & rhs) const { return m_frames == rhs.m_frames; }
	bool operator!=(const Timecode & rhs) const { return m_frames != rhs.m_frames; }
	bool operator< (const Timecode & rhs) const { return m_frames <  rhs.m_frames; }
	bool operator<=(const Timecode & rhs) const { return m_frames <= rhs.m_frames; }
	bool operator> (const Timecode & rhs) const { return m_frames >  rhs.m_frames; }
	bool operator>=(const Timecode & rhs) const { return m_frames >= rhs.m_frames; }

	/// Advance by one frame.
	Timecode & operator++() { ++m_frames; return *this; }

	/// Retreat by one frame (clamped to 0).
	Timecode & operator--() { if (m_frames > 0) --m_frames; return *this; }

private:
	int64_t   m_frames = 0;
	FrameRate m_rate   = FrameRate::Fps24;
};

} // namespace nle
