---
title: "feat: Event-driven dynamic camera system"
type: feat
status: active
date: 2026-05-30
deepened: 2026-05-30
---

# feat: Event-driven dynamic camera system

## Overview

Replace the current unit-following camera with an event-driven system that pans to interesting things happening in the fortress — births, new dig designations, combat, trapped creatures, mood starts, construction milestones — rather than cycling through random citizens. Add an "already on screen" check to prevent viewport jitter when multiple events fire near each other (e.g., mass combat).

---

## Problem Frame

The current camera (`camera.cpp`) picks a unit every 2000 ticks (~33 seconds) from a priority list (megabeasts → invaders → random citizens scored by job type) and follows them with `plotinfo->follow_unit`. This creates a passive, unit-centric view that misses fort-expansion moments (new rooms designated), dramatic events (births, moods, trap captures), and causes disorienting jumps during combat because it re-picks a random combatant every cycle.

The user wants the camera to track **events**, not units. A new dig designation should show the expansion area. A birth should pan to the mother. A fight should stay focused on the combat zone without jittering between individual fighters. And if the event target is already visible, the camera shouldn't move at all.

---

## Requirements Trace

- R1. Camera pans to event locations rather than following individual units
- R2. Supported event types: birth, dig designation, construction start, combat, trap capture, strange mood start
- R3. No viewport jump when the event location is already on screen
- R4. Designation events (dig, smooth, etc.) show the area being designated, not the dwarf doing the work
- R5. Combat events hold the camera on the combat zone; don't jitter between individual combatants
- R6. Camera still falls back to interesting citizens when no events are pending
- R7. Existing `config.camera` toggle continues to work as the master on/off switch

---

## Scope Boundaries

- No new config UI — the existing `config.camera` bool is sufficient
- No cinematic transitions or smooth scrolling — `Gui::revealInDwarfmodeMap` is the movement primitive, same as today
- No event filtering UI — all event types are always active
- No recording/replay changes — movie recording is already stubbed out for Steam DF
- Diplomacy/petition camera events deferred — those overlays are dismissed but not yet handled

---

## Context & Research

### Relevant Code and Patterns

- `camera.cpp` / `camera.h`: Current unit-following camera. Updates every 2000 ticks via `events.onupdate_register`. Uses `Gui::revealInDwarfmodeMap(coord, true)` and `plotinfo->follow_unit` for movement.
- `Gui::getViewCoords(x, y, z)` in `ai.cpp:48`: Gets current viewport center. Available for "already on screen" checks.
- `population.cpp:166-200`: `update_citizenlist()` detects new citizens (births that age out of baby status). Runs every 250 ticks.
- `population.cpp:204-208`: Detects babies specifically (`Units::isBaby(u)`) — can detect actual birth moment.
- `population_death.cpp:20`: `deathwatch()` polls death events every tick.
- `population_moods.cpp:20-62`: `update_moods()` detects mood starts and completions. Runs every 250 ticks.
- `plan_construct.cpp:186-242`: `add_task()` calls for room construction — the moment a room transitions from dug-out to "ready to build". Each carries a `room *r` with `r->min`, `r->max`, `r->pos()` coordinates.
- `plan_task.cpp:195`: `want_dig` → `dig_room` promotion — the moment a new area is designated for mining. `digroom(out, t->r)` is called, then the task is deleted.
- `plan_task.cpp:578-627`: `rescue_caged()` — trap capture detection.
- `military.cpp:52-118`: `tag_enemies()` iterates active units for combat targets. Runs every 1200 ticks.
- `room.h:200,239`: Room coordinates: `df::coord min, max` and `pos()` method returning center.
- `event_manager.h:45`: `onupdate_register(descr, ticklimit, initialtickdelay, callback)` — the registration pattern for periodic callbacks.

### Institutional Learnings

- No relevant `docs/solutions/` entries for camera behavior.

---

## Key Technical Decisions

