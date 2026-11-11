# Online mode navigation

2026-11-09 full-match retry: Player1 (26456) and Player2 (9692) completed all three periods and reached matching END OF GAME screens, San Jose 2-1 Pittsburgh (shots 12-10). Both intermissions cleared with Cross/Resume Game. The host sent GameManager AdvanceGameState GSTA=4 (POST_GAME) at 09:41:51 UTC and both players received state notifications. Both sent GameReporting 28/1; server result processing/persistent stats are not implemented. Evidence: artifacts/otp-complete-player1.png, otp-complete-player2.png and otp-complete-blaze.log. Left both final screens open. This verifies one complete same-PC two-player match; it does not explain the earlier intermission stall.

2026-11-09 validation: both players reached the same Online Team Play match (San Jose vs Pittsburgh), controlling opposing centers at the opening faceoff. Fixed the Blaze OTP statistics scope/notification responses and supplied the OTP onboarding configuration. No emulator changes. Details: sibling blaze compatibility pack Research/online-team-play.md. External window captures: artifacts/otp-player1.png and artifacts/otp-player2.png. Full match completion and stats persistence are not yet verified.

From the initial main-menu selection:
- Online Team Play: Right four times, then Down, then Cross.
- EASHL: Right four times, then Cross. Work on this after Online Team Play.

2026-11-09: Confirming Online Team Play displayed Sign In Required. The player launcher targets the host's LAN address while the services had been started on loopback only. Restore the existing LAN host configuration before investigating mode-specific requests.

Services restored to the existing LAN configuration; all five ports accepted connections. A detached launcher run left runtime settings offline; keeping launch_multiplayer_player.ps1 attached retained online settings and produced a successful NHL17Probe identity login and Blaze authentication. Online Team Play then displayed Failed to retrieve data. The final request before that error was Stats.GetStatGroup (7/4), NAME=OtpSummary, message 23. Its response currently uses the generic five-stat descriptor list in build_stat_group_payload. This is a suspect, not a verified root cause. Capture: 20261109-074039-589666-c2-f24.bin in the Blaze component runtime. No mode-specific implementation changed yet.

2026-11-09 EASHL: fresh login now passes welcome and reaches Customize Pro. Missing EASHL_PREORDER_INCENTIVE_1/2/3 configuration caused the retrieval error; explicit zero values fix it. Club and match flow remain unverified. See sibling Blaze Research/eashl.md.

2026-11-09 HUT: Right three times from initial main menu, then Cross. Native
CardHouse 2148/101 NO_PLAYER_INFO response opens Name Your Team. Util text
validation and persistent team creation now reach starter-pack CreatePack
2148/401 PTYP=1. Starter packs and economy are unimplemented. The account-limit
popup is the original UI's generic creation-failure fallback, not evidence of
an actual quota. Details and currency findings: sibling blaze pack Research/hut.md.
