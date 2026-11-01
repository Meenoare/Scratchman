SCRATCH LAB 0.3 — SOURCE PROJECT, NOT PREBUILT PLUGINS

BUILD USING GITHUB ACTIONS
1. Create a GitHub repository and extract this archive.
2. Commit the CONTENTS of ScratchLab/ to repository root, including the hidden
   .github/workflows/build.yml. Do not upload only the ZIP to the repository.
3. Actions > Build Scratch Lab > Run workflow (or push).
4. After jobs succeed download Artifacts ScratchLab-Windows and ScratchLab-macOS.
   Extract the artifact ZIP, then the plugin ZIP inside it.
The workflow is configured but HAS NOT run from this chat. Internet access,
GitHub Actions access/minutes and a GitHub account are required.

LOCAL BUILD
Windows: Visual Studio 2022 Desktop C++ tools, CMake >=3.22 and Git:
cmake -S . -B build -A x64
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
Mac: Xcode command line tools, CMake >=3.22 and Git:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build build --config Release --parallel 2
ctest --test-dir build --output-on-failure
Output: build/ScratchLab_artefacts/Release/.

INSTALL AFTER BUILDING
Windows x64: entire Scratch Lab.vst3 bundle to C:\Program Files\Common Files\VST3
Mac VST3: ~/Library/Audio/Plug-Ins/VST3
Mac AU: ~/Library/Audio/Plug-Ins/Components
Logic Pro uses AU, NOT VST3. Other named hosts need supported VST3 versions.
Rescan plugins and add as MIDI/instrument track. This is a sampler/instrument,
not an effect processing incoming audio. No VST2/AAX/32-bit/Windows ARM64 build.
macOS outputs are ad-hoc signed, NOT Developer ID signed or notarized.
For public distribution proper signing/notarization is needed. Do not globally
turn off operating-system security. DAW compatibility has NOT been certified.

FEATURES
20 synthetic gesture patterns, slice selection, equal/transient slicing,
manual boundaries, MIDI and host-tempo duration.
MIDI note NUMBERS (octave names vary by host):
36-55 = gestures in pad order; 60 = clean selected slice;
61 = clean entire phrase; 72-103 = select slices 1-32, selection only.
Alternate clean and scratch notes in the piano roll; clean playback isn't random.
Clean ignores scratch speed/tempo/span; output gain and brief de-click fades remain.
Repeat while held off = one-shot, on = stops at note-off.
Q W E R T Y U I O A S D F G H J K L Z X = gestures when editor focused.
Space = clean slice; P = clean phrase; 1-8 = slices; Esc = stop.
MIDI is preferable because DAWs may intercept keyboard shortcuts.
Load audio or Demo; Shift-click waveform to split; fields adjust boundaries.

LIMITATIONS
Synthetic approximations, not recorded gestures by a professional DJ.
One voice; linear resampling may alias at extreme speed. Hard retriggers may click.
Host BPM controls duration, not quantized starts or transport chasing.
Samples embedded in state can enlarge DAW sessions. Replaced samples remain in
RAM until instance destruction to avoid audio-thread sample-buffer deallocation.
Repeated long-file loading increases memory use. No production-readiness claim.
No Windows/macOS binaries or DAW validation results are included here.

LICENSE
Project source: AGPL-3.0-or-later. JUCE 8.0.6 is fetched during configure;
its separate AGPL/commercial terms and third-party notices apply. Review JUCE's
LICENSE.md and licensing terms before proprietary distribution. No proprietary
JUCE licence, Apple certificates or credentials included.
