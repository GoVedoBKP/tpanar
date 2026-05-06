bin_PROGRAMS = tpanar

tpanar_SOURCES = \
    main.cpp \
    core/engine.cpp \
    core/config_manager.cpp \
    core/key_bindings.cpp \
    core/masterbus.cpp \
    core/metronome.cpp \
    core/transport.cpp \
    core/undo_stack.cpp \
    core/engine_save.cpp \
    audio/jack_backend.cpp \
    audio/oss_backend.cpp \
    audio/resampler.cpp \
    audio/sample_buffer.cpp \
    audio/sample_voice.cpp \
    audio/timestretch.cpp \
    dsp/dsp_chain.cpp \
    dsp/reference_analyzer.cpp \
    dsp/reference_matcher.cpp \
    edit/cmd_track_cut.cpp \
    edit/cmd_track_copy.cpp \
    edit/cmd_track_paste.cpp \
    edit/cmd_track_silence.cpp \
    edit/cmd_track_insert_silence.cpp \
    gui/wx_main_window.cpp \
    gui/wx_project_panel.cpp \
    gui/wx_detached_frame.cpp \
    gui/wx_tracker_panel.cpp \
    gui/wx_tracker_view.cpp \
    gui/wx_tracks_panel.cpp \
    gui/wx_instrument_panel.cpp \
    gui/wx_mixer_panel.cpp \
    gui/wx_settings_panel.cpp \
    gui/wx_help_panel.cpp \
    gui/wx_vu_meter.cpp \
    gui/wx_analog_vu_meter.cpp \
    gui/wx_waveform_view.cpp \
    gui/wx_spectral_view.cpp \
    gui/wx_transportbar.cpp \
    gui/model/mixer_model.cpp \
    gui/theme.cpp \
    instrument/instrument.cpp \
    instrument/instrument_rack.cpp \
    instrument/sample_instrument.cpp \
    instrument/soundfont_instrument.cpp \
    instrument/sfz_instrument.cpp \
    instrument/dssi_instrument.cpp \
    instrument/lv2_instrument.cpp \
    instrument/midi_instrument.cpp \
    io/audio_file.cpp \
    io/project_archive.cpp \
    io/song_serializer.cpp \
    midi/midi_input.cpp \
    mixer/track.cpp \
    mixer/track_clipboard.cpp \
    mixer/mixer_bus.cpp \
    sequencer/automation.cpp \
    sequencer/pattern.cpp \
    sequencer/sequencer.cpp \
    sequencer/timing.cpp \
    analysis/fft_analyzer.cpp

tpanar_CXXFLAGS = \
    $(JACK_CFLAGS) \
    $(WXWIDGETS_CFLAGS) \
    $(SNDFILE_CFLAGS) \
    $(SAMPLERATE_CFLAGS) \
    $(SNDTOUCH_CFLAGS) \
    $(NLOHMANN_JSON_CFLAGS) \
    $(FFTW3_CFLAGS) \
    $(LIBARCHIVE_CFLAGS) \
    $(FLUIDSYNTH_CFLAGS) \
    $(ALSA_CFLAGS)

tpanar_LDADD = \
    $(JACK_LIBS) \
    $(WXWIDGETS_LIBS) \
    $(SNDFILE_LIBS) \
    $(SAMPLERATE_LIBS) \
    $(SNDTOUCH_LIBS) \
    $(FFTW3_LIBS) \
    $(LIBARCHIVE_LIBS) \
    $(FLUIDSYNTH_LIBS) \
    $(ALSA_LIBS)
