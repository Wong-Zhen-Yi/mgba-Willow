# Emerald story progress and difficulties

## Latest verified snapshot

- 17 October 2026: Fortree City, map group 0 / number 4, live frame 1,216,829. Stable overworld at (30,15), facing south; no active menu or battle.
- The game was saved here: “MATT saved the game” and save_confirmed at frame 1,205,470, map 4 at (30,15). The save screen confirmed Fortree City, five badges (Stone, Knuckle, Dynamo, Heat, Balance), Pokédex 9, and playtime 86:47.
- M18 is partial. Steven's Devon Scope receipt on Route 120 was verified earlier; the invisible obstruction at Fortree Gym is still uncleared. Keep M18 unchecked until the obstruction is cleared. Next: approach the obstruction with the Scope, then defeat Winona for M19.
- Party at handoff: Castform Lv25 53/73 HP, paralyzed; Swellow Lv32 46/85 HP, healthy and holding Exp. Share; Swampert Lv38 119/135 HP, healthy.
- Controller was at 4× speed and connected while notes were prepared; it is now disconnected. No battle or menu was open. Older entries below are historical.

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

## Latest handoff — 11 October 2026, stopped at user's request
- Current live location: Rusturf Tunnel, Verdanturf entrance, map24/4 (6148), (29,16), facing north. Stable overworld. Saved checkpoint `root-rusturf-exp-share-detour-20261011`, then disconnected MCP. No new in-game save this session; the older Lavaridge save will not contain later progress.
- **Next session objective: clear the tunnel rock barrier and receive Strength (M12), then leave west for Rustboro and collect the eligible Exp. Share from Mr. Stone on Devon 3F.** After that train backups and consider a Fighting-type before retrying Flannery (M14). User explicitly authorized leveling/catching for team optimization.
- Travel since the Gym loss: Lavaridge -> southern Route112 east ledges -> Route111 south, Rock Smash barrier -> Mauville -> Route117 west -> Verdanturf -> Rusturf Tunnel. Defeated Tyron/Celina double (Sandshrew19/Roselia18), Breeder Isaac's six level11 Pokemon, Triathlete Maria (Doduo17), Bug Maniac Derek (Dustox16/Beautifly16), and Breeder Lydia's six level11 Pokemon. Marshtomp reached35; Taillow reached22 and evolution into Swellow explicitly observed. No captures.
- Verdanturf Center full-health message observed and live party confirms all HP/status/PP restored. Final party: Marshtomp35 102/102, Oddish16 43/43, Whismur6 23/23, Plusle13 41/41, Swellow22 60/60. Held items zero. Supplies: Super Potion6, Revive3, Parlyz Heal1, Super Repel3; Poke Ball14, Great Ball5, Premier Ball1.
- Verdanturf Center is (16,3); (12,3) is the Mart. Tunnel entrance in town (8,1), arriving at tunnel (29,16). Central barrier not yet reached. Destination (25,8) was collision/no-path and advanced no frames. Likely route north via (29,10), (28,5), then west along y4/5, but inspect fresh map before moving. Eastern tunnel trainer at (32,13) not fought.
- Exp. Share eligibility confirmed from live flags and pret/pokeemerald source: FLAG_DELIVERED_STEVEN_LETTER=0xBD set; FLAG_RECEIVED_EXP_SHARE=0x110 clear; FLAG_RECEIVED_HM_STRENGTH=0x6A clear. M07 marked verified. Exp. Share item is not present; key-item numeric272 is a different item, not receipt evidence.
- Navigation lessons: Route117 trainers stand south of the main path and can see along long vertical lines; offset the route when avoiding battles. Warp lists may contain inactive Gym pits, so check tile behavior. Geyser spawn avatar state can require a short raw step before planner movement.
- Do not resume gameplay until the user requests it. Start from current live state; if restarting, latest checkpoint preserves the handoff. No older checkpoint restored this session.

## 11 October 2026 — Rusturf continuation input blocker
- Reconnected at Rusturf Tunnel (29,16). North route requires crossing trainer risk at (29,13); fully healed party justified deliberate exposure. Hiker Mike challenged us.
- Marshtomp35 defeated Mike's first Geodude16 with Water Gun, gained294 EXP, remains102/102 HP; Water Gun24 PP. No other party damage or supply consumption.
- Stopped at “HIKER MIKE is about to use GEODUDE.” in battle, map24/4, (29,13). Single-frame A/B taps repeatedly failed to advance this prompt. Longer confirmation holds are rejected by bridge decision-input validation. Reduced AI speed from8 to1, then2; failures persisted. No emulator reset or checkpoint load.
- Checkpoint root-rusturf-mike-input-blocker-20261011 preserves current battle. No new in-game save. Strength and Exp. Share remain uncollected. Next: resolve confirmation-input blocker, finish Hiker Mike, then continue Strength -> Rustboro Devon3F Exp. Share.

