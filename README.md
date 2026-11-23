# Deluge rapid prototypes: mic velocity

**Unofficial.** Not made by, checked by, or supported by Synthstrom Audible or the Deluge community firmware team.
Please don't report problems with this build to either of them.

Prototypes are tested on real hardware, but they are not bug free. Use them at your own risk: back up your SD card
first, and don't rely on a prototype for a gig or a session you can't lose.

## What it is

The community firmware's main branch (commit fa6b048f2, 22 November 2026) with two additions:

- **Mic velocity.** The Deluge's pads have no velocity. This build listens to the internal microphone: about 2.5 ms
  after a pad is pressed, the sound of your finger hitting it sets the note's velocity. The note starts at once, as
  usual, and its velocity is corrected a moment later. Notes you record or tap into the grid keep the measured
  velocity.
- **Brightness follows velocity.** Notes on the grid are lit on a curve from 10% to full, so soft notes look soft.

Mic velocity works only while nothing is plugged into line in or the mic jack, because either one switches the
internal mic off.

## Using it

1. Copy the `.bin` from the Releases page to the root of the SD card, as you would any firmware, and hold Shift
   while switching on to install it.
2. Go to **Settings > Mic velocity > Calibrate** and follow the screen: tap each corner and the centre softly, then
   medium, then hard, three times each. A sidebar pad cancels. If the levels are too close together, it asks for
   that spot again.
3. **Settings > Mic velocity > Curve** sets the velocity for the lightest, soft, hard and hardest taps.
   **Enabled** turns the feature on and off.

The calibration is saved on the card in `SETTINGS/PADVEL.BIN`.

## Known limits

- A loud song playing through the Deluge's own speaker, or a noisy room, can push velocities up.
- Calibration is per Deluge, and it changes if the Deluge sits on a different surface.

## Licence

GPL-3.0, like the firmware it's built on. The source for this build is this branch.

---

# Deluge Community Firmware

## Dear Deluge owners
If you want to start using the community firmware please visit https://delugecommunity.com/ for all relevant details.

## About
The [Deluge](https://synthstrom.com/product/deluge/) from [Synthstrom Audible](https://synthstrom.com/) is a portable sequencer, synthesizer and sampler. Synthstrom Audible decided to [open source](https://synthstrom.com/open/) the firmware application that runs on the Deluge to allow the community to customize and extend the capabilities of the device. 

The hardware is built around a Renesas RZ/A1L processor with an Arm® Cortex®-A9 core running at 400MHz and 3MB of on-chip SRAM. Connected to that is a 64MB SDRAM chip, a PIC for button handling and either a 7-Segment array or an OLED screen. The application is written in C and C++ and some occasional assembly in bare metal style without an operating system.

## Important links
* For acquiring and using the firmware visit the [community firmware website](https://delugecommunity.com/)
* Once you have the firmware, consult our detailed [update guide](https://github.com/SynthstromAudible/DelugeFirmware/wiki/Update-guide) for instructions on how to install it
* To contribute to the project with code, bug reports, suggestions, and feedback see: [How to contribute to Deluge Firmware](https://github.com/SynthstromAudible/DelugeFirmware/blob/main/docs/CONTRIBUTING.md)
* To learn about the toolchain, application, and to start working on the code continue with: [Getting Started with Deluge Development](https://delugecommunity.com/development/deluge/getting_started/)
* Please also visit the [Synthstrom Forum](https://forums.synthstrom.com/) and the community [Discord](https://discord.gg/BnRcyFSgaT) to interact with other users and the developers
* You can donate to the project using the [Synthstrom Patreon](https://www.patreon.com/Synthstrom)
