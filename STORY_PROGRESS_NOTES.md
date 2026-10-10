# Emerald story progress and difficulties

## Latest verified snapshot

- Last parent-chat observation before this side conversation: southern Route 114, at the top of the rocky stairs toward Meteor Falls. This is historical evidence, not a live observation; refresh before input.
- Last verified in-game save: Fallarbor Town, 10 October 2026, playtime 19:43, three badges. The save-success message was observed. Later Route 114 progress is not verified saved.
- Full healing completed at Fallarbor. Last party inspection after Nancy: Marshtomp level 30, 88/88 HP; Taillow level 19, 45/45 HP; Oddish, Whismur, and Plusle healthy. Refresh current HP and PP before continuing.
- No Super Potions remain. Berry stock totals need a fresh inspection. Meteor Falls' theft scene and the Mt. Chimney Maxie battle remain unverified.
- The objective tracker contains the current summary; entries below are chronological snapshots and may describe earlier positions.

## 10 October 2026 — session start and detour

- Connected to the existing Emerald window. Saved checkpoint `root-story-start-20261010` before movement.
- Trainer card confirms MATT has three badges. Marshtomp is level 28; party also contains Oddish, Whismur, Plusle, and Taillow.
- Difficulty: the ROM SHA1 is unsupported by decoded state and pathfinding tools. Navigation uses screenshots and short raw button holds. Camera scrolling, turn delays, and collisions can make movement ambiguous.
- Difficulty: short one-frame menu taps were unreliable. Using eight-frame taps with released frames improves reliability. The `press` tool allows at most 60 release frames; longer waits use `act_sequence`.
- Cleared a PokéNav call from Ricky. Crossed Fiery Path southward; defeated a Numel and Torkoal. Torkoal poisoned Marshtomp. Used one Super Potion; two remain. No Antidote or Repel was visible in the Items pocket.
- Route correction: initially misidentified the northern Fiery Path entrance as Meteor Falls. The existing objectives tracker confirms Meteor Falls has not yet been visited. Going south was a detour; return through Fiery Path, then east to upper Route 111 and west along Route 113 to Fallarbor.
- Position at this earlier point: southern Route 112, approaching the cable-car building. No new main-story milestone was verified.

This log records observed outcomes and problems. The checklist in `POKEMON_EMERALD_OBJECTIVES.md` remains the milestone tracker.

### Berry stop and recovery
- Harvested both Oran trees and both Pecha trees on upper Route 111. Screenshots were read as 2 Oran berries per tree and 3 Pecha berries per tree, but the bag quantities were ambiguous; do not derive current inventory totals from that reading.
- Difficulty: choosing Use on a berry while standing beside empty berry soil planted a Pecha Berry. Moved away from the soil before retrying the treatment.
- Verified party message: Marshtomp was cured of poisoning. Then used a second Super Potion to restore HP from 31/83 to 81/83. One Super Potion remains.
- Route 111 ledges and the blocked rest-house approach delayed travel; use alternating gaps around ledges and head west from the northern berry area toward Route 113.

### Route 111 battle
- Defeated Cooltrainer Brooke: Wingull, Roselia, Numel (all level 17). Rock Tomb handled Wingull, Taillow used Wing Attack against Roselia, Marshtomp used Water Gun against Numel.
- Taillow reached level 19 and learned Double Team over Focus Energy. I initially misread the proposed move as Endeavor; the completed learning message confirms Double Team.
- Taillow became paralyzed; 33/45 HP after leveling. Marshtomp remained 75/83 HP and no longer poisoned.

### Route 113
- Defeated Youngster Jaylen's level 19 Trapinch and Parasol Lady Madeline's level 19 Numel. Marshtomp reached level 29.
- Defeated Twins Tori and Tia's two level 19 Spinda. Oddish fainted; Taillow replaced it. Marshtomp finished the second Spinda with Mud Shot and remains 57/85 HP.
- Difficulty: double-battle target selection requires a separate confirmation. Party navigation also changes when a fainted partner is shown separately on the left. Inspect the highlighted slot before confirming.
- Still heading west on Route 113. Healing at Fallarbor is now the immediate supporting objective.

### Fallarbor reached and healed
- Defeated Ninja Boy Lao's three level 17 Koffing. Marshtomp was poisoned; used the last Super Potion during battle. Last Koffing fainted after attacking (its move was not captured in the final screenshot).
- Defeated Youngster Dillon's level 19 Aron with one Mud Shot at Route 113's western exit.
- Reached Fallarbor Town and completed Pokémon Center healing. Nurse's full-health confirmation observed: poison, paralysis, and Oddish's fainting are now cleared.
- No Super Potions remain. Fallarbor Center is the new healing/blackout return point. Next main-story route is west through Route 114 to Meteor Falls.

### Route 114 and last observation
- Saved successfully in Fallarbor after healing; save screen showed playtime 19:43 and three badges.
- Crossed Route 114's bridge and defeated Picnicker Nancy: Marill and Lombre, both level 18. Marshtomp reached level 30; Taillow used Wing Attack against Lombre.
- Party menu after Nancy confirmed Marshtomp 88/88 HP and Taillow 45/45 HP, with all five party members healthy. Battle switches had not changed the overworld lead order.
- Fled wild encounters while travelling south. Last screenshot showed the player on the rocky plateau above the southern Route 114 stairs toward Meteor Falls. No cave story scene was yet observed.

### Improvements implemented in the side conversation
- Updated the mgba-willow skill with tracker-first route planning, short observation cycles, explicit menu/target confirmation, contextual berry use, supply checks, and evidence-based logging.
- Updated the main objective tracker with the latest historical position, last verified save, party changes, depleted supplies, and remaining M13 prerequisites.
- Read-only source diagnosis: `EmeraldGameState::identify` hashes the loaded cartridge backing store, including patches. The cartridge memory accessor returns the actual ROM size. The observed SHA-1 differs from the one verified retail hash; the cause of the difference remains unknown. No unfamiliar hash was whitelisted and no emulator runtime/build was changed.
- No game-control tools were called for these improvements. The parent chat must refresh the live state before resuming.
