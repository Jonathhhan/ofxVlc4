# ofxVlc4MidiExample

Focused MIDI-and-media example for `ofxVlc4`.

It shows two paths side by side:

- regular audio/video playback through `libVLC`
- local MIDI file playback, analysis, export, and sync routed through `ofxMidiOut`

See also:

- [../README.md](../README.md)
- [../CHANGELOG.md](../CHANGELOG.md)

What it shows:

- addon-owned MIDI transport through `ofxVlc4`
- addon-owned MIDI sync controls:
  - MIDI Clock
  - MTC quarter-frame
  - optional watch-time follow
- local MIDI output routing through `ofxMidiOut`
- drag-and-drop loading for both VLC media and `.mid` / `.midi`
- ImGui-based controls instead of the old bitmap-text overlay
- the default texture backend for VLC preview

Controls:

- `Space`
  - play / pause current VLC or MIDI transport
- `S`
  - stop current VLC or MIDI transport
- `N` / `P`
  - next / previous VLC playlist item
- `M`
  - mute VLC playback
- `Up / Down`
  - VLC volume
- `I`
  - export the current MIDI analysis bundle
- `[` / `]`
  - previous / next MIDI output port
- `R`
  - refresh MIDI output ports
- `-` / `+`
  - MIDI tempo multiplier
- `,` / `.`
  - previous / next selected MIDI channel
- `K`
  - mute / unmute selected MIDI channel
- `L`
  - solo / unsolo selected MIDI channel
- `0`
  - clear MIDI solo
- file drag-and-drop
  - loads dropped media into the matching VLC or local MIDI path

Notes:

- MIDI files do not use VLC synthesis here
- MIDI playback is routed to the selected `ofxMidiOut` device or virtual port
- video preview stays conservative and uses the same normal `player.draw(...)` path
- watch-time stays enabled for VLC media and can optionally drive MIDI sync

Useful API calls demonstrated:

- `init(...)`
- `setWatchTimeEnabled(...)`
- `setWatchTimeMinPeriodUs(...)`
- `formatCurrentPlaybackTimecode()`
- `loadMidiFile(...)`
- `playMidi()`
- `pauseMidi()`
- `stopMidi()`
- `seekMidi(...)`
- `setMidiTempoMultiplier(...)`
- `setMidiMessageCallback(...)`
- `setMidiSyncSettings(...)`
- `setMidiSyncSource(...)`
- `setMidiSyncToWatchTimeEnabled(...)`
- `getMidiTransportInfo()`
- `getMidiAnalysisReport()`
- `draw(...)`
- `close()`

Recommended MIDI flow:

- configure watch-time and MIDI sync/output behavior once:
  - `setWatchTimeEnabled(...)`
  - `setWatchTimeMinPeriodUs(...)`
  - `setMidiSyncSettings(...)`
  - `setMidiMessageCallback(...)`
- load a file with:
  - `loadMidiFile(...)`
- drive transport with:
  - `playMidi()`
  - `pauseMidi()`
  - `stopMidi()`
  - `seekMidi(...)`
- observe state with:
  - `getMidiTransportInfo()`
  - `getMidiAnalysisReport()`
- keep routing and sync policy in addon state instead of rebuilding MIDI timing in app code

This example is meant as the addon's smallest MIDI-capable UI starting point: real playback for normal media, real MIDI transport for `.mid` files, and a compact ImGui panel for transport, routing, sync, and analysis.
