# SMIB — SAS compliance spec & CMS production plan

Status: draft, 2026-07-12. Companion to `README.md`; grounded in the current
firmware (`main/sas/`, `main/cms/`) and the host simulator.

---

## Part 1 — SAS protocol: what must be complied with

The de facto standard is the **IGT Slot Accounting System (SAS) Protocol,
version 6.03** (6.02 is the floor for AFT-capable machines). The spec is
licensed — obtain the official document from IGT/Light & Wonder or through the
EGM vendor; everything below must ultimately be checked against it, not this
summary.

### 1.1 Requirements on the EGM (what to demand from the slot machine)

An EGM is usable with this SMIB only if it provides:

- **SAS 6.02+** with the **AFT feature set**: long polls `0x72` (transfer),
  `0x73` (registration), `0x74` (game lock & status), and the AFT exception
  group (`0x66`–`0x6F`).
- **In-house cashable transfers both directions** (`transfer type 0x00`
  to-EGM, `0x80` from-EGM) enabled in the operator menu, plus **host-cashout
  mode** if the cash-out button should route credits to the card instead of
  handpay/ticket.
- **Asset number** configured (non-zero) and **AFT registration mode** set to
  match ours (currently we send a zero registration key, i.e. the EGM must be
  in "registration not required" mode — see §1.4).
- Standard accounting long polls: `0x19` (5 basic meters), `0x1A` (current
  credits), `0x1F` (game/denom info), `0x54` (SAS version + serial).
- **Accounting denomination** reported correctly via `0x1F` — all credit
  meters are in units of this denom, not cents (see gap G3).
- GLI-11 certification (or the local-jurisdiction equivalent) with the SAS
  option covered by the cert.

### 1.2 Physical / link layer (whoever touches the real EGM wire)

The SMIB itself speaks SAS-over-mux to the host PC, so this lands on the
production splitter app (or on the SMIB if it is ever wired to the EGM
directly):

- **19,200 baud**, 11-bit words: 1 start, 8 data, **1 wakeup ("9th") bit**,
  1 stop. The wakeup bit is set only on the address byte of each poll. On
  UARTs without 9-bit support the standard trick is mark/space parity
  switched per byte.
- EGM must start its response **within 20 ms**; a gap of **> 5 ms** between
  response bytes terminates the message.
