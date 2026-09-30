# Writing test cases

Test cases live in `testcases.json` beside the binary, auto-loaded at start.
**Tools → Test cases** reloads, resets a run, or saves the observation report.

A case is a condition expressed in the console's query language. The engine
watches live or recorded traffic and records what it saw. It reports
**OBSERVED / NOT ANSWERED / not observed** — never Pass or Fail. Whether a
case passes is the signatories' judgement; this supplies the evidence.

## The three shapes

### 1. Simple — a condition seen anywhere

```json
{"id": "32.7/SIG_OV", "title": "SIG_OV set when signal overridden",
 "query": "field:SIG_OV=1"}
```

### 2. Sequence — a prior state, a trigger, and a result

Most SIF cases have this shape. Steps must occur **in order**; each may set
a deadline measured from the previous step.

```json
{"id": "32.x/STANDBY-FS",
 "title": "From Full Supervision, entering Stand_By when no cab is active",
 "sequence": [
   {"label": "in Full Supervision", "match": "field:LOCO_MODE=4"},
   {"label": "no cab active",       "match": "field:cab1_active=0 field:cab2_active=0"},
   {"label": "Stand_By",            "match": "field:LOCO_MODE=1", "within_ms": 10000}
 ]}
```

Steps may read **different packet types** — the prior state and result come
from LSRP/ARP, the trigger from DIP1. That is the point: the console
correlates across packets, which is what no single query can do.

`within_ms: 0` (or omitted) means no deadline on that step.

`given` / `then` is accepted as shorthand for a two-step sequence.

### 3. Expand — one shape, several numbered clauses

When the SIF numbers the same behaviour once per mode, write it once.
`{placeholders}` are substituted everywhere, including inside steps.

```json
{"id": "32.x/STANDBY-{code}",
 "title": "From {mode_name}, entering Stand_By when no cab is active",
 "sequence": [
   {"label": "in {mode_name}", "match": "field:LOCO_MODE={mode}"},
   {"label": "no cab active",  "match": "field:cab1_active=0 field:cab2_active=0"},
   {"label": "Stand_By",       "match": "field:LOCO_MODE=1", "within_ms": 10000}
 ],
 "expand": [
   {"code": "FS", "mode": "4", "mode_name": "Full Supervision"},
   {"code": "LS", "mode": "3", "mode_name": "Limited Supervision"},
   {"code": "SR", "mode": "2", "mode_name": "Staff Responsible"}
 ]}
```

### 4. all_of — one trigger, several required consequences

When a single event must produce **several** reactions, they arrive in
different packets and in no guaranteed order, but every one is required.

```json
{"id": "32.x/FOREIGN-TAG",
 "title": "Foreign tag causes SR mode, emergency brake and loss of position report",
 "sequence": [
   {"label": "foreign tag reported", "match": "field:TAG_LINK_INFO!=0"},
   {"label": "loco reacts", "within_ms": 15000,
    "all_of": [
      {"label": "Staff Responsible mode", "match": "field:LOCO_MODE=2"},
      {"label": "brake applied",          "match": "field:Brake_Applied!=0"},
      {"label": "emergency status set",   "match": "field:EMERGENCY_STATUS!=0"}
    ]}
 ]}
```

The step completes only when **every** part has been seen. If the window
closes first, the report names which parts were satisfied and which were
missing — "the mode changed and the brake applied but no emergency status
followed" is the finding; "the step did not complete" would not be.

A step uses either `match` or `all_of`, never both.

## Coverage — the document's simulate-with lists

Clause 32.9.3 does not ask for *a* frame ending 011; it lists thirteen. The
case is not complete until every listed value has been seen **while the
condition matched**.

```json
{"id": "32.9.3", "title": "Health bits 6-11 on frames ending 011",
 "query": "field:FRAME_NUM&7=3",
 "cover_field": "FRAME_NUM",
 "cover_values": [3,11,19,27,35,43,51,59,67,75,83,91,99]}
```

The report lists which values are still missing.

## The field catalogue — `fieldmap.json`

This is where you tell the program **what fields exist, which packet
carries them, and what their values mean**. It is what lets a case be
written in the language of the specification instead of the language of the
decoder.

```json
{
  "packets": { "lsrp": "Loco → Stationary regular packet",
               "dip1": "Digital inputs, bank 1" },
  "fields": [
    { "name":   "loco_mode",
      "field":  "LOCO_MODE",
      "packet": "lsrp",
      "desc":   "Operating mode reported by the loco",
      "values": { "stand_by": 1, "staff_responsible": 2,
                  "full_supervision": 4, "on_sight": 6 } },

    { "name":   "cab1_active",
      "field":  "24 cab1_active",
      "packet": "dip1",
      "values": { "inactive": 0, "active": 1 } }
  ]
}
```

| Key | Meaning |
|---|---|
| `name` | what a test case writes |
| `field` | the decoder's name for it; omit if identical to `name` |
| `packet` | the captype that carries it — the field will match **only** in that packet |
| `values` | symbolic names for numbers |
| `desc` | shown in tooling; free text |

With that in place:

```
field:loco_mode=staff_responsible      instead of   field:LOCO_MODE=2
field:cab1_active=inactive             instead of   field:24 cab1_active=0
field:radio1_link_fail=fault           instead of   field:B6 RADIO1_LINK_FAIL=1
```

Three things this buys, and the third is the reason it exists:

- **Readability.** `field:LOCO_MODE=2` says nothing on the page about which
  mode is meant.
- **Packet scoping.** A name that exists in two packets no longer matches in
  both. A case written for DIP1 cannot be satisfied by an LSRP frame.
- **A misspelt value is refused at parse time**, with a list of what it
  could have meant. Previously a wrong digit in `LOCO_MODE=2` produced a
  case that ran happily and checked the wrong thing — the worst possible
  outcome for an acceptance test, because it reports success.

The catalogue is **additive**. A field it has not been told about is still
queryable by its raw decoder name, and with no `fieldmap.json` at all every
existing query keeps working.

Completion offers catalogue names, and offers a field's symbolic values once
you have typed `field:loco_mode=`.

## Writing the conditions

Anything the schema decodes is available:

| | |
|---|---|
| `field:SIG_OV=1` | numeric equality |
| `field:TRAIN_SPEED>60` | `>` `>=` `<` `<=` `!=` |
| `field:FRAME_NUM&7=3` | bitwise mask, for multiplexed fields |
| `field:CAB_INPUT_FAULT=1` | an expanded health bit, by name |
| `field:B22=1` | the same bit, by SIF number |
| `field:Loco_Health~RADIO1` | substring of the rendered value |
| `"PACKET REJECTED"` | plain text still works |
| `A OR B`, `NOT A`, `(A OR B) C` | boolean composition |

Right-click any decoded row → **Why this colour?**, or open the Decoded
fields panel, to see the exact field names a packet offers.

## Things worth knowing

- **Assertions are independent.** One run satisfies every case it exercises;
  nothing consumes a message.
- **A persisting precondition arms once.** Cab inputs stay inactive across
  many DIP frames; the deadline runs from when the condition began, not
  from the latest frame.
- **A stalled sequence is a finding, not an untested case.** It is reported
  as NOT ANSWERED and raises a warning during the run.
- **A broken query is kept and flagged**, never silently dropped — a case
  that quietly is not being checked is worse than one visibly broken.
- The report records the **slowest observed gap**, which is how to tune a
  `within_ms` that is too tight.
