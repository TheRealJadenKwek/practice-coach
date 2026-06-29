# Practice Coach

An adaptive Practice-Mode coach for Geometry Dash. It watches where you actually die, clusters those deaths into **hotspot sections** automatically (no manual checkpoints needed), remembers every run across sessions, and tells you what to work on next — adapting to how you're currently playing the level.

**It picks a strategy from how you play:**
- On levels you chain forward, it tracks your **growing edge** — "push past where you reliably reach now" — and moves the goal forward as you improve.
- On harder projects you grind backwards (cold runs die near the start while you've practiced far past it), it switches to **grind mode** — "drill your worst wall" — and shows your real progress.
- When you reliably reach the end, it tells you to **go for the clear**.

**HUD (practice mode, classic levels):**
- a gold next-goal line,
- your top least-consistent sections,
- a green→red **heat strip** of the whole level with a live cursor.

**Coach menu:** estimates your skill from the AREDL demon ladder, lists what you're closest to clearing, and suggests what to play next — tap to jump in.

All data is stored locally. Classic (percent-based) levels only; platformer levels are detected and skipped.
