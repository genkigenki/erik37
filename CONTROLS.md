# Erik37 1.3: controls

## SMK-37

| Control | Job |
| --- | --- |
| Keys | the piano (velocity through the touch curve), C3..C6; OCTAVE- / OCTAVE+ by octaves |
| K1..K4 | the four knobs at the bottom of the screen (pages below) |
| K5 | MASTER (the volume) |
| K6..K8 | MIDI CC 16, 17, 18 (channel 1): a value 0..127 that starts at 64, 2 a detent; no sound setting |
| < / > | the pages: play, TONE & TOUCH, ROOM, PAD LAYER, PEDALS & PADS, ABOUT |
| PATCH | the play screen; held: the audio load and the voices in the header |
| Pads 1-4 | the voices CONCERT, STUDIO, VINTAGE, JAZZ |
| Pads 5-16 | USER 1-12: a tap recalls, held a second saves |
| SEQ REC | sustain on / off (a toggle; its light and the header's SUSTAIN ON while on) |
| CHORD | sostenuto (held while held; a tap latches) |
| SCALE | una corda (held while held; a tap latches) |
| PEDAL jack | sustain, half pedal with a continuous pedal |
| Mod wheel | nothing, or sustain (PEDALS & PADS K2) |
| Pitch wheel | pitch bend, +-2 semitones |
| SEQ PLAY | the metronome |
| BT | save the settings now |
| FX | every note let go |
| Faders | MIDI CC 80, 81, 82, 83 (channel 1): the fader's position; no sound setting |
| OCTAVE- + OCTAVE+ held 5 s | update mode |

| Page | K1 | K2 | K3 | K4 |
| --- | --- | --- | --- | --- |
| play | tone | room | reverb | tempo |
| TONE & TOUCH | touch curve | brilliance | resonance (past 0: off) | release noise (off .. 100, 40 at first) |
| ROOM | room | size | amount | width |
| PAD LAYER | pad (0 off .. 100, off at first) | attack 0.02 .. 2 s | release 0.1 .. 4 s | tone 300 Hz .. 8 kHz |
| PEDALS & PADS | una corda depth | mod wheel: off / sustain | pedal jack: auto / open / closed | pad light 45 / 60 / 80 / 100 % |
| ABOUT | audio load | voices | most voices | xruns |

Pedal jack: **auto** learns the rest position once the reading has been steady for about 0.2 s (turn K3 back to
auto with the pedal up to learn it again); **open** for a pedal that is open at rest (most), **closed** for one that
is closed at rest.

Pad lights: voices dim amber, the chosen one gold; presets pale (empty), brighter (filled), ivory (chosen).

MIDI CC out: a knob's value starts at 64 at power-on and is sent on every detent; a fader's first reading is not
sent, a move is (a change of one step once it has held for 40 ms, so a fader resting between two steps does not
stream). Nothing listens to these numbers inside Erik37, so a DAW echoing them back changes nothing.

PAD LAYER: a soft pad under every note, held by the key and the pedals like the piano, into the same room. Up to 8 pad
notes; a ninth takes the faintest. A user preset keeps it (1.2's presets recall with it off).

## FM-1

| FM-1 | Job |
| --- | --- |
| KNOB 1..4 | the page's knobs (PAD LAYER as on the SMK-37; PEDALS: una corda depth only) |
| SELECT | the pages |
| ALGORITHM | the voices |
| PRESETS | USER 1-12, recalled as it lands on them |
| SAVE | tap: save the settings; held a second: the sound into the user preset PRESETS points at |
| REC / ENV / SEL | sustain (a toggle) / sostenuto / una corda |
| PLAY, FX, HOME, OCT | as SEQ PLAY, FX, PATCH and OCTAVE on the SMK-37; MASTER the volume |
