# Native PS2 PC Port — 2026-11-17

- Reduced frame preparation, Vulkan submission overhead and repeated rendering work on busy tracks.
- Restored menu and race input separation, Xbox button prompts and the controller layout presets.
  Trigger layouts apply proportional throttle and braking.
- Added optional DualSense adaptive triggers and police radio output with a speaker volume setting.
  The speaker feature uses a USB DualSense with its Windows audio endpoint enabled.
- Improved compatibility with custom localization resources and corrected profile deletion and settings storage.
- Restored the original reflection-map alpha when rendering cars, including the Viper headlight glass.
- Preserved the Wine/Proton XAudio2 PCM audio path.

Use the graphical startup dialog to select game data when adjacent archives or extracted files
are unavailable. Manually selected game data paths are used for the current launch only.