- **Event queues, not event polling**: Events push camera targets into per-tier deques when they occur, rather than the camera polling all possible sources each cycle. This decouples event detection (which already happens in population/plan/military code) from camera consumption.
- **Deque-per-tier, not priority_queue**: Three `std::deque<CameraEvent>` (tier 0 = combat/threats, tier 1 = fort events, tier 2 = citizen interest). Consumption drains tier 0 first, then tier 1, then tier 2. FIFO within each tier is automatic. This avoids the complexity of a `std::priority_queue` with custom comparators and monotonic sequence counters.
- **Coordinate-based targeting, not unit-based**: Events push `df::coord` targets (captured at queue time), not unit IDs. This naturally handles the "show the dig area, not the digger" requirement and avoids the unit-follow jitter problem. The camera uses `Gui::revealInDwarfmodeMap(coord, true)` directly instead of `plotinfo->follow_unit`. Coordinates are snapshots — for moving units (births, moods), the position is captured when the event fires and is not re-fetched at consumption time. This is acceptable because the camera is showing "where the event happened," not tracking the unit afterward.
- **Dwell time via tick counter**: After panning to an event, hold the camera there for a configurable dwell period (default ~1000 ticks, ~17 seconds) before consuming the next event. A higher-priority event (tier 0) interrupts an active dwell from a lower tier (tier 1/2). Same-tier or lower-tier events wait for dwell to expire.
- **On-screen check uses fixed radius from viewport center**: Compare event coord against `Gui::getViewCoords` center using manhattan distance. If the event is within 15 tiles in x and y and on the same z-level, skip the pan. A fixed radius avoids depending on Steam DF's variable viewport sizing. The R5 combat jitter prevention works through this mechanism: when multiple combat events fire near each other, subsequent events within the 15-tile radius are discarded as "already on screen."
- **Invalid coordinates rejected at enqueue time**: `queue_event()` validates that the coord is valid (`pos.isValid()`) before pushing. Events with invalid coords (dead units, off-map positions) are silently dropped.

---

## Open Questions

### Resolved During Planning

- **How to get viewport dimensions for on-screen check?**: Use a fixed 15-tile manhattan distance threshold from `Gui::getViewCoords()` center rather than exact viewport pixel math. This avoids depending on Steam DF's variable viewport sizing. `Gui::getViewCoords` takes output reference parameters `(int32_t &x, int32_t &y, int32_t &z)`.
- **Should combat hold follow_unit or use coord?**: Use coord. Store the coord of the first combat event and hold it for the dwell period. New combat events within the on-screen radius are discarded as "already on screen." This is the mechanism that prevents jitter between individual fighters (R5).
- **Priority queue or deque-per-tier?**: Deque-per-tier. A `std::priority_queue<CameraEvent>` would require a custom comparator with inverted ordering (since `priority_queue` is a max-heap but we want min-priority-first), plus a monotonic sequence counter for FIFO tie-breaking. Three `std::deque<CameraEvent>` achieve the same behavior trivially — drain tier 0 first, then 1, then 2, FIFO within each.
- **Snapshot vs live coordinates for unit events?**: Snapshot at queue time. For births and moods, the position is captured when the event fires. The camera shows "where the event happened," not where the unit is now. This is consistent with room-based events (dig designations, constructions) which are inherently fixed coordinates.

### Deferred to Implementation

- Exact dwell tick count tuning — start with 1000 ticks, adjust based on feel
- Whether `Gui::revealInDwarfmodeMap` correctly handles z-level changes in Steam DF — verify during implementation, fall back to `Gui::setViewCoords` if needed

---

## High-Level Technical Design

> *This illustrates the intended approach and is directional guidance for review, not implementation specification. The implementing agent should treat it as context, not code to reproduce.*

```
Event sources (population.cpp, plan_task.cpp, military.cpp, etc.)
    │
    │  camera.queue_event(tier, coord, description)
    │  (validates coord, pushes to tier's deque)
    ▼
Camera::tiers[3]  (std::deque<CameraEvent> per tier: 0=combat, 1=fort, 2=citizen)
    │
    │  Camera::update() drains tier 0 first, then 1, then 2
    ▼
Dwell check:  is dwell timer active?
    │
    ├─ YES, higher-priority tier has events → interrupt dwell, consume it
    ├─ YES, same/lower tier → return (hold position)
    └─ NO  → continue to on-screen check
             ▼
On-screen check:  manhattan distance(event, viewport center) <= 15 tiles, same z?
    │
    ├─ YES → discard event, try next in tier
    │
    └─ NO  → Gui::revealInDwarfmodeMap(coord, true)
             clear plotinfo->follow_unit to -1
             set dwell_until = current_tick + dwell_ticks
             store last_event_coord for ignore_pause() restore

All tiers empty + dwell expired → citizen fallback (existing scoring logic,
    but using Gui::revealInDwarfmodeMap on citizen position, shorter dwell)
```

