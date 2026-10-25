# DMX Flame model + Flame effect — design spec

Status: **built** on `jhp-fixes` (2026-10-24). §11 lists where the build differs from the first draft.

Sources (Dropbox, `PROJECTS/Thanksgiving Point/Archive/`):
- Galaxis G-Flame user manual V2.43 — `2022/Technical/User-Manual-G-Flame-V2.43-036.pdf`
- Showven Circle Flamer X-F1800 — `2023/Showven Circle Wave Flamer.pdf`

## 1. Goals

1. A single **DMX Flame** model with a **Fixture** choice: Galaxis G-Flame, Showven Circle Flamer, Custom (JSON).
2. A **Flame** effect whose panel knows what the model on its row can do: pick an angle, a preset or a manual shot in one click.
3. Two repeat modes: **back-to-back refire** for the length of the effect, and **fire on timing marks**.
4. Effects that use a preset with a known length **resize themselves on the timeline** to show that length, unless the user switches that off for the effect.
5. An optional **move early** step so the flame lands on the beat after the nozzle has travelled, shown on the timeline bar.
6. An animated **flame jet** in the layout and house previews.

Non-goals (decided):
- **xLights never arms anything.** It never writes the G-Flame safety channel or the Showven CH6 pump arm. Arming stays with flamesafe / ltcplay.
- No max-burn warnings and no extra re-trigger guard rails beyond what the fixture protocol needs to work (see 5.3).

## 2. What the fixtures do over DMX

### 2.1 Galaxis G-Flame (manual §4.5.2, §16.2)

| Channel | Use | Values |
|---|---|---|
| Flame | Fire | Fires while ≥ 229 (90%). Must drop **< 15** before it can fire again. |
| Safety | Arm (separate address, often shared) | Rising edge into the configured range (30–50 … 70–90%) while Flame < 15. **Not driven by xLights.** |

- No motor, no presets. Burn length = how long Flame is held high. The angle is how it's mounted (up to 45° on bottles, 75°/90°/135°/180° on cartridges), and the preview only uses it for drawing.
- The manual's height table uses valve-open times of 0.3 / 0.5 / 1.0 / 1.5 s. These become the G-Flame **shot length presets**.
- The unit has its own "Max. Flame Duration" setting. xLights doesn't model it.

### 2.2 Showven Circle Flamer X-F1800 (manual §3–6)

| Ch | Use | Values |
|---|---|---|
| CH1 | Angle | 0–255 = −105°…+105°, 128 = straight up. Value = 127.5 + deg × 1.2145. |
| CH2 | Speed (manual moves) | 0 / 255 = max speed, 1–254 = slow → fast |
| CH3 | Fire | ≥ 254 fires. Must drop < 254 before it can fire again. |
| CH4 | Manual burn length | 0 / 255 = permanent (10 s cap), 1–254 = value × 10 ms |
| CH5 | Preset | 0–2 none, 3–255 = presets 1–70 (each covers 2–3 values, see table) |
| CH6 | Pump arm | 0–49 / 201–255 = pressure relief (**E-stop**), 50–200 = armed. **Not part of the model.** |

The 15 named positions (CH1): 1 = −105° (0), 2 = −90° (18), 3 = −75° (36), 4 = −60° (54), 5 = −45° (73), 6 = −30° (91), 7 = −15° (109), **8 = 0° (128)**, 9 = 15° (146), 10 = 30° (165), 11 = 45° (183), 12 = 60° (201), 13 = 75° (219), 14 = 90° (237), 15 = 105° (255).

Motor travel time from position 8 (at max speed): 0 / 50 / 70 / 90 / 110 / 130 / 150 / 170 ms for 0 … 7 positions away. In general, for *k* positions away: `k == 0 ? 0 : 30 + 20k` ms.

