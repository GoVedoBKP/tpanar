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
<li><b>Record audio</b> &mdash; arm one or more audio tracks, enable <b>Rec</b> in the transport, then use <b>Play</b> / <b>Stop</b> for multitrack recording.</li>
<li><b>Analyze audio</b> &mdash; use <b>Analyze</b> on an audio track in the Project tab to populate its linked note track.</li>
<li><b>Quantize notes</b> &mdash; use <b>Quantize</b> on a note track and choose either tracker rows or metronome clicks.</li>
<li><b>Edit and arrange</b> &mdash; refine note timing/velocity in Tracker, then edit audio ranges and apply retrigger stretching in Tracks.</li>
</ol>

<h2>Views</h2>
<table width="100%" border="0" cellpadding="4">
<tr><td valign="top"><b>Project</b></td><td>Track pairing, linked-track routing, drum role, analysis/quantize actions, output bus, and note-track humanization.</td></tr>
<tr><td valign="top"><b>Tracker</b></td><td>Tempo plus note tracks only. Each note track shows note/volume columns, with up to three sub-columns per track.</td></tr>
<tr><td valign="top"><b>Tracks</b></td><td>Timeline for audio takes and note content. Includes track header controls, range editing, group editing, and retrigger stretch actions.</td></tr>
<tr><td valign="top"><b>Instrument</b></td><td>Sampler, SoundFont, and SFZ editing, plus direct sample recording into sampler slots.</td></tr>
<tr><td valign="top"><b>Mixer</b></td><td>Per-track and bus gain, pan, DSP chains, and spectrum analysis.</td></tr>
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
<li><b>Audio track</b> &mdash; sampler-backed take lane, audio input assignment, drum role, linked note track, output bus, and <b>Analyze</b>.</li>
<li><b>Note track</b> &mdash; SoundFont-backed note lane, linked audio track, output bus, humanization controls, and <b>Quantize</b>.</li>
<li><b>Quantize</b> &mdash; snaps note events either to tracker rows or directly to metronome clicks.</li>
<li><b>Analyze</b> &mdash; detects drum hits on the audio take and writes events into the linked note track.</li>
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
<tr><td><code>[</code> / <code>]</code></td><td>Decrease / increase base octave</td></tr>
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
<li>Header controls provide <b>Mute</b>, <b>Solo</b>, <b>Pan</b>, and <b>Volume</b> on audio and note tracks.</li>
<li>Audio tracks also have an <b>R</b> arm button. All armed audio tracks can record at the same time when transport <b>Rec</b> is enabled.</li>
<li>Use <b>Ctrl/Cmd-click</b> for multi-select and <b>Shift-click</b> for range selection across tracks.</li>
<li>Cut, copy, paste, silence, and insert-gap operations apply to all selected tracks.</li>
<li>Right-click an audio track for <b>Retrigger Stretch Selection</b> or <b>Retrigger Stretch Whole Track</b>. This needs a linked note track with quantized notes.</li>
</ul>

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

<h2>Audio Backends</h2>
<p>TPanar supports <b>JACK</b> and <b>OSS</b>. Use JACK when you need live inputs for track recording or sampler recording. OSS is playback-oriented.</p>

<h2>File Formats</h2>
<table width="100%" border="0" cellpadding="4">
<tr><td><b>.tp</b></td><td>TPanar project archive</td></tr>
<tr><td><b>.tpi</b></td><td>TPanar sample instrument archive</td></tr>
<tr><td><b>WAV / AIFF / FLAC</b></td><td>Audio samples and takes</td></tr>
<tr><td><b>SF2 / SF3</b></td><td>SoundFont instruments</td></tr>
<tr><td><b>.sfz</b></td><td>SFZ instruments</td></tr>
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
