# CollingOS 3DS

Windows-inspired desktop environment for Nintendo 3DS, written in C with devkitARM + libctru. Output: `CollingOS.3dsx`.

## Build
Local: install devkitPro (3ds-dev), then `dkp-pacman -S 3ds-curl 3ds-mbedtls 3ds-zlib`, download https://curl.se/ca/cacert.pem into `romfs/`, run `make`.
GitHub: push the repo; Actions builds it; download the artifact `CollingOS-3DS`.

## Install
Copy `CollingOS.3dsx` to `sdmc:/3ds/CollingOS.3dsx`, start it from the Homebrew Launcher.
Music needs DSP firmware (`sdmc:/3ds/dspfirm.cdc`, made by the DSP1 app).

## Store catalog
Edit `catalog/store.txt` (format described at its top) and push it; the default catalog URL points to
`https://raw.githubusercontent.com/plszynon/collingos-3ds/main/catalog/store.txt` (change in Settings).

## Known limits
Apps run full-screen one at a time. Cannot launch other .3dsx files (libctru has no chain-load API). Images max ~2 MP. Audio: 16-bit PCM WAV and MP3. Downloads are sequential.
