# Open questions for the owner

- **Q1 – Repository:** Create a GitHub repo for this project? Under which
  account, public or private, and what name
  (suggestion: `yacoin-cpuminer`)? Until then the repo is local only, in
  `~/projects/cpu-miner`.
- **Q2 – Go ahead:** Should implementation start after the P0-08 task in the
  yacoin project is finished? Expected yield is small: difficulty is fixed
  at the minimum, so it depends only on our own hash rate, roughly one block
  per day or two at 10 H/s (5.17 YAC each; plan §4). Is it still worth it as
  a hobby and testing tool?
- **Q3 – RPC credentials:** The node's RPC password is in
  `/srv/yacoin/datadir/yacoin.conf`, readable only by the `yacoin` user.
  Options: (a) run the miner as `yacoin` and read that file; (b) add a second
  RPC user for the miner with `rpcauth=` in `yacoin.conf` (needs a node
  restart); (c) a copy of the password in a miner config file only you can
  read. Suggestion: (a) to start, (b) later.
- **Q4 – Licence** for the miner's own code. Suggestion: MIT, same as yacoin
  (the copied code stays under its own MIT/public-domain notices).
- **Q5 – CPU budget:** How many threads may it use by default while the node
  runs and the laptop is also used for builds? Suggestion: 4 threads (2 GiB
  RAM), `nice 10`, and you can change it at any time.
- **Q6 – Always on?** Run it as a systemd service that starts at boot, or
  only by hand?
