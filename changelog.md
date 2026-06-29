# Practice Coach — Changelog

## v1.0.1
- Polished the mod icon — rounded corners and a light outline to match the rest of the index.

## v1.0.0
First public release.

- **Practice-mode HUD** for classic levels: deaths auto-cluster into hotspot sections (no checkpoints to place), a green→red consistency heat strip with a live position cursor, and persistent per-level reach/fail history with recency weighting.
- **Adaptive goal-setting** that reads how you play each level:
  - chain-forward levels follow your **growing edge** — push past where you reliably reach;
  - harder projects you grind backwards switch to **grind mode** — drill your worst wall, with your real progress shown;
  - reliably reaching the end flips to **go for the clear**.
- **Difficulty awareness** via the AREDL demon ladder — advice scales to the level relative to your estimated skill.
- **Coach menu screen**: estimates your skill, lists levels you're closest to clearing, and suggests what to play next. Tap a recommendation to open the level.
- Reliable by design: per-attempt save-to-disk, and correct death-percent tracking even with GD's anti-cheat spike active.