---

## Implementation Units

- U1. **Event queue data structure and API**

**Goal:** Add a priority event queue to Camera that other subsystems can push events into.

**Requirements:** R1, R6

**Dependencies:** None

**Files:**
- Modify: `camera.h`
- Modify: `camera.cpp`

**Approach:**
- Define a `CameraEvent` struct: `{ df::coord pos; std::string description; }`.
- Define tier constants: `CAMERA_TIER_COMBAT = 0`, `CAMERA_TIER_FORT = 1`, `CAMERA_TIER_CITIZEN = 2`. Three tiers total.
- Add to Camera class: `std::deque<CameraEvent> tiers[3]` (one deque per tier), `int32_t dwell_until` tick timestamp, `int32_t dwell_tier` (which tier started the current dwell, for priority interruption), `df::coord last_event_coord` (for `ignore_pause()` restore), and a public `void queue_event(int tier, df::coord pos, const std::string & description)` method.
- `queue_event()` validates `pos.isValid()` before pushing. Cap each tier's deque at ~20 entries (drop from back when full).
- Add an `on_screen_radius` constant (default 15 tiles).

**Patterns to follow:**
- Existing Camera member declarations in `camera.h`
- `events.onupdate_register` pattern in `camera.cpp:64-65`

**Test scenarios:**
- Test expectation: none — this unit is pure data structure scaffolding with no behavioral change. Behavior is tested through U2 and U3.

**Verification:**
- Code compiles with the new struct and queue
- `queue_event()` is callable from other translation units via `ai.camera.queue_event(...)`

---

- U2. **Rewrite Camera::update() to consume event queue**

**Goal:** Replace the unit-following logic with event-queue consumption, on-screen check, dwell timer, and citizen fallback.

**Requirements:** R1, R3, R5, R6, R7

**Dependencies:** U1

**Files:**
- Modify: `camera.cpp`

**Approach:**
- `Camera::update()` flow:
  1. If `config.camera` is false, return (unchanged).
  2. Check dwell timer: if active (`*cur_year_tick < dwell_until`), check whether any higher-priority tier than `dwell_tier` has events. If yes, break the dwell and continue to step 3. If no, return — hold current position.
  3. Iterate tiers 0, 1, 2 in order. For the first non-empty tier, pop front. Check if `pos` is within `on_screen_radius` of `Gui::getViewCoords()` center (manhattan distance in x and y, and same z-level). If on-screen, discard and try next event in same tier. If off-screen, pan via `Gui::revealInDwarfmodeMap(pos, true)`, clear `plotinfo->follow_unit` to -1, set `dwell_until` and `dwell_tier`, store `pos` in `last_event_coord`, log the event description, and return.
  4. If all tiers empty or all events on-screen: fall back to citizen-following logic. The existing citizen selection and job-type scoring from the current `update()` (the code that builds threat/conflict/citizen target lists, shuffles, and scores by job type) is preserved here, but uses `Gui::revealInDwarfmodeMap(Units::getPosition(u), true)` instead of `plotinfo->follow_unit`, with a shorter dwell (~500 ticks).
- Remove the external-follow-change guard at `camera.cpp:124-128` (`if (following != plotinfo->follow_unit ...)`). This guard detects when something else sets `plotinfo->follow_unit`, but since the new system no longer sets it, this guard would spuriously fire every cycle.
- Keep the `following` / `following_prev` fields for the citizen fallback path only.
- `Camera::update_tick()`: Remove the `plotinfo->follow_unit` / `follow_item` tracking logic. Keep the `follow_unit` and `follow_item` fields alive for the `config.camera == false` path in `AI::ignore_pause()`, but `update_tick()` only needs to save/restore them when `config.camera` is off. When `config.camera` is on, `update_tick()` becomes a no-op.
- `AI::ignore_pause()`: When `config.camera` is true, restore the viewport to `last_event_coord` if it's valid, otherwise use `Gui::getViewCoords` saved state (`last_good_x/y/z` in `ai.cpp`). The `config.camera == false` path remains unchanged.
- Reduce the update interval from 2000 ticks to 500 ticks so events are consumed more promptly.

