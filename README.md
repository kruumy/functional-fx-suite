# Functional Effects Suite

Free, open-source visual effects plugins for After Effects and OFX hosts.
Each plugin runs fully locally: no settings files, no network, no licensing.

## Effects

| Category | Effects |
|---|---|
| Adjust | `f_badcolors`, `f_duotone`, `f_hotspots`, `f_pseudocolor`, `f_tint`, `f_tritone` |
| Blur | `f_blurmocurves`, `f_zblur`, `f_zdefocus` |
| Distort | `f_distort`, `f_distortblur`, `f_distortchroma`, `f_distortrgb`, `f_heatdistortion`, `f_shake`, `f_warpchroma`, `f_warpfisheye`, `f_warppuddle` |
| Lighting | `f_edgerays`, `f_flicker`, `f_glint`, `f_glow`, `f_glowaura`, `f_glowdark`, `f_glowrings`, `f_lensdirt`, `f_lensflare`, `f_rays`, `f_ultraglow` |
| Render | `f_psykoblobs`, `f_psykostripes` |
| Stylize | `f_ascii`, `f_digitaldamage`, `f_digitize`, `f_edgecolorize`, `f_edgedetect`, `f_filmdamage`, `f_halftone`, `f_halftonecolor`, `f_jpegdamage`, `f_mosaic`, `f_pixelsort`, `f_rollingbar`, `f_scanlines`, `f_tvdamage`, `f_vignette` |
| Time | `f_freezeframe`, `f_jitterframes`, `f_motiondetect`, `f_reverseedits`, `f_timedisplace`, `f_timewarprgb` |

## Build

For the After Effects plugins, download
the After Effects SDK from Adobe and put it in `external/AfterEffectsSDK`; without it only the OFX plugins are built.

## More

- [How to add an effect](docs/How%20to%20add%20an%20effect.md)
- `tools/presets/sapphire_presets.py` converts After Effects presets of S_Shake, S_Flicker and S_BlurMoCurves to
  f_shake, f_flicker and f_blurmocurves.
