# Practice Coach

A [Geode](https://geode-sdk.org) mod that turns Geometry Dash's Practice Mode into a data-driven coach. It records every attempt, learns where you actually struggle, and tells you what to work on next — adapting to how you're currently playing each level.

Targets **GD 2.2081 / Geode 5.7.x** on Windows.

## What it does

**In practice mode (classic levels):**
- **Finds your walls automatically.** Every death is clustered into hotspot sections — no checkpoints to place, no setup. A green→red heat strip along the bottom shows your consistency across the whole level, with a live cursor tracking your position.
- **Adaptive goal-setting.** The coach reads how you play a level and picks the right strategy:
  - On levels you chain forward, it follows your **growing edge** — "push past where you reliably reach now" — and moves the goal forward as you improve.
  - On harder projects you grind backwards (cold runs die near the start while you've practiced far past it), it switches to **grind mode** — "drill your worst wall" — and shows your real progress instead of a discouraging cold-reach number.
  - When you reliably reach the end, it tells you to **go for the clear**.
- **Persistent history.** Reach/fail stats are saved per level across sessions, with recency weighting so the coach tracks your *current* skill, not ancient runs.

**On the main menu:**
- A **Coach** screen that estimates your overall skill from the [AREDL](https://aredl.net) demon ladder, lists which levels you're closest to clearing, and suggests what to play next. Tap a recommendation to jump straight into the level.

## Settings

| Setting | Default | Meaning |
|---|---|---|
| Enable HUD | on | Show the overlay in practice mode |
| Show heat-strip mini-map | on | Draw the bottom consistency strip |
| Attempts kept per level | 300 | Rolling history size (lifetime totals never lost) |
| Min reaches to trust a section | 8 | Reaches before a hotspot is acted on |
| Full runs before recommending | 8 | Cold-start: runs to log before naming a target |
| Section reliable at pass-rate | 0.85 | Pass-rate at which a section counts as solid |
| Recency weighting | 0.98 | How fast old runs fade (lower = recent runs dominate) |
| Show full-clear odds | on | Show estimated clear chance / cold-proven progress |

## Notes

- **Classic (percent) levels only.** Platformer levels are detected and skipped.
- **All data is local** — it lives in the mod's `saved.json` and is written to disk after every attempt. Nothing leaves your machine except an anonymous fetch of the public AREDL demon-list API (used only to gauge level difficulty).
- Renders its own HUD node and never touches the vanilla percentage label or progress bar, so it co-exists with other HUD mods.

## Build from source

You need the **Geode SDK + CLI** and a C++ toolchain (on Windows: Visual Studio 2022 Build Tools with the "Desktop development with C++" workload, which bundles MSVC + CMake + Ninja).

```sh
geode build
```

This produces `build/jaden.practice-coach.geode` and, with a configured profile, auto-installs it to your game's `geode/mods/` folder.

## License

MIT — see [LICENSE](LICENSE).
