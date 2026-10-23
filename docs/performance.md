# Performance and memory

CPU, memory and load time measurements. Part of the [documentation index](../README.md#documentation).

## Performance and memory

### CPU (author's Windows machine, peak, original Nuked-SC55 for comparison)

| Song | Original | Poly before optimisation | Current |
| --- | --- | --- | --- |
| E1M1 (Doom) | 4.6 % | 12.4 % | **3.8 %** |
| Animus | 5.4 % | 16.6 % | **12.3 %** |

"Animus" needs 32-41 voices, which means 2-3 units (the original steals the rest). One SC-55 instance took about 19 % (v1.21) / 30 % (MkII) of one core before the optimisation (sandbox, one core). An SC-88 Pro unit costs about 11-12 % of one core. The interface thread with the editor open takes 3.3 % (SC-55) or 2.9 % (SC-88 Pro) of a core under Wine, while a song plays in real time.

### Core optimisation (bit-exact)

The four timers (3 x FRT, 1 x TMR) are evaluated lazily, one by one. Ticks without an event only increase the counter; ticks with a compare match, an overflow or after a register access (`dirty`) run through the original logic. Every timer register access synchronises everything; divisions are shifts. This is bit-exact because the timers are independent and only set interrupt pending bits, which are evaluated before the next instruction and cleared only through `TIMER_Write`. The pure core (E1M1, v1.21, `-O2`) went from 24.8 s to 15.1 s (-38 %).

Tried and rejected: polling the peripherals less often (< 5 %), skipping silent PCM slots (only helps at low load and risks bit-exactness), PGO (bit-exact, but about 10 % slower), `-march=x86-64-v2/v3` (no gain), hand-written SIMD for the PCM slot loop (too costly for bit-exact special cases, estimated 15-25 %). The big remaining lever would be a JIT for the SC-55 CPU, which is a project of its own.

### Memory (Wine, embedded ROMs, units active)

| Plug-in | Before | After |
| --- | --- | --- |
| MT-32 / CM-32L (2 units) | - | about 3 MB per unit on top of the plug-in (measured on Linux) |
| SC-55 v1.21 | 236 MB | **30 MB** |
| SC-55mk2 | 216 MB | **29 MB** |
| SC-88 Pro | 247 MB | **166 MB** |
| SC-8820 (4 units) | 213 MB | **81 MB** |
| SC-8850 (2 units) | 160 MB | **129 MB** |

Each SC-55 unit held 19.5 MB of arrays (wave ROMs 5 MB + card 2 MB + expansion 8 MB + a 4 MB LCD buffer), of which about 3.5 MB were used. Now the wave ROMs are one `calloc` block shared by all units (untouched pages cost nothing) and the LCD buffer is only created on demand. In the SC-88 Pro, the decoded wave ROM is shared process-wide (cache by FNV hash of the raw data); the rest (about 35 MB per unit) is the internal state of 88emu and JIT code. The SC-8820 and SC-8850 map their wave ROM straight from the embedded image instead of copying it (the SC-8820 used to copy 24 MB per unit, the SC-8850 a shared 32 MB block that was also hashed at every boot), so a unit costs about 13 MB (SC-8820) or 34 MB (SC-8850) in the Linux measurement; the output is bit-identical.

### Load time (Wine, one core)

| Plug-in | Before | After |
| --- | --- | --- |
| SC-55 v1.21 | 0.37 s | **0.04 s** |
| SC-55mk2 | 6.41 s | **0.58 s** |
| SC-88 Pro | 3.56 s | **1.06 s** |

Only SC-55 unit 0 boots; the others take a bit-exact copy of its state (`Emulator::CopyStateFrom`). SC-88 Pro units 1..N boot in background threads, and a unit that is needed before it has booted makes the wake-up wait. The warmer thread (not the audio thread) waits for a booting unit it needs, and it schedules by the logical position in the state log, because otherwise the warm-up would depend on the boot time and the output would not be reproducible. So playing right after loading no longer stalls the first block (SC-8820 1.7 s to 2 ms, SC-8850 2.8 s to 4 ms in the sandbox).