The 70 presets (reference lengths from the manual; the CH5 value used is the **middle of the manual's range, rounded down**):

| # | Group | Angles | Length |
|---|---|---|---|
| 1–15 | Single SHORT | positions 1…15, static | 0.19 s |
| 16–30 | Single LONG | positions 1…15, static | 0.56 s |
| 31 / 32 | Step SHORT | 1→15 / 15→1 | 2.57 s |
| 33 / 34 | Step SHORT | 5>8>11 / 11>8>5 | 1.00 s |
| 35 / 36 | Step SHORT | 6>10 / 10>6 | 0.90 s |
| 37 / 38 | Step SHORT | 4>6>8>10>12 / reverse | 1.30 s |
| 39 / 40 | Step SHORT | 8>6>10>4>12 / 8>10>6>12>4 | 1.35 s |
| 41–50 | Step LONG | same patterns as 31–40 | 8.90 / 8.90 / 2.01 / 2.01 / 1.35 / 1.35 / 3.01 / 3.01 / 2.67 / 2.67 s |
| 51 / 52 | Wave middle | 5→11 / 11→5 | 1.99 s |
| 53 / 54 | Big wave | 1→15 / 15→1 | 4.14 s |
| 55 / 56 | Wave middle | 8→1 / 8→15 | 2.18 s |
| 57 / 58 | Wave middle | 1→8 / 15→8 | 2.16 s |
| 59 / 60 | Wave short | 8→11 / 8→5 | 1.12 s |
| 61 / 62 | Wave short | 5→8 / 11→8 | 1.24 s |
| 63 / 64 | Step SHORT | 2>14 / 14>2 | 0.60 s |
| 65 / 66 | Step LONG | 2>14 / 14>2 | 1.38 s |
| 67 / 68 | Step SHORT | 8>14 / 8>2 | 1.85 s |
| 69 / 70 | Step LONG | 8>14 / 8>2 | 4.18 s |

The full CH5 ranges are copied into the built-in table verbatim from the manual (e.g. #1 = 3–5 → 4, #31 = 80–81 → 80, #70 = 179–181 → 180).

To fire a preset (manual §6 example): set CH5, raise CH3 to 255 and **hold it for the whole preset**. Dropping CH3 below 254 stops the flame immediately (confirmed by Jeff from the real unit). To fire again, CH3 must go below 254 for at least one frame and back up. CH1 sets where the nozzle sits **after** the preset.

## 3. The DMX Flame model

New `DisplayAsType::DmxFlame`, class `DmxFlame : DmxModel` (`src-core/models/DMX/DmxFlame.{h,cpp}`), with `DmxGeneral` as the template and the beam-drawing math borrowed from `DmxMovingHead`.

### 3.1 Properties

| Property | Values | Notes |
|---|---|---|
| Fixture | Galaxis G-Flame / Showven Circle Flamer / Custom | Sets the channel count and names. |
| Custom definition | file path | Only for Custom. JSON, see §7. Resolved with `FixFile()`, `ObtainAccessToURL()`. |
| Angle limits (Showven) | min / max position 1–15 | Mirrors the unit's own "Angle Limit" menu. The panel greys out positions outside the limits and render clamps to them. |
| Facing | Audience / Stage | Flips left/right so position 1 is drawn on the correct side. The manual numbers from the audience side. |
| Mount tilt (G-Flame) | −180…180° | Preview drawing only. |
| Flame length | metres (default 8 Showven, 5 G-Flame) | Preview drawing only. |
| Idle values | read-only summary | See 5.4. |

### 3.2 Channels

| Fixture | Channels the model owns | Node names |
|---|---|---|
| G-Flame | 1 | Flame |
| Showven | **5** (CH1–CH5) | Angle, Speed, Fire, Duration, Preset |
| Custom | from JSON | from JSON |

The Showven model stops at CH5, so CH6 belongs to nobody in xLights. **Important:** xLights sends **0 on every channel no model owns** (the frame buffer is zeroed and the whole universe is sent). So if xLights output goes straight to a Showven, CH6 = 0 = pressure relief, and the unit stays depressurised. That's the safe default, but it won't fire unless something downstream (ltcplay / flamesafe) supplies CH6. See open item O1.

### 3.3 Registration touch points

`DisplayAsType.{h,cpp}` (enum, string maps, `IsDmxDisplayType`), `XmlNodeKeys.h` (`DmxFlameType`), `ModelManager::CreateDefaultModel`, `XmlDeserializingModelFactory` (`DeserializeDmxFlame`), `BaseObjectVisitor` / `BaseSerializingVisitor::Visit(DmxFlame&)`, `Model::DetermineClass` ("DMX Special Purpose"), `LayoutPanel` add-DMX popup + icon, `ModelPropertyManager` + new `DmxFlamePropertyAdapter`.

## 4. The Flame effect

New effect `Flame` (`src-core/effects/FlameEffect.{h,cpp}`), JSON metadata `resources/effectmetadata/Flame.json`, and panel `FlamePanel` registered with `RegisterJsonSubclass<FlamePanel>` for the custom button rows (precedent: `ServoPanel`, `StatePanel`). The panel reads the row's model (`SetPanelStatus`) and rebuilds the presets and angles for that fixture. On a model group or a non-flame model it shows a "Put this effect on a DMX Flame model" note.

### 4.1 What a shot is: **Action**

| Action | Showven | G-Flame |
|---|---|---|
| **Preset** | Fires one of the 70 presets (CH5 + CH3 pulse). | — (hidden) |
| **Shot** | Static flame at an angle for a set length (CH1 + CH4 + CH3 pulse). | Flame high for the shot length. |
| **Sweep** | Moves from a start angle to an end angle at a set speed while firing (manual §6 "Wave Firing"). | — (hidden) |

### 4.2 How often: **Repeat**

| Repeat | Behaviour |
|---|---|
| **Once** | One shot at the start of the effect (after the move-early lead, if on). |
| **Back-to-back** | Fires again as soon as the last shot ends, until the effect ends. Cycle = shot length + 1 frame with Fire low (the device needs that frame to re-trigger) + optional extra gap (ms, default 0). A shot that wouldn't finish inside the effect isn't started. |
| **On timing marks** | Pick a timing track. Fires at the start of every mark inside the effect. If a mark comes before the previous shot has ended, the running shot is **cut short**: fire drops low for 1 frame, then the new shot fires (≈1 frame late). The timeline shows the cut. |

### 4.3 Controls

- **Preset picker:** a dropdown grouped like the table in §2.2, plus a button grid of the 15 single-shot presets in the shape of the fan (SHORT/LONG toggle). Each entry shows its length, e.g. "31 · Step 1→15 short · 2.57 s".
- **Angle:** all three ways, as agreed:
  1. **15 position buttons** laid out as the fan (−105° … +105°). One click sets the angle.
  2. **Degrees slider** −105…+105 with a value curve (for Shot and for the Sweep start/end).
  3. **Angle pattern** (text, e.g. `1,8,15,8`). Used by Back-to-back and On timing marks: each shot takes the next angle in the list and loops. Empty = use the single angle.
- **Shot length:** G-Flame quick buttons 0.3 / 0.5 / 1.0 / 1.5 s + custom. Showven manual 10–2540 ms (CH4).
- **Speed** (Showven Sweep): 1–254, with a "max" checkbox.
- **Move early** checkbox (Showven; see 4.5).
- **Fit length to preset** checkbox (see 4.4). Named so it doesn't clash with the existing effect **Lock** on the timeline.

### 4.4 Auto length on the timeline

When **Fit length to preset** is on (the default) and Repeat = Once, choosing a preset or shot length sets the effect's end to:

`start + move-early lead + preset/shot length` (rounded up to the frame)

- Built the same way as the Video panel's "Match Effect To Video Duration" (`EVT_SET_EFFECT_DURATION` → `MainSequencer::SetEffectDuration`), but **with an undo step** (`UndoManager::CaptureEffectToBeMoved`). That path has none today.
- It respects the timeline Lock (a locked effect is never resized) and clamps to the next effect and the end of the sequence. If it gets clamped, the panel says "Shortened: next effect at 1:23.450".
- Dragging the effect's edge by hand turns **Fit length to preset** off for that effect, so it won't snap back.
- Back-to-back and On timing marks use the length you give them, so Fit length doesn't apply.
- **iPad:** the same, via `SequencerViewModel.resizeEffectEdge` (it already registers undo).

### 4.5 Move early

xLights can't render an effect before its start time, so move early works **inside** the effect:

- Turning it on moves the effect's **start** earlier by the lead and leaves the fire time where it was. Lead = travel time from the idle angle (position 8) to the shot's angle, from the manual's table (0–170 ms).
- During the lead the effect writes the target angle with Fire low. Fire goes high when the lead ends.
- For Back-to-back and timing marks with an angle pattern, each shot's own travel is taken out of the gap before it (travel from the previous shot's angle, *k* positions = 30 + 20*k* ms). If a gap is too short to finish the travel, the shot fires on time anyway and the timeline marks it in amber.
- **Timeline:** `FlameEffect::DrawEffectBackground` draws the lead as a hatched band at the front of the bar, plus a tick at each fire point and a shaded band for each burn. So you can see when the nozzle moves, when it fires and when the flame actually ends. This hook is shared with the iPad grid.
- The table is for max speed. In Sweep with a slow speed set, the lead isn't adjusted; the hatch shows only the table value.

## 5. Render rules

### 5.1 Showven, per frame

| State | CH1 | CH2 | CH3 | CH4 | CH5 |
|---|---|---|---|---|---|
| Lead (move early) | target | 0 | 0 | as set | 0 |
| Preset fire | rest angle | 0 | **255 held for the preset's whole length**, then 0 | 0 | preset value, held for the same time |
| Shot fire | angle | 0 | **255 held for the shot length**, then 0 | length / 10 ms | 0 |
| Sweep | start → end | speed | 255 while sweeping | 0 | 0 |

CH3 is held high for as long as the flame should burn, because dropping it stops the flame immediately. If a shot is cut short (timing marks too close, or the effect ends early), CH3 drops at that moment and the flame stops there. CH4 is still set to the shot length as a backstop, so the unit also stops on its own if the signal is lost.

### 5.2 G-Flame, per frame

Flame = 255 for the shot length, then 0. Between shots there is always at least 1 frame at 0.

### 5.3 What the fixture protocol requires

The one-frame-low rule between shots (Showven CH3 < 254, G-Flame < 15) is built into the repeat modes, because without it a refire never registers. This isn't the optional "re-trigger guard" you declined; nothing else is enforced.

### 5.4 Safe idle values

When no Flame effect covers a frame, the model writes **CH1 = 128 (straight up), CH2–CH5 = 0** and **G-Flame Flame = 0**. Showven CH1 needs this because a plain zeroed frame would be 0 = −105° (far left). This needs a model-level "default when no effect" hook. `EnableFixedChannels` forces values all the time, so it doesn't fit as is. To be checked during build.

## 6. Preview — animated flame jet

- `DmxFlame::DrawModel` draws the nozzle, plus a plume when Fire is high: a cone that tapers and fades orange → yellow → transparent, flickering a little.
  - For the Showven, the plume points at the CH1 angle.
  - For the G-Flame, it points along the mount tilt.
  - Its length comes from the Flame length property; SHORT vs LONG preset scales it.
- **Presets match exactly, playing or scrubbing**, the same as every other model. The preview loads each frame's values from the fully rendered show (`_seqData[frame]`, `tabSequencer.cpp:1061` and `:2899`, plus the house preview in `ModelPreview.cpp:1280`). Before drawing frame *f*, the model is also handed the rendered show and *f* (a new optional `Model::SetPreviewFrameContext(const SequenceData*, frame)`, a no-op for every other model). `DmxFlame` looks back through its own CH3/CH5 bytes in earlier frames (at most the longest preset, 8.9 s) to find where the current shot started. Elapsed time = (*f* − start frame) × frame time, which picks the exact point on the preset's step or wave path from the table. It holds no state between frames, so any frame, in any order, draws the same picture.
- If the context isn't available (e.g. a dialog preview that shows only one frame), it falls back to drawing from the current channel values alone.
- The previews draw through the same core functions on desktop and iPad. The iPad's preview call sites need the same one-line context hand-off.

## 7. Custom fixture JSON

Loaded from the model's Custom definition file. Built-in fixtures use the same format, shipped in `resources/flamefixtures/` (new resources folder, which needs adding to the Xcode resources phase, `CMakeLists.txt`, `Makefile`, the `.iss` installer and `BuildMSIX.ps1`).

```json
{
  "name": "Showven Circle Flamer X-F1800",
  "channels": ["Angle", "Speed", "Fire", "Duration", "Preset"],
  "fire":     { "channel": 3, "on": 255, "off": 0, "rearmBelow": 254 },
  "angle":    { "channel": 1, "minDeg": -105, "maxDeg": 105, "centerValue": 128,
                "positions": [ {"n":1,"deg":-105,"value":0}, "…", {"n":15,"deg":105,"value":255} ],
                "travelMs": [0, 50, 70, 90, 110, 130, 150, 170] },
  "speed":    { "channel": 2, "max": 0 },
  "duration": { "channel": 4, "msPerStep": 10, "min": 1, "max": 254 },
  "preset":   { "channel": 5, "none": 0,
                "list": [ {"n":31,"name":"Step 1→15 short","group":"Step SHORT","value":80,
                           "lengthMs":2570,"path":{"type":"step","positions":[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15]},"size":"short"} ] },
  "idle":     { "1": 128 },
  "shotPresetsMs": []
}
```

The G-Flame file has one channel, `fire.on = 255`, `rearmBelow = 15`, no angle/preset blocks, and `shotPresetsMs: [300, 500, 1000, 1500]`. Bad JSON is rejected with a message in the model's properties. It's parsed with nlohmann using non-throwing number parsing (`strtol`/`strtod`).

## 8. Files

**New:**
- `src-core/models/DMX/DmxFlame.{h,cpp}`, `src-core/models/DMX/FlameFixture.{h,cpp}` (fixture definition + JSON loader)
- `src-core/effects/FlameEffect.{h,cpp}`, `include/flame-{16,24,32,48,64}.xpm`, `resources/effectmetadata/Flame.json`
- `src-ui-wx/effectpanels/FlamePanel.{h,cpp}`
- `src-ui-wx/modelproperties/adapters/DmxFlamePropertyAdapter.{h,cpp}`
- `resources/flamefixtures/{GalaxisGFlame,ShowvenCircleFlamer}.json`
- iPad: the Flame custom rows in `EffectCustomRows*.swift`, the add-model entry, and `BuildDmxFlameProps` + its setters in `LayoutEditorView.swift`.

**Edited:** the registration points in §3.3 and §4, `EffectManager.{h,cpp}`, `EffectPanelManager.cpp`, `MainSequencer` (undoable resize), `xLights.cbp`, `xLights.vcxproj(.filters)`, `README.txt`, `plans/platform-parity/04-effects-catalog.md` + `06-layout-models.md`.

**iPad parity:** State A (in the same change). The core is shared; the model add/properties and the panel's custom rows get SwiftUI equivalents.

## 9. Testing

- **Unit-style checks** in a headless render: preset *n* → CH5 value inside the manual's range for all 70; angle position → CH1 matches the manual table; travel lead matches the table.
- **Headless render** of a test `.xsq` with one effect per mode, then `XL_FSEQCMP_DUMPCH=<ch> xLights --fseqcmp …` to dump each channel. Check: CH3 stays high for exactly the shot/preset length, there is ≥ 1 low frame between refires, CH1 = 128 when idle, and nothing ever lands on the CH6 address.
- **Builds:** macOS Debug + `xLights-iPadLib`.
- **Preview:** scrub to random frames inside long presets (e.g. #41, 8.9 s) and check the picture matches the same frame during playback.
- **On the real units, with Andy, cold (no gas, Showven in TEST MODE):** check the angles, preset numbers and that refire works at the show frame rate.

## 10. Open items

- **O1 — Addresses (Jeff to supply; not blocking).** xLights sends 0 on CH6, which depressurises a Showven, so ltcplay/flamesafe must supply CH6 downstream. A Showven needs 5 contiguous slots, with its pump arm on the 6th; the rev 6 panel table (fire 431, 432 / arm 403) doesn't fit that yet. The model takes any start address, so nothing in the build depends on this.
- **O2 — Decided:** marks closer than a shot cut the running shot short and fire again (§4.2).
- **O3 — Decided:** Fire is held high for the whole shot/preset; dropping it stops the flame (§5.1).
- **O4 — Decided:** the preview matches exactly when scrubbing, worked out from the rendered frames (§6).

## 11. Changes made while building

- **Sweep** ramps CH1 frame by frame at max speed (CH2 = 0) instead of using the unit's speed channel. The manual doesn't say how long a speed-controlled move takes, so the old way could never be timed or previewed exactly.
- **Mount tilt** was dropped: rotate the model in the layout instead.
- **Built-in fixtures** are compiled in (`FlameFixtures::GFlame()` / `ShowvenCircle()` in `src-core/models/DMX/FlameFixture.cpp`), so there's no new resources folder to install. Custom fixtures use the same JSON format; `plans/flame-fixture-example.json` is a working example. `fire.threshold` (the value at which the unit counts fire as on) was added; G-Flame 229, Showven 254.
- **Timing marks too close:** the running shot ends one frame before the next mark, so the next shot fires **on** its mark (not one frame late).
- **Move early** stores how far it has shifted the effect's start in a hidden setting, `E_TEXTCTRL_Flame_LeadMs`, so changing the angle or preset keeps the fire time fixed. It applies to Once and Back-to-back. With timing marks, the move uses the gap before each mark, and the lead band turns amber when the gap is too short.
- **Fit length** is undoable (new `MainSequencer::SetEffectTimes`). It respects the timeline Lock, and it is clamped to the neighbouring effects and the end of the sequence.
- **Safe idle** is written by the render engine: when a render range is cleared, and on any frame where a flame model has no effect on any layer (`RenderEngine.cpp`, `DmxFlame::WriteIdleValues`).
