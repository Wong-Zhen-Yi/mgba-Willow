# Current Emerald handoff

Verified 12 October 2026. Obtain a fresh observation before input.

- Latest full snapshot frame212214: Verdanturf Center map6/4 (1540), (7,4),
  facing north, stable overworld. MCP disconnected; no other controller joined.
- Verified in-game save: success message197109, save_confirmed197130, same
  location, playtime39:29, three badges. This supersedes the old Devon save.
- Carried party: Swellow25 leads,67/67HP, status0, Exp. Share182, all PP full;
  Marshtomp35,102/102HP, status0, all PP full. Only these two were carried in the
  live session; earlier five-member notes are historical.
- Supplies: Super Potion6, Revive3, Parlyz Heal1, Super Repel2; no supplies used
  during this test. No captures. Wild Oddish13 defeated, Swellow gained144EXP.
- Next: acquire/train the required League roster, then Flannery/Heat Badge M14.
  Required roster: Swampert, Gardevoir, Breloom, Swellow, Manectric, Flygon.
  See POKEMON_EMERALD_OBJECTIVES.md for verified acquisition progress.
- Winstrate family, Strength and Exp. Share remain previously verified. This
  session did not verify intervening human story progress; Flannery unverified.
- Test: exactly50 action/inspection cycles; compact output active. Final JSON
  text6012 full versus891 compact (85.2% smaller text, not measured tokens).
- Known limitations: two missed party-submenu directional taps; inspect images
  for submenu destinations and settling transitions. train_battle rejected
  wild flag4 in the live run. Source guard corrected after disconnect and unit
  tested; restart MCP and validate before relying on automated wild training.
- Use move_to for live verified routes, allow encounters, wait_until for known
  animations, and inspect each new decision separately. Checkpoints are not saves.

Details: tools/mcp/TOKEN_EFFICIENCY_TEST.md. Replace this at the next milestone.
