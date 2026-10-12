# Project working rules

## Storage

The owner explicitly requires careful disk-space use. New validation scratch
data belongs on C:, under `build/validation`, with only one active scratch set
and a total additional budget of 1 GiB. Check its size and C: free space before
heavy work; retain at least 20 GiB free. Ask before exceeding either limit.

Reuse existing builds and source-only CI. Do not make full source snapshots,
extra worktrees, duplicate embedded executables, or repeated copies of private
BIN/ROM files. Keep only necessary small receipts and selected screenshots.
Never recreate the deleted D:/SFE-validation history. Do not delete settings,
saves, unrelated projects, or uncommitted source to meet the storage budget.

## Nintendo 3DS target

Supported development targets are New 3DS / New 3DS XL with slider-controlled
stereo, and New 2DS XL in mono. Old 3DS / 3DS XL / 2DS are unsupported. Preserve
the real pre-game menu and source simulation/audio cadence. Use accuracy-enabled
emulator testing; do not request minor incremental hardware tests.
