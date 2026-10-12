# 50-turn live token-efficiency test — 12 October 2026

Completed exactly 50 action/inspection cycles through the installed MCP. Setup,
read-only reconciliation, final measurements and disconnect are outside the turn
count. No reset, checkpoint restore, flee, Repel, or supply consumption occurred.

- Calls: 32 press, 8 move_to, 8 wait_until, 1 train_battle, 1 act.
- All 50 responses used state_format:compact. No flags/inventory were included
  in the decision views; full_state:true returned them on demand.
- 20/50 action results omitted screenshots; 30 included images, mostly for
  partially decoded menus/dialogue or explicit verification. This is an image
  count, not a measured token-saving percentage.
- At the final unchanged overworld position, get_game_state text measured
  6,012 characters full versus 891 compact: 85.2% smaller. Frames differ by the
  read interval. This comparison measures JSON text size, not model tokens or
  total session savings. Across 50 action results, text totaled 69,714 characters.
- Native default taps reported 2 release + 2 hold + 2 release frames. Eight used
  that default, while 24 used a longer 12-frame trailing release for UI settling.
  No automatic confirmation retries or rejected confirmation batches occurred.
- Two visibly missed party-submenu directions were observed at turns6 and8;
  longer trailing release did not eliminate the issue. Do not treat taps as
  universally reliable. The decoded party slot does not identify the switch
  destination/submenu cursor; screenshots remain necessary there.
- move_to completed a 9-tile and a 19-tile segment in one call each. It stopped
  on wild-battle transition (turn16) and the Center door script (turn33). Raw
  boundary travel stopped after one frame on map change (turn30).
- Two battle-menu waits timed out at message acknowledgments (turns17 and24),
  demonstrating that an input-required message is not merely an animation.
- train_battle stopped before input at turn18 with unexpected_battle because
  an ordinary wild battle had type_flags4. The source guard was corrected after
  disconnect, with regression fixtures now using4. The correction has unit
  validation only; it was not loaded during these 50 live turns. Emerald defines
  4 as IS_MASTER, set in non-link battles:
  [primary source](https://github.com/pret/pokeemerald/blob/master/include/constants/battle.h).

After the source correction, all30 MCP unit/protocol tests passed, including
ordinary-wild flag4 acceptance and trainer/link/double/special flag rejection.

Game outcome: Swellow25 moved to lead, defeated wild Oddish13 using Wing Attack,
received144EXP, and took no damage. Traveled from Route117 to Verdanturf Center;
both party members fully healed, all PP restored. No new story objective completed.
Save message frame197109; save_confirmed197130, map1540, (7,4), facing north,
playtime39:29. Final full snapshot frame212214; disconnected after measurements.
The earlier notes described a different live party/location; no inference about
unobserved intervening human progress is made.

Raw action evidence: [token-efficiency-live-20261012.json](test/token-efficiency-live-20261012.json).
