# T-06: Performance measurements and defaults

- Depends on: T-04
- Size: S
- Owner:
- Started:
- Finished:

## Goal
Replace the plan's guesses (plan §4, §5) with numbers and pick defaults.

## Steps
1. H/s at N-factor 21 for 1–8 threads, with and without `-march=native`, with and without huge pages; compare with yacoind's built-in miner (`setgenerate`, measured briefly on the lowdiff test chain at N-factor 21 or by `hashespersec`).
2. Effect on the running mainnet node (CPU, memory) and laptop temperature.
3. Pick the default threads and nice level (Q5) and document the numbers.
4. Expected time per block from the measured rate and the network estimate.

## Acceptance criteria
- [ ] Table of results in the task log; defaults set in the code and README.

## Log
-