**Patterns to follow:**
- Current `Camera::update()` structure in `camera.cpp:117-301`
- `Gui::getViewCoords()` usage in `ai.cpp:48`

**Test scenarios:**
- Happy path: Event queued at off-screen coord → camera pans to that coord, dwell timer starts
- Happy path: Event queued at on-screen coord → camera does not move, event discarded
- Happy path: Multiple events queued → tier 0 drained before tier 1, FIFO within each tier
- Edge case: Dwell timer active, same-tier event arrives → not consumed until dwell expires
- Edge case: Dwell timer active on tier 1 event, tier 0 combat event arrives → dwell interrupted, combat consumed immediately
- Edge case: Queue empty → falls back to citizen-following behavior
- Edge case: All queued events are on-screen → falls through to citizen fallback
- Happy path: Combat event coords → camera holds position for dwell duration, subsequent combat events within 15 tiles discarded as on-screen (R5 jitter prevention)
- Happy path: `config.camera = false` → update returns immediately, no panning
- Integration: `AI::ignore_pause()` called during event dwell → restores viewport to `last_event_coord`
- Integration: `AI::ignore_pause()` called with `config.camera = false` → restores `follow_unit`/`follow_item` as before

**Verification:**
- Camera pans to event coordinates instead of following units
- Repeated combat events near each other do not cause viewport jitter
- Camera falls back to citizen mode when no events pending
- Status string reflects event-driven mode

---

- U3. **Wire event sources into the queue**

**Goal:** Add `camera.queue_event()` calls at each event detection point across the codebase.

**Requirements:** R2, R4

**Dependencies:** U1, U2

**Files:**
- Modify: `population.cpp` (birth detection)
- Modify: `population_moods.cpp` (mood start)
- Modify: `plan_task.cpp` (dig designation promotion, trap capture)
- Modify: `plan_construct.cpp` (construction start)
- Modify: `military.cpp` (combat target detection)

**Approach:**
- **Birth** (`population.cpp`, inside `update_citizenlist()` new-citizen block ~line 184): `ai.camera.queue_event(CAMERA_TIER_FORT, Units::getPosition(u), "new citizen: " + AI::describe_unit(u))`. Coord captured at detection time (snapshot).
- **Dig designation** (`plan_task.cpp`, when `want_dig` is promoted to `dig_room` at ~line 195, right after `digroom(out, t->r)` is called): `ai.camera.queue_event(CAMERA_TIER_FORT, t->r->pos(), "designating: " + AI::describe_room(t->r))`. Uses room center coord, not digger position.
- **Construction start** (`plan_construct.cpp`, inside `construct_room()` at the `add_task(task_type::construct_*)` calls ~lines 192-242): `ai.camera.queue_event(CAMERA_TIER_FORT, r->pos(), "constructing: " + AI::describe_room(r))`. To avoid firing on retries, only queue the event when the room's `bld_id` was previously -1 (first-time construction, not a check_construct retry that re-adds the task).
- **Combat** (`military.cpp`, inside `tag_enemies()` when a squad is ordered to attack ~lines 63-108): `ai.camera.queue_event(CAMERA_TIER_COMBAT, Units::getPosition(u), "combat: " + reason)`. Tier 0 (highest priority).
- **Mood start** (`population_moods.cpp`, when a new mood is detected ~line 59): `ai.camera.queue_event(CAMERA_TIER_FORT, Units::getPosition(u), "strange mood: " + AI::describe_unit(u))`. Coord captured at detection time.
- **Trap capture** (`plan_task.cpp`, inside `rescue_caged()` when a creature is detected in a cage ~line 627): `ai.camera.queue_event(CAMERA_TIER_FORT, t, "creature caged")` using the cage's tile position.

**Patterns to follow:**
- Existing `ai.debug()` and `ai.event()` call patterns at each hook point
- `Units::getPosition(u)` for unit coords, `r->pos()` for room coords

**Test scenarios:**
- Happy path: New citizen detected → birth event queued with citizen's position (snapshot)
- Happy path: Room promoted from want_dig → designation event queued with room center
- Happy path: Squad ordered to attack → combat event queued at tier 0
- Happy path: Mood detected on citizen → mood event queued with citizen's position
- Happy path: First-time construction task created → construction event queued with room center
- Edge case: Construction retry (check_construct re-adds task) → no duplicate event queued
- Edge case: Unit with invalid position (off-map) → event silently dropped by queue_event validation
- Integration: Combat event (tier 0) consumed before pending dig event (tier 1) even if dig was queued first