## 11 October 2026 — Strength obtained
- Resumed successfully: single confirmation taps advanced again. Defeated Hiker Mike's remaining Geodude16 and Machop16; reward640 observed. Marshtomp35 remains102/102, Water Gun22 PP. No supplies used.
- Smashed Rusturf Tunnel boulder at (24,5), interacting west from (25,5). Couple thanked us; “Obtained the HM04!” observed at frame147967. M12 verified complete. Next: finish dialogue, west to Rustboro and collect Exp. Share at Devon3F.

## 11 October 2026 — Exp. Share obtained
- Reached Rustboro via Route116, escaped wild Whismur6, Nincada7 and Taillow8 without damage; no supplies used. Hiker Mike defeated earlier for640.
- Devon Corporation3F, map11/2, (17,4), facing south to Mr. Stone (17,5). Delivered-letter thanks observed, then “Obtained the EXP. SHARE!” at frame197195. Strength and Exp. Share detour complete. Next: equip/train Swellow and strengthen backups before Flannery; consider a Fighting-type for Norman.

## Latest handoff — Strength and Exp. Share saved, 11 October 2026
- In-game save success “MATT saved the game” observed frame206295, save_confirmed206316. Rustboro City, Devon3F map11/2 (2818), (17,4), facing south; 3 badges, playtime36:41. Latest handoff frame207480 stable overworld. Checkpoint root-strength-exp-share-saved-20261011; disconnected afterward.
- Inventory confirms Exp. Share182 and HM04 Strength342. Both unequipped/untaught. All five Pokemon fully healthy, no status, held items empty; Marshtomp35 Water Gun22PP. Supplies unchanged. No captures this continuation.
- Next: give Exp. Share to Swellow22, train backups and consider a Fighting-type for Norman, then retry Flannery. Current objectives section in tracker supersedes earlier handoffs and input-blocker notes.

## 11 October 2026 — Flannery preparation resumed
- Swellow22 given Exp. Share: explicit held-item message and held_item_id182 verified at frame246371, Devon3F (17,4). All party healthy; supplies unchanged. Bag submenu directional taps failed; party > Item > Give worked. Continuing training before Flannery (M14).

## 11 October 2026 — user-selected League roster
- User requires Swampert, Gardevoir, Breloom, Swellow, Manectric and Flygon for the Elite Four. Tracker updated with acquisition/evolution checklist. Swellow22 holds Exp. Share; Marshtomp35 remains unevolved; other desired lines not verified acquired.
- Read-only inspection frame323089: map0/31, (29,17), stable overworld, all five party members healthy, three badges. Another MCP controller is actively advancing gameplay; no gameplay inputs sent by this thread. Existing save remains the latest verified save. Ongoing controller should follow this roster when catching/training.

- Winstrate Victor defeated (Taillow16, Zigzagoon16), reward640; Victoria defeated (Roselia17), reward680. Swellow switched in and OHKO Roselia with Wing Attack, reached23 at frame451253, now56/62HP. Marshtomp35 97/102HP. No healing used; one Super Repel consumed crossing Rusturf, two remain. Continuing family series at (13,115), Route111 map26; Vivi dialogue active.

## 11 October 2026 — Winstrate family completed after manual continuation
- User reported the Winstrate house defeated. Fresh live observation at frame511800 corroborates final battle: "Player defeated WINSTRATE VICKY!" frame500936; reward720 frame501127; final invitation dialogue frame501311. Family series complete; do not revisit as an undefeated training opportunity. Victor/Victoria were verified earlier; Vivi completion inferred from reaching and defeating final family member Vicky.
- Live location Route111 map26, (16,115), facing north, Start menu open (MATT selected); no battle. Swellow23 holds Exp. Share182, 56/62HP, Wing Attack32PP. Marshtomp35 97/102HP, Rock Smash11, Mud Shot14, Rock Tomb8, Water Gun21PP. Other members full HP/no status. No new in-game save yet verified after the training.
- Next: continue finding stronger training opportunities for a reliable backup, heal and save preparation, then Flannery/Heat Badge (M14). Winstrate family is complete, not a remaining training target.
- Subsequent human continuation moved into Route111 double battle at (19,119). Last handoff frame521822: Numel18 defeated, poisoned Shroomish18 22/49HP remains; Marshtomp35 50/102HP (no status), Oddish grew17 40/45HP, Swellow23 56/62HP holds Exp. Share. Supplies: Super Potion6, Revive3, Parlyz Heal1, Super Repel2; new item36 present (name not verified here). No new save evidence retained. MCP disconnected to avoid conflicting with ongoing human controls. Resume from fresh observation; finish battle, heal, continue backup training, then M14.

