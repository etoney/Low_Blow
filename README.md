# Low Blow

Kick/bass ducker by Castle Danger Music Company. Goes on the bass; the kick comes in on the sidechain. Every kick hit
ducks the bass (only below the crossover in LOWS ONLY mode) with attack / hold / release, a self-adjusting trigger
(SENSITIVITY) and optional lookahead. Part of the Mix System: The Master sets it over Castle Danger Link.

- JUCE 8.0.15, VST3 / AU / Standalone, universal macOS (arm64 + x86_64, macOS 11+) and Windows
- `controls.txt` — every control and the sidechain setup; `readme.txt` — installing by hand
- CI builds on every push to `main`; release zips land on the `downloads-macos` / `downloads-windows` branches
- Local headless checks: `cmake -B build -DLOWBLOW_TESTS_ONLY=ON && cmake --build build --target LowBlowSnapshot && ./build/LowBlowSnapshot_artefacts/Release/LowBlowSnapshot snaps`