- Host polls each EGM on a **40–200 ms cycle**; if the EGM sees no poll for
  ~5 s it treats the link as down (and may tilt/display "communications
  lost").
- CRC-16/KERMIT, appended LSB-first — already correct in `mux/crc16.c`.

### 1.3 Host-engine behavior required by the spec

- **General poll** is the single byte `0x80 + address` — for our EGM at
  address 1 that is **`0x81` only**. There is no A/B alternation (gap G1).
- **Implied ACK protocol**: sending the *next* poll acknowledges the previous
  response. On a missing/garbled long-poll response the host must **repeat
  the same long poll** (typically up to 2 retries) so the EGM can resend;
  jumping to a different poll implicitly acks a response we never saw
  (gap G4).
- **Exception queue**: general-poll responses are exception codes; the EGM
  buffers them, priority exceptions jump the queue. The host must poll often
  enough to drain it and must handle at minimum: doors (`0x11/0x12`…),
  general tilt (`0x20`), **handpay pending `0x51`** (staff flow), game
  started/ended (`0x7E/0x7F`), and the AFT group (`0x66` cash-out button,
  `0x69` transfer complete, `0x6A/0x6B` host-cashout requests, `0x6C/0x6D`
  registration).
- **Meters** are 4-byte packed BCD (8 digits) and roll over at 10^8 — deltas,
  not absolutes, must be accumulated for reconciliation.
- **Busy response**: an EGM may answer a long poll with just its address byte
  (busy) — treat as retry, not protocol error.

### 1.4 AFT specifics

- **Recommended transfer sequence**: `0x74` lock request → verify
  "available for transfer" status → `0x72` full transfer → resume general
  polls → exception `0x69` → `0x72` interrogate (index 0x00) for final
  status → next poll acks. From-EGM transfers on many machines **require**
  the `0x74` lock first (gap G5).
- **Transaction ID**: ASCII, ≤ 20 bytes, must differ from the previous
  transaction's ID. Re-sending an identical, already-completed transaction
  returns its final status — this is the EGM-side idempotency the funds
  chain relies on.
- **Amounts are always in cents** in `0x72`, regardless of denom.
- **Registration (`0x73`)**: the zero registration key we send is only valid
  when the EGM is configured "registration not required". Jurisdictions may
  mandate real registration (key + POS ID) — keep the code path open.
- **Host cashout via button**: when the player presses cash-out and the EGM
  is in host-cashout mode, it raises `0x66`/`0x6A` and waits a short,
  EGM-configured window for the host to start the from-EGM transfer before
  falling back to handpay/ticket. Our ИСПЛАТИ flow must also be triggerable
  from this exception, not only from the touchscreen.
- **Power-loss recovery**: after any reset (SMIB or EGM), interrogate the
  last transaction before starting a new one, and reconcile with the CMS
  pending-txn record (gap G6).

### 1.5 Regulatory / certification standards

- **GLI-11** — the EGM itself (vendor's responsibility; verify the cert).
- **GLI-13** — on-line monitoring & control systems: applies to the
  SMIB + splitter + CMS as an MCS. Notable asks: meter reconciliation,
  significant-event capture (doors, tilts, handpays), program verification
  (SAS long poll `0x21` ROM signature can satisfy this), clock sync
  (`0x7F` set date/time).
- **GLI-16** — cashless systems in casinos: applies to the AFT/CMS funds
  chain (transaction logging, recovery, player-account integrity).
- **National regulation (North Macedonia)**: the gambling regulator's
  technical requirements decide whether GLI standards are accepted, whether
  connection to a state monitoring system is required, and data-retention
  rules. This must be answered before certification planning.
- **Player data**: card UID ↔ identity mapping is personal data — retention
  and access rules per local data-protection law.

### 1.6 Known gaps in the current firmware (ranked)

| # | Gap | Where |
|---|-----|-------|
| G1 | General poll must be `0x80 + addr` (`0x81`), not alternating `0x80/0x81` | `sas_defs.h`, `do_general_poll()` |
| G2 | Real physical layer (19.2k, wakeup bit) unimplemented — must live in the production splitter | `tools/host_sim/mux.py` successor |
| G3 | Credit meters (`0x1A`, `0x19`) treated as cents; real EGMs report credits of the accounting denom — parse denom from `0x1F` and convert (AFT stays cents) | `sas.c`, `sim sas_egm.py` |
| G4 | No same-poll retry (implied ACK); timeout skips straight to the next poll | `sas_exchange()` |
| G5 | No `0x74` lock before transfers; from-EGM cashout will be refused by many machines | `run_aft_job()` |
| G6 | No AFT recovery after SMIB reset: in-flight txn not persisted (NVS) and no boot-time interrogate — breaks the "never rollback after AFT success" guarantee across a power cycle | `sas.c`, `app.c` |
| G7 | Cash-out button path: exceptions `0x66`/`0x6A` not handled, so player-initiated cashout at the machine falls back to handpay | `app.c` event handling |
| G8 | Handpay `0x51`, tilt `0x20` unhandled (staff notification + CMS event) | `app.c` |
| G9 | Meter rollover not handled in reconciliation | `sas.c` / CMS |
| G10 | Nice-to-have per GLI-13: `0x21` ROM signature, `0x7F` time sync, `0x01/0x02` enable/disable play tied to session state | new |

---

## Part 2 — CMS production plan

Today the CMS exists only as `tools/host_sim/cms_server.py`. Plan to
production, in order:

### Phase 0 — Freeze the wire contract
- Extract the JSON message set (auth, logout, debit, commit, rollback,
  credit, menu, order, ping, logout_push) from `cms.c`/`cms_server.py` into a
  versioned schema doc; add a `ver` field to the envelope.
- Codify the invariants the firmware already assumes: `debit_commit`,
  `debit_rollback`, `credit_req` **idempotent by `txn`**; commit retried
  forever after AFT success; timeouts surface as `err_code:"timeout"`.

### Phase 1 — Production splitter (host relay app)
- Promote `mux.py` into a daemon on the slot-machine PC: serial mux ↔
  **TLS** to the CMS; per-device identity (SMIB serial + asset number),
  reconnect with backoff, store-and-forward for CMS frames during outages,
  local log capture of channel `0x7F`.
- This is also where the real SAS physical layer lives if the PC fronts a
  physical EGM (gap G2).

### Phase 2 — CMS backend
- **Ledger**: double-entry, append-only; card balance = derived. Holds
  (debit) are first-class rows — a hold survives restarts and never
  auto-expires once an AFT may have started; staff tooling resolves stuck
  holds (mirrors the "контактирајте персонал" path).
- Sessions (card ↔ EGM ↔ SMIB), single-active-session enforcement,
  `logout_push`, 5-min inactivity logout server-side too.
- Menu/points/orders; admin UI for players, cards, menu, manual
  adjustments (all audited).
- **Reconciliation**: SMIB uploads SAS meter snapshots (from `0x19`)
  periodically; nightly job compares meter deltas vs. AFT ledger and flags
  variances (GLI-13/16 requirement and the main fraud control).

### Phase 3 — Security hardening
- Mutual TLS or per-device tokens for splitters; replay protection (nonce or
  monotonic counter in the envelope).
- Card auth: UID-only is cloneable — move to challenge–response cards
  (e.g. DESFire) or at minimum UID + server-side velocity/geo checks;
  the RC522 limits us to MIFARE-family options.
- Role-based staff access, full audit log, backup/restore drills.

### Phase 4 — Compliance & pilot
- Bench-test the SAS engine against 2–3 real EGM models (poll timing, AFT
  lock behavior, denom handling) — fix gaps G1–G7 first.
- Gap assessment against GLI-13/GLI-16 + the national regulator's technical
  rules; decide certification scope.
- Pilot on one machine: run the manual acceptance script plus fault drills
  (power-pull during each step of both transfer chains), daily
  reconciliation report for two weeks before scaling.

### Suggested firmware order of work
1. G1 + G4 (protocol correctness — cheap, unblock real-EGM testing)
2. G3 (denomination — money correctness)
3. G6 (NVS txn persistence + boot interrogate — funds safety)
4. G5 + G7 (lock + cash-out button — feature completeness)
5. G8–G10 as required by the regulator gap assessment.