## 12 October 2026 — 50-turn MCP token-efficiency test

- Fresh session differed from earlier notes: Route117 map32 (23,9), Marshtomp35 98/102HP and Swellow25 67/67HP; only these two in the carried party. Three badges. Intervening human progress was not verified by this test.
- Completed exactly50 action/inspection cycles:32 press,8 move_to,8 wait_until,1 train_battle,1 raw boundary action. Before-test checkpoint root-token-test-before-20261012 created; never restored. Compact responses active throughout, with20 action results omitting images. Final overworld full/compact text6012/891 characters (85.2% smaller JSON text; not measured tokens).
- Swellow moved to lead, defeated wild Oddish13 with Wing Attack, gained144EXP without damage. Verified travel segments reached Verdanturf, then Center map6/4 (1540), (7,4), facing north. Full healing and all PP restored: Swellow25 67/67 holds Exp. Share182; Marshtomp35 102/102, both status0. No captures or supplies consumed.
- Save message "MATT saved the game." frame197109; save_confirmed197130 at map1540 (7,4), playtime39:29,3 badges. Final full state frame212214 stable overworld; MCP disconnected after final measurements.
- Failures: two missed party-submenu directional taps; longer release did not guarantee reliability. train_battle rejected the ordinary wild type_flags4 at turn18 before input. Corrected its source guard after disconnect and added fixtures for this flag; correction still needs fresh live validation after MCP restart. Two waits timed out at input-required battle messages, then individual inspected taps advanced them safely.
- Next: required-roster acquisition/training, then Flannery/Heat Badge (M14). CURRENT_HANDOFF.md supersedes old Route111 context. Test evidence in tools/mcp/TOKEN_EFFICIENCY_TEST.md and tools/mcp/test/token-efficiency-live-20261012.json.

12 October 2026: Objective execution resumed from Verdanturf Center. Traveled via Route117/Mauville to Route111, cleared Rock Smash barrier at (18,101). Defeated Picnicker Irene: Shroomish17 with Swellow Wing Attack, Marill17 with Marshtomp Mud Shot. Reward272 observed frame174381; both full HP, Swellow Wing Attack34PP, Marshtomp Mud Shot14PP. Flannery remains outstanding; continuing toward Route112 cable car. No new save yet.

Route111 Camper Travis defeated (Sandshrew18), reward288 frame222809. Route112 Hiker Brice Numel17 and Machop17 defeated with Wing Attack; Swellow reached26 frame293481, still full HP. Marshtomp35 full HP, Mud Shot14PP/Water Gun24PP. No items used.

12 October 2026: Reached Lavaridge after cable car/Jagged Pass. Healing confirmed frame471736. Marshtomp35 now leads102/102HP, Swellow26 69/69HP holding Exp. Share; both full PP/status0. In-game save success534004 and save_confirmed534026 at Center map4/5 (1029), (7,4), playtime41:57, three badges. Irene/Travis/Brice defeated, no supplies used. Next Flannery M14.


## 14 October 2026 — read-only live-state handoff
- Connected to the existing original English Pokémon Emerald session. `emerald_en_v1` reports a stable overworld at map0/12, (9,7), facing south; one controller remains connected.
- At frame95990, the decoder reports four badges from save-block flags: Stone, Knuckle, Dynamo, and Heat. The Heat Badge verifies M14 (Flannery) complete and supersedes older notes that still listed Flannery as next.
- Two party members are healthy: slot0 species ID285, level36, 127/127 HP; slot1 species ID305, level26, 69/69 HP, holding Exp. Share. No save confirmation was retained in this decoder session; this alone does not establish whether the game is saved.
- Next main-story objective: M15 — defeat Norman at Petalburg Gym, earn the Balance Badge, and receive Surf. The shared objective display now shows this goal. No gameplay inputs were sent during this discovery session; MCP remains connected at the user's request.