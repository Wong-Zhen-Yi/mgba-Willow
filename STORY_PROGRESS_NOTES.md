# Emerald story progress and difficulties

## Latest verified snapshot

- 11 October 2026: Saved successfully in Lavaridge Town Pokémon Center after full healing. Explicit “MATT saved the game.” observed; 3 badges, Pokédex 6, playtime 30:25.
- Position: Center 1F, map group 4 / number 5, (7,4), facing north at nurse counter. No active menu/battle. Checkpoint `root-lavaridge-healed-saved-20261011`.
- M13 is complete: Maxie defeated on Mt. Chimney, Magma and Aqua departed, Meteorite recovered.
- Party: Marshtomp 33, Oddish 15, Whismur 6, Plusle 13, Taillow 20. All full HP, no status, PP restored. Inventory: 2 Super Potions, 3 Super Repels, 3 Parlyz Heals; Meteorite in Key Items.
- Next: M14, Flannery and Heat Badge. Gym not attempted. Center exits south; Gym door in town (12,15), approach (12,16). Replenish healing supplies as appropriate.
- User requested a good save point and a handoff note; AI control disconnected and game left at ordinary speed. Do not continue until requested. Older entries below are historical snapshots.

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

### Resumed Route 114 exploration
- Fresh session observation confirmed the rocky Route 114 location. The western stairs led to a small alcove; doubled back and climbed the eastern stairs.
- Difficulty: cliff edges and narrow corridors caused visual stalls. A PokéNav call interrupted travel; cleared it before resuming. The eastern corridor was a side path containing an item, rather than verified progress toward Meteor Falls.
- Used Marshtomp's Rock Smash to clear the corridor rock. At the user's request, collected the visible Poké Ball: the game confirmed “MATT found one PROTEIN!”

### Meteor Falls reached and story advanced
- Defeated Pokémaniac Steve's level 19 Aron with Mud Shot. Defeated Kindler Bernie's level 18 Slugma and Wingull; Rock Tomb missed once, then defeated Wingull.
- Defeated Hiker Lucas's level 18 Geodude and Numel with Water Gun. Picnicker Angelina started a separate battle immediately afterward; Taillow defeated her level 18 Lombre and reached level 20. Marshtomp defeated her Marill with Mud Shot after Taillow's attack was lowered.
- Last battle health: Marshtomp level 30, 81/88 HP; Taillow level 20, 39/47 HP. Other three party members were healthy on the battle party screen. No status conditions shown. Useful Marshtomp PP before final Mud Shot: Mud Shot 13/15, Water Gun 22/25; Rock Tomb last used twice after its 10/10 inspection.
- Difficulty: repeated wild encounters interrupted movement, and cliff faces made short displacements ambiguous. The early alcove and Protein corridor were detours. The successful route moved farther east around the large boulder before south, continued around the eastern cliff, then south past Bernie and west up the stairs beyond Lucas/Angelina. Passed the final trainer without battle and descended two stairs, then headed north to the cave entrance.
- Consulted Route 114 guide and map to resolve navigation: https://www.thonky.com/pokemon-ruby-sapphire-emerald/hoenn-route-114 and https://www.guiasnintendo.com/1_GAMEBOY_ADVANCE/pokemonrubizafiro/pok_rubi_zafiro_SP/28_ruta_114.html . Live screenshots remained the position evidence.
- Verified Meteor Falls story scene: Team Aqua confronted Team Magma, Magma departed with the Meteorite for Mt. Chimney, Archie spoke to MATT and departed, and Professor Cozmo remained. M13 is partial; Maxie victory has not occurred.
- In-game save flow completed at Meteor Falls, three badges, playtime 22:59: overwrite accepted, Saving screen observed, then returned to overworld. The brief success text was not captured. Checkpoint `root-meteor-falls-scene-20261010` saved afterward.
- Current position: Meteor Falls first chamber, west side of the bridge, upper ledge above Cozmo. Next: return east across the bridge and out to Route 114, heal/replenish supplies in Fallarbor, then return via Route 113/111 and Fiery Path to the Route 112 cable car for Mt. Chimney.

### Read-only story reinspection after adapter fix
- Reconnected on 10 October 2026; decoded state now reports supported and available, with atomic screenshot/state alignment. Normalized ROM hash is `4c743011d7f9af0fbc1ef1de7bff157dde718f56`; the previously observed backing hash includes changing clock GPIO registers.
- Still in Meteor Falls first chamber, map group 24/number 0, at (13, 18), facing east. Party HP, levels and moves match the prior handoff.
- Verified flags against pret/pokeemerald `include/constants/flags.h`: first three badge flags set, later badge flags clear; met Archie in Meteor Falls and removed the Route 112 Magma blockers; Mt. Chimney Maxie victory and HM Strength receipt flags clear. Next story objective remains Mt. Chimney, followed by Lavaridge Gym.
- No movement, menu input, battle, save, reset or checkpoint restore performed during this inspection.

