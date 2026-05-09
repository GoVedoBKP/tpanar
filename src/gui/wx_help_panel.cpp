#include "wx_help_panel.h"
#include "../core/engine.h"

#include <wx/sizer.h>

namespace tpanar_ns {

HelpPanel::HelpPanel(wxWindow* parent, Engine& engine)
    : wxPanel(parent, wxID_ANY), m_engine(engine), m_html_win(nullptr)
{
    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);

    m_html_win = new wxHtmlWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHW_SCROLLBAR_AUTO);

    main_sizer->Add(m_html_win, 1, wxEXPAND | wxALL, 0);
    SetSizer(main_sizer);

    load_documentation();
}

void HelpPanel::load_documentation() {
    if (!m_html_win) return;

    wxString html = R"(
<html>
<body bgcolor="#1e1e1e" text="#d4d4d4" link="#569cd6" vlink="#4ec9b0">
<h1>TPanar &mdash; Digital Drums Recording Workstation</h1>
<p>TPanar is built around a drum-production workflow: record audio takes, analyze them into note events, quantize the resulting note tracks, and use the tracker and timeline together to refine the performance.</p>

<h2>Core Workflow</h2>
<ol>
<li><b>Add a track pair in Project</b> &mdash; the <b>+</b> button creates a linked <b>Audio N</b> track with a <b>Sampler</b> instrument and a linked <b>Note N</b> track with a <b>SoundFont</b> instrument.</li>
<li><b>Assign audio input</b> &mdash; each audio track shows an input channel selector in the Project tab; update it after reinitializing audio.</li>
<li><b>Record audio</b> &mdash; arm one or more audio tracks (R button), enable <b>Rec</b> in the transport, then press <b>Play</b>. All armed tracks record simultaneously.</li>
<li><b>Punch-in recording</b> &mdash; set an IN and OUT marker in the Tracks view, then choose <b>Punch record</b> from the right-click menu to re-record only the marked region.</li>
<li><b>Analyze audio</b> &mdash; use <b>Analyze</b> on an audio track in the Project tab to populate its linked note track.</li>
<li><b>Quantize notes</b> &mdash; use <b>Quantize</b> on a note track and choose either tracker rows or metronome clicks.</li>
<li><b>Edit and arrange</b> &mdash; refine note timing/velocity in Tracker, then edit audio ranges and apply retrigger stretching in Tracks.</li>
<li><b>Export</b> &mdash; use <b>Export WAV</b> to render a stereo mix (muted tracks are excluded), or <b>Export MIDI</b> to save all notation tracks as a Standard MIDI File.</li>
</ol>

<h2>Views</h2>
<table width="100%" border="0" cellpadding="4">
<tr><td valign="top"><b>Project</b></td><td>Track pairing, linked-track routing, drum role, analysis/quantize actions, output bus, audio input selector, and note-track humanization.</td></tr>
<tr><td valign="top"><b>Tracker</b></td><td>Tempo plus note tracks only. Each note track shows note/volume columns, with up to three sub-columns per track.</td></tr>
<tr><td valign="top"><b>Tracks</b></td><td>Timeline for audio takes and note content. Includes track header controls, input VU meters, range editing, group editing, punch-in markers, and retrigger stretch actions.</td></tr>
<tr><td valign="top"><b>Instrument</b></td><td>Sampler, SoundFont, and SFZ editing, plus direct sample recording into sampler slots.</td></tr>
<tr><td valign="top"><b>Mixer</b></td><td>Per-track and bus gain, pan, mute, solo, DSP chains, group mute buttons for audio / notation tracks, and spectrum analysis.</td></tr>
<tr><td valign="top"><b>Settings</b></td><td>Audio backend, MIDI, theme, key bindings, and GUI settings.</td></tr>
<tr><td valign="top"><b>Help</b></td><td>This workflow guide.</td></tr>
</table>

<h2>Transport</h2>
<p>The transport separates tracker editing from audio recording:</p>
<ul>
<li><b>Edit</b> &mdash; enables tracker note/volume entry.</li>
<li><b>Rec</b> &mdash; enables transport-controlled recording for all armed audio tracks.</li>
<li><b>Loop</b> &mdash; loops playback.</li>
<li><b>&#9833;</b> &mdash; metronome toggle with level control.</li>
<li><b>BPM / LPB / Oct / Step</b> &mdash; tempo, lines per beat, base octave, and tracker step size.</li>
</ul>
<table width="100%" border="0" cellpadding="3">
<tr><td><code>Space</code></td><td>Play / Stop</td></tr>
<tr><td><code>F5</code></td><td>Play song from the beginning</td></tr>
<tr><td><code>F6</code></td><td>Play current pattern</td></tr>
<tr><td><code>F7</code></td><td>Play from current row</td></tr>
<tr><td><code>F8</code></td><td>Stop</td></tr>
<tr><td><code>R</code></td><td>Toggle Edit mode</td></tr>
<tr><td><code>Esc</code></td><td>Toggle Edit mode</td></tr>
<tr><td><code>`</code></td><td>Toggle metronome</td></tr>
</table>

<h2>Project Tab</h2>
<p>Each added pair consists of one audio track and one linked note track.</p>
<ul>
<li><b>Audio track</b> &mdash; sampler-backed take lane, audio input assignment (updates after audio re-init), drum role, linked note track, output bus, and <b>Analyze</b>.</li>
<li><b>Note track</b> &mdash; SoundFont-backed note lane, linked audio track, output bus, humanization controls, and <b>Quantize</b>.</li>
<li><b>Quantize</b> &mdash; snaps note events either to tracker rows or directly to metronome clicks.</li>
<li><b>Analyze</b> &mdash; detects drum hits on the audio take and writes events into the linked note track.</li>
<li><b>Export WAV</b> &mdash; renders the whole song to a stereo WAV file. Muted tracks are excluded from the mix.</li>
<li><b>Export MIDI</b> &mdash; saves all notation tracks as a Format-1 Standard MIDI File (one MIDI track per note track). Drum tracks are assigned to MIDI channel 10.</li>
</ul>

<h2>Tracker Tab</h2>
<p>The tracker shows the tempo track and note tracks only. Audio tracks do not appear here.</p>
<ul>
<li>The pattern list is a linear set: each song position owns its own pattern instead of reusing references to an earlier one.</li>
<li>Each note sub-column contains <b>Note</b> and <b>Volume</b> fields.</li>
<li>Use the header <b>+</b> and <b>-</b> buttons to change the number of sub-columns, up to <b>3</b>.</li>
<li>Use pattern copy when you want a starting point for the next section: it creates a new editable clone rather than another reference to the same pattern.</li>
<li>Use <b>Split</b> with the <b>4 bars</b> / <b>8 bars</b> selector to turn one long pattern into shorter sections; tempo changes become hard cuts and the remaining cuts are placed on nearby lower-activity bar boundaries.</li>
<li>Use <b>Join</b> to merge the whole pattern list back into one large pattern without losing event timing.</li>
<li>The tracker is intended for note correction, velocity work, and pattern editing after analysis or manual entry.</li>
</ul>
<table width="100%" border="0" cellpadding="3">
<tr><td><code>Z S X D C V G B H N J M</code></td><td>C4&ndash;B4</td></tr>
<tr><td><code>Q 2 W 3 E R 5 T 6 Y 7 U I</code></td><td>C5&ndash;C6</td></tr>
<tr><td><code>1</code></td><td>Note Off</td></tr>
<tr><td><code>Arrow Keys</code></td><td>Move cursor</td></tr>
<tr><td><code>Tab / Shift+Tab</code></td><td>Next / previous visible track</td></tr>
<tr><td><code>Shift+Arrows</code></td><td>Extend selection</td></tr>
<tr><td><code>Ctrl+A</code></td><td>Select all</td></tr>
<tr><td><code>Ctrl+Z / Ctrl+Y</code></td><td>Undo / Redo</td></tr>
<tr><td><code>Delete</code></td><td>Clear current field or selection</td></tr>
</table>
<p>Right-click the tracker to transpose notes or select the current sub-column / whole visible track.</p>

<h2>Tracks Tab</h2>
<p>The Tracks tab is the timeline view for linked audio and note content.</p>
<ul>
<li>Header controls provide <b>Mute</b>, <b>Solo</b>, <b>Pan</b>, and <b>Volume</b> on audio and note tracks. Armed audio tracks also show a live <b>input VU meter</b> in the header.</li>
<li>Audio tracks have an <b>R</b> arm button. All armed audio tracks record simultaneously when the transport <b>Rec</b> is enabled.</li>
<li>During recording the waveform updates in real time. The existing take is not played back while recording is active.</li>
<li>Use <b>Ctrl/Cmd-click</b> for multi-select and <b>Shift-click</b> for range selection across tracks.</li>
<li>Cut, copy, paste, silence, and insert-gap operations apply to all selected tracks.</li>
<li>Right-click an audio track for <b>Retrigger Stretch Selection</b> or <b>Retrigger Stretch Whole Track</b>. This needs a linked note track with quantized notes.</li>
</ul>

<h3>Punch-in / Punch-out Recording</h3>
<p>Use punch markers to re-record only a specific region without affecting the rest of the take:</p>
<ol>
<li>Right-click and choose <b>Set IN marker here</b> (or press <code>[</code> at the selection start) to place the punch-in point.</li>
<li>Right-click and choose <b>Set OUT marker here</b> (or press <code>]</code> at the selection end) to place the punch-out point.</li>
<li>Arm the target audio track(s), then right-click and choose <b>Punch record</b>.</li>
</ol>
<p>Playback starts 2 bars before the IN marker. Recording begins exactly at the IN point and stops at the OUT point. The captured audio is spliced into the existing take &mdash; material before and after the punch region is preserved.</p>
<p>To remove markers, right-click and choose <b>Clear markers</b>. Markers are shown as green (IN) and red (OUT) vertical lines with a shaded region between them.</p>
<table width="100%" border="0" cellpadding="3">
<tr><td><code>X / C / V</code></td><td>Cut / Copy / Paste</td></tr>
<tr><td><code>S</code></td><td>Silence selection</td></tr>
<tr><td><code>I</code></td><td>Insert gap at cursor</td></tr>
<tr><td><code>[</code></td><td>Set IN punch marker at selection</td></tr>
<tr><td><code>]</code></td><td>Set OUT punch marker at selection end</td></tr>
<tr><td><code>Ctrl+Z / Ctrl+Y</code></td><td>Undo / Redo</td></tr>
</table>

<h2>Instrument Tab</h2>
<table width="100%" border="1" cellpadding="5">
<tr><th>Type</th><th>Use in TPanar</th></tr>
<tr><td><b>Sampler</b></td><td>Used for audio tracks and takes. Supports imported samples and direct recording into sampler slots.</td></tr>
<tr><td><b>SoundFont</b></td><td>Default instrument type for newly created note tracks.</td></tr>
<tr><td><b>SFZ</b></td><td>Alternative note-track instrument with file-based multi-sample playback.</td></tr>
</table>
<p>Sampler recording in the Instrument tab is separate from transport recording: it records directly into the selected sample slot.</p>

<h2>Mixer and Routing</h2>
<p>Tracks can be routed to Master or auxiliary buses. DSP chains live in the Mixer tab, not in the tracker. Use the mixer for level balance, pan, inserts, and spectrum monitoring.</p>
<p>The master strip contains two group mute buttons:</p>
<ul>
<li><b>Mute Audio / Unmute Audio</b> &mdash; mutes or unmutes all audio tracks at once. Useful for listening to notation tracks in isolation or for rendering a MIDI-only export.</li>
<li><b>Mute Notes / Unmute Notes</b> &mdash; mutes or unmutes all notation tracks at once. Useful for listening to the raw audio takes without playback instruments.</li>
</ul>
<p>Both buttons update to reflect the current group state. Muted tracks are excluded from WAV export.</p>

<h2>Audio Backends</h2>
<p>TPanar supports <b>JACK</b> and <b>OSS</b>. Use JACK when you need live inputs for track recording or sampler recording. OSS is playback-oriented.</p>
<p>After changing the audio backend or reinitializing audio, audio input selectors in the Project tab update automatically to reflect the new channel count.</p>

<h2>File Formats</h2>
<table width="100%" border="0" cellpadding="4">
<tr><td><b>.tp</b></td><td>TPanar project archive</td></tr>
<tr><td><b>.tpi</b></td><td>TPanar sample instrument archive</td></tr>
<tr><td><b>WAV / AIFF / FLAC</b></td><td>Audio samples and takes</td></tr>
<tr><td><b>SF2 / SF3</b></td><td>SoundFont instruments</td></tr>
<tr><td><b>.sfz</b></td><td>SFZ instruments</td></tr>
<tr><td><b>.mid</b></td><td>Standard MIDI File export (Format 1, one track per notation track)</td></tr>
<tr><td><b>.chain</b></td><td>Saved DSP chain</td></tr>
<tr><td><b>.json</b></td><td>Effect preset</td></tr>
</table>

<hr>
<p><i>TPanar &mdash; tracker editing for drum-focused audio and note workflows</i></p>
</body>
</html>
)";

    m_html_win->SetPage(html);
}

} // namespace tpanar_ns