**Verification:**
- Each event type produces a camera pan to the correct location when off-screen
- Designation events pan to the room center, not to any unit
- Combat events pan to the threat's position
- Construction events fire only on first-time construction, not retries
- Events from all sources appear in the camera debug output

---

- U4. **Update Camera::status() and debug logging**

**Goal:** Update the status string and debug output to reflect event-driven behavior — show current event, queue depth, and dwell state.

**Requirements:** R1

**Dependencies:** U2

**Files:**
- Modify: `camera.cpp`

**Approach:**
- `Camera::status()` should report: currently dwelling on event (with description), queue depth, or "idle (citizen fallback)" when in fallback mode.
- Add `DFAI_DEBUG(camera, ...)` calls when events are consumed, discarded (on-screen), or when falling back to citizens.

**Patterns to follow:**
- Current `Camera::status()` in `camera.cpp:320-345`
- `DFAI_DEBUG(camera, level, ...)` macro pattern used throughout `camera.cpp`

**Test scenarios:**
- Happy path: Camera dwelling on event → status shows event description and "dwelling"
- Happy path: Queue has events → status shows queue depth
- Happy path: No events, citizen fallback → status shows "idle" with citizen info

**Verification:**
- Status output is informative in the web report and debug log
- Debug log entries trace event consumption decisions

---

## System-Wide Impact

- **Interaction graph:** Event sources (population, plan_task, plan_construct, military) push events via `ai.camera.queue_event()`. The primary path is push-based (subsystems → camera). The citizen fallback path still pulls unit data internally (iterating `world->units.active` for scoring), same as the current implementation.
- **Error propagation:** Invalid coordinates are rejected at enqueue time (`pos.isValid()` check in `queue_event()`). No invalid coords reach the consumption path.
- **State lifecycle risks:** Each tier's deque is capped at ~20 entries to prevent unbounded growth from mass combat or large dig operations. Oldest events are dropped when full.
- **API surface parity:** `AI::ignore_pause()` (`camera.cpp:303-318`) must be updated: when `config.camera` is true, restore viewport to `last_event_coord` instead of using `plotinfo->follow_unit`. The `config.camera == false` path (`camera.cpp:305-310`) is unchanged. `Camera::update_tick()` (`camera.cpp:98-115`) must be simplified — the `plotinfo->follow_unit` tracking logic is removed when `config.camera` is on, but the `follow_unit`/`follow_item` fields are kept for the `config.camera == false` path. The external-follow-change guard at `camera.cpp:124-128` is removed since the camera no longer sets `plotinfo->follow_unit`.
- **Unchanged invariants:** `config.camera` master switch behavior unchanged. `check_record_status()` unchanged (already stubbed). Event JSON logging (`ai.event()`) unchanged — camera events are separate from data events.

---

## Risks & Dependencies

| Risk | Mitigation |
|------|------------|
| `Gui::revealInDwarfmodeMap` may not handle z-level transitions smoothly in Steam DF | Fall back to `Gui::setViewCoords` if needed; test during implementation |
| Event flood from mass combat could spam tier 0 deque | Cap each tier's deque at ~20 entries; combat events near each other coalesce via on-screen check |
| `Gui::getViewCoords` may return stale coords immediately after a pan | The dwell timer naturally provides a grace period before the next on-screen check |
| Stale unit coordinates for birth/mood events (unit may move between queue and consumption) | Acceptable — camera shows "where the event happened," consistent with room-based events. Documented as a design choice, not a bug |

---

## Sources & References

- `camera.cpp`, `camera.h`: Current camera implementation
- `event_manager.h`: Callback registration API
- `population.cpp:166-200`: Birth/citizen detection
- `military.cpp:52-118`: Combat target detection
- `plan_task.cpp:195`: Dig designation promotion (`want_dig` → `dig_room`)
- `plan_construct.cpp:186-242`: Construction task creation
- `population_moods.cpp:20-62`: Mood detection
- `plan_task.cpp:578-627`: Trap capture / cage rescue