### Continuing toward Mt. Chimney
- Left Meteor Falls via the Route 114 exit; fled one wild Zubat. Pre-travel checkpoint: `root-story-mtchimney-start-20261010`.
- Defeated Hiker Lenny's level 18 Geodude with Water Gun and level 18 Machop with Mud Shot, receiving ₽720. Marshtomp reached level 31, 84/91 HP, and declined Take Down to retain its existing coverage/HM moves. Next travel target remains Fallarbor for healing and supplies.
- Returned to Fallarbor and completed Pokémon Center healing. Live state confirms Marshtomp 91/91 HP, Taillow 47/47 HP, all party members healthy and useful PP restored (Mud Shot 15, Water Gun 25, Rock Tomb 10). The decoded planner took long detours around Route 114 cliffs; splitting travel at the two stairways and bridge gave reliable progress. Fled a wild Lombre during the return trip.
- Bought six Super Potions (₽4200) and six Super Repels (₽3000) at Fallarbor Mart. Purchase messages, decoded inventory (item 22 ×6, item 83 ×6) and remaining ₽8464 confirmed.
- Used a Super Repel when leaving Fallarbor eastward. Bird Keeper Coby intercepted us on western Route 113; defeated his level 17 Skarmory with Water Gun and level 19 Swellow with Rock Tomb. Marshtomp remained 91/91 HP, received ₽760, and retains all six Super Potions.
- Crossed Route 113 eastward and reached the Route 111/112 junction. Defeated Cooltrainer Wilton's level 17 Electrike, Wailmer and Makuhita with Mud Shot, receiving ₽816. Marshtomp remained 91/91 HP; Mud Shot now 12/15 PP. Next: west to upper Route 112, south through Fiery Path, then the cable car.
- Used a second Super Repel at Fiery Path's north entrance, traversed the cave southward, and took the Route 112 cable car to Mt. Chimney. No wild battles in the cave. Current position on arrival: Mt. Chimney (map group 24/number 12), (17, 37). Checkpoint `root-mtchimney-before-magma-20261010` created before the Magma battles.


## 11 October 2026 — Mt. Chimney completed; Lavaridge handoff

- Native mgba MCP tools were absent from this chat, although `codex mcp list` showed the server registered. Connected through the existing stdio MCP server using a temporary local SDK client. Did not modify/reload the ROM or restart the emulator.
- Live baseline corrected the older tracker: already on Mt. Chimney (14,16), Marshtomp level 32, all party members full HP, six Super Potions and four Super Repels. One visible Magma NPC's post-battle dialogue did not establish that all summit battles were complete.
- Defeated a remaining grunt (Zubat 20), then Tabitha (Numel 18, Poochyena 20, Zubat 22, Numel 22). Marshtomp took no damage in these fights. Initially called Tabitha Maxie in commentary; corrected after inspecting the battle introduction.
- Maxie was at (13,6); approached via (12,6), facing east. Defeated Mightyena 24, Zubat 24, Camerupt 25. Mightyena lowered Attack; Zubat's confusion caused repeated failed actions. Switched to Taillow and back to reset confusion/stat drops, then Rock Tomb finished Zubat. Three Super Potions used. Camerupt caused a heavy hit; Water Gun finished it after healing. Marshtomp reached level 33, 69/96 HP at victory; Taillow 17/47. Maxie's defeat dialogue and ₽2000 reward observed. M13 marked complete.
- HP text on small screenshots was misread once (reported 19); later decoded state was used for exact HP. Treat historical commentary estimates as superseded by recorded state.
- Maxie departed, Archie thanked us, both teams left. Took Meteorite from the machine; receipt message observed. Descended summit through (17,20), (25,30), (26,37), then Jagged Pass exit (20,41) with a short Down input.
- Used one Super Repel at Jagged Pass entrance. `move_to` cannot jump the many south-facing ledges; used short Down holds and refreshed map/state between ledges. Defeated Hiker Eric (Geodude 20, Baltoy 20) without further damage.
- Picnicker Autumn and Triathlete Julio triggered a double battle while crossing the middle ledges. Shroomish and Magnemite were level 21. Initial target positions were reversed: Mud Shot hit Shroomish, Poisonpowder targeted immune Magnemite. Corrected after Shroomish fainted; Mud Shot defeated Magnemite. Marshtomp was paralyzed and reached 43/96 HP; used one Super Potion to reach 93/96. Oddish grew to level 15 and was 30/41 HP. In the double-party item screen, the initial selection was Marshtomp; an unnecessary Up selected Cancel. Reopened and verified successful healing.
- Camper Ethan intercepted at (12,35); defeated Zigzagoon 20 and Taillow 20. Marshtomp finished 83/96 HP, paralyzed. Ethan registered in PokéNav after post-battle dialogue. Avoid continuing movement while post-battle dialogue is active.
- Jagged Pass exit: move to (14,40), press Down; arrived Route 112 (6,47). Moved to (1,51), then west across map boundary into Lavaridge (18,11). Center approach (9,7), press Up to door (9,6). Center interior entrance (7,8), nurse interaction from (7,4).
- Healing verified by decoded full HP, no status, restored PP for all five members. Final party: Marshtomp 33 (96/96), Oddish 15 (41/41), Whismur 6 (23/23), Plusle 13 (41/41), Taillow 20 (47/47).
- Saved at Lavaridge. At fast emulation, the success text vanished between observations; reduced AI speed to 1× and captured short no-input frame samples through the save flow. Final explicit “MATT saved the game.” screenshot: 3 badges, Pokédex 6, 30:25. Checkpoint `root-lavaridge-healed-saved-20261011` saved afterward.
- User requested stopping at a good save point. Left at nurse counter (7,4), facing north, stable overworld. Next story objective is Flannery/Heat Badge; M12 Strength remains unverified. Two Super Potions and three Super Repels remain. Disconnected MCP; no subsequent gameplay input.

