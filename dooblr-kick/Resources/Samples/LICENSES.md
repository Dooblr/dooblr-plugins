# dooblr-kick factory samples: sources and licences

All 21 factory samples are **CC0 1.0 Universal (public domain dedication)**, which allows
redistribution inside a commercial or free plug-in with no attribution requirement. Credit is
given here anyway. No samples from D16 Group, Sonic Academy or any commercial pack are used.

Processing applied to every file (scripts kept outside the repo): mixed to mono; onset found at
2 % of peak with 1 ms pre-roll and a raised-cosine fade-in; end at the point where the 10 ms RMS
falls below -60 dB (capped per sample); raised-cosine fade-out of min(50 ms, length / 4);
peak-normalised to -1.0 dBFS; written as 24-bit PCM WAV at the original sample rate (no
resampling). 0 clipped samples in every output file.

## Virtuosity Drums (Austin McMahon, recorded at Virtuosity Musical Instruments, Boston)
Repository: https://github.com/sfzinstruments/virtuosity_drums (licence: CC0-1.0, git tree 9f04cf9a734527edfbb0a4eee1f674e45bbf71bc)

| File | Original file | Original format | Notes |
|---|---|---|---|
| kit_kick_close.wav | Samples/kickmic/kick/kickmic_kick_snoff_vl4_rr2.flac | 48 kHz / 24-bit mono FLAC | kick-mic close kick, 1.6 s, gain -0.6 dB |
| kit_kick_mid.wav | Samples/mid/kick/mid_kick_snoff_vl4_rr2.flac | 48 kHz / 24-bit stereo FLAC | mid-distance kit mic, summed to mono, 1.6 s, gain +11.3 dB |

## VCSL - Versilian Community Sample Library (Versilian Studios LLC)
Repository: https://github.com/sgossner/VCSL (licence: CC0-1.0), commit c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e.
Raw base URL: https://raw.githubusercontent.com/sgossner/VCSL/c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e/

| File | Original file | Original format | Notes |
|---|---|---|---|
| concert_bd_muted.wav | Membranophones/Struck Membranophones/Bass Drum 3 - Legacy/bdrum_muted_fff_rr1.wav | 44.1 kHz / 24-bit | 1.8 s, gain +1.2 dB |
| concert_bd_open.wav | Membranophones/Struck Membranophones/Bass Drum 3 - Legacy/bdrum_fff_rr1.wav | 44.1 kHz / 24-bit | 3.5 s, gain +1.4 dB |
| concert_bd_boom.wav | Membranophones/Struck Membranophones/Bass Drum 2/bassdrum_hit_ff.wav | 44.1 kHz / 16-bit | 4.0 s (of ~9 s natural decay), gain +5.9 dB |
| cajon_bass.wav | Idiophones/Struck Idiophones/Cajon/Cajon_hit1_fff_rr1.wav | 44.1 kHz / 16-bit | 0.667 s, gain +0.3 dB |
| frame_drum_mute.wav | Membranophones/Struck Membranophones/Frame Drum/HDrumL_HitMuted_v3_rr1_Sum.wav | 44.1 kHz / 16-bit | 0.8 s, gain +11.7 dB |
| slapstick.wav | Idiophones/Struck Idiophones/Slapstick/slapstick_rr2.wav | 44.1 kHz / 24-bit | 0.386 s, gain +1.9 dB |
| woodblock.wav | Idiophones/Struck Idiophones/Woodblock/wood_click_ff.wav | 44.1 kHz / 24-bit | 0.3 s, gain +8.1 dB |
| hand_clap.wav | Idiophones/Struck Idiophones/Claps/SoloClap_vl4.wav | 44.1 kHz / 24-bit | 0.202 s, gain -0.3 dB |
| shaker_hit.wav | Idiophones/Struck Idiophones/Shaker, Large/LShaker_Hit_rr1_Mid.wav | 44.1 kHz / 16-bit | 0.4 s, gain +14.8 dB |

### Added in 0.1.1 (VCSL, same commit)

| File | Original file | Original format | Notes |
|---|---|---|---|
| low_tom_mallet.wav | Membranophones/Struck Membranophones/Tom 2/Mallet/TomL_HitM_v4_rr1_Mid.wav | 44.1 kHz / 16-bit stereo | low concert tom, mallet, 1.24 s, gain +7.0 dB |
| timpani_low.wav | Membranophones/Struck Membranophones/Timpani 2/Hit/Timpani1A_hit_v5_rr1_main.wav | 44.1 kHz / 24-bit stereo | largest timpano (~98 Hz), 3.0 s (of ~5 s natural decay), gain +16.0 dB |
| brake_drum_hit.wav | Idiophones/Struck Idiophones/Brake Drum/BrakeDrum1_Hammer_v3_rr1_Mid.wav | 44.1 kHz / 16-bit stereo | hammer on a brake drum, 0.8 s, gain +7.2 dB |
| cabasa_hit.wav | Idiophones/Struck Idiophones/Cabasa/Cabasa1_Hit_rr1_Mid.wav | 44.1 kHz / 16-bit stereo | 0.5 s, gain +20.8 dB |
| darbuka_doum.wav | Membranophones/Struck Membranophones/Darbuka/Darbuka_1_hit_vl2_rr1.wav | 44.1 kHz / 16-bit stereo | low "doum" stroke, 1.0 s, gain +2.9 dB |

## FreePats Synthesizer Percussion (Roberto <roberto@zenvoid.org>, made with Yoshimi and Geonkick) - added in 0.1.1
Page: http://freepats.zenvoid.org/Percussion/electric-percussion.html, archive
SynthesizerPercussion-SFZ-20220718.7z (version 2022-07-18). Licence: CC0 1.0 (LICENSE and readme.txt in the archive).

| File | Original file | Original format | Notes |
|---|---|---|---|
| synth_kick_punch.wav | samples/Kick04.wav | 48 kHz / 24-bit mono | short electronic kick (~93 Hz), 0.226 s, gain -1.0 dB |
| synth_kick_sub.wav | samples/Kick06.wav | 48 kHz / 24-bit mono | deep sub-sweep kick (falls to ~26 Hz), 0.38 s, gain -1.0 dB |
| synth_tom_low.wav | samples/LowTom02-01.wav | 48 kHz / 24-bit mono | electronic low tom (~81 Hz), 0.5 s, gain -1.0 dB |
| synth_click.wav | samples/Claves01.wav | 48 kHz / 24-bit mono | electronic clave click, 53 ms, gain -1.0 dB |
| synth_clap.wav | samples/Clap01.wav | 48 kHz / 24-bit mono | electronic clap / noise burst, 0.72 s, gain -1.0 dB |

Full CC0 text: https://creativecommons.org/publicdomain/zero/1.0/legalcode