## 11 October 2026 — audio synchronization freeze repair

- MCP and manual controls froze despite `paused:false`; the frame counter stopped. Temporarily selecting uncapped AI speed released the frame thread, then 1x ran normally. Preserved the latest live state in `root-audio-fix-before-restart-20261011` before closing the app cleanly for rebuilding.
- Root cause: after an interrupt, `src/core/thread.c` waited on the raw game audio buffer even while Emerald's normal-speed music used `audioPlaybackBuffer`. The backend consumed only the playback buffer, so raw audio could block the frame that would discard it.
- Corrected the post-interrupt synchronization to use the backend's playback buffer, with the ordinary game buffer as fallback. Added a threaded regression to the Emerald music integration test. The original code failed the frame-advance assertion; the corrected code passed, along with identical music at 8x/16x and 240 accelerated gameplay frames with audio sync enabled. Tests use separate memory-backed game instances.
- Deployed the rebuilt app through the launcher and restored the pre-restart checkpoint. Live MCP checks passed at 1x and 8x across repeated observations and frame actions; directional taps verified movement. Restored the nurse-counter position and facing, returned AI speed to 1x, and disconnected. Party, three badges, and story position preserved; Flannery remains next.

## 11 October 2026 — Lavaridge Gym attempt and team preparation
- Native MCP connected; verified live baseline at Lavaridge Center. Bought six Super Potions for ₽4200 (balance ₽11664).
- Gym door is town (5,15), not (12,15). Cleared Hiker Eli (Numel 23), Kindler Jace (Slugma 23), and Cole/Gerald double battle (Numel/Kecleon 23). Marshtomp reached 34, Oddish 16 and automatically learned Stun Spore into its empty fourth slot. Taillow fainted to Kecleon.
- Gym route: upper entrance northwest hole (8,9); basement south passage trainers; hole (1,14); upper northwest (0,10); basement northwest (0,6); upper (2,3); basement east (7,2); upper southeast (10,6); basement jump south ledge then (12,12); upper Flannery (13,9), interact from (13,10). Warp listings include inactive holes: check live tile behavior. Raw input is needed to leave geyser spawn tiles and enter special holes.
- Flannery's Numel 24, Slugma 24, Camerupt 26 each fell to one Water Gun. Torkoal 29 used Sunny Day, Attract, Body Slam, Overheat, and two Hyper Potions. Repeated infatuation/paralysis prevented attacks. Used five Super Potions and two Parlyz Heals. Switched to Plusle then back; Torkoal reapplied Attract while healing. Entire party fainted; Oddish poisoned Torkoal before fainting, but Whismur could not survive. M14 remains incomplete. No checkpoint restored.
- Blackout fully healed party and returned to Lavaridge outside Center. Marshtomp 34 (99/99), Oddish16 (43/43), Whismur6 (23/23), Plusle13 (41/41), Taillow20 (47/47), all healthy.
- User suggested leveling/catching to optimize and asked about Exp. Share. Verified pret/pokeemerald flags: delivered Steven letter 0xBD (189) is set; received Exp. Share 0x110 (272) is clear. Devon 3F script awards Exp. Share when those conditions hold. M07 now verified. All held item IDs are zero and Exp. Share absent from bag.
- Bought 3 Revives (₽4500) and 3 Super Potions (₽2100) after blackout, leaving ₽980. Current supplies: 6 Super Potions, 3 Revives, 1 Parlyz Heal, 3 Super Repels. Live position currently Lavaridge Mart purchase dialogue. Planned detour: east/south to Mauville, Route117/Verdanturf, Rusturf Tunnel Strength, Rustboro Exp. Share; train Taillow and obtain a Fighting-type backup before retrying Flannery.
