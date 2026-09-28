# DocEvidenceDeduction

Observations, testimony, provenance, hypotheses, typed contradictions and player conclusions (Modules 21–40 handoff, Section 12; Module 30). This is a fictional gameplay reasoning model. It is not a forensic truth engine and needs no AI model.

**Status:** Implemented / Unverified until a `Scripts/Output` run covers this revision.

| | |
|---|---|
| Subsystem | `UDocEvidenceSubsystem` (GameInstance; owner-scoped state, retains no actors) |
| Assets | `UDocEvidenceDefinition`, `UDocHypothesisDefinition` |
| Depends on | DocModularCore; engine modules only. No Codex, Dialogue, board UI or AI model. |
| Not included | Probabilistic truth, natural-language inference, numeric/time comparison evaluators beyond typed contradictions, custom predicate registry, network replication, rule editor UI |

## Record model

- **Scopes:** every investigation is scoped by `OwnerId`. `NAME_None` is the single-player default. Owners never see each other's observations, conclusions or receipts. The subsystem never merges scopes.
- **Claims and facts:**
  - A **claim** is a reported assertion. Only an **established fact** can support or disqualify.
  - `ReviseObservation` cannot turn a claim into a fact.
  - `EstablishClaim(Observation, CorroboratingSource)` is the explicit promotion, and it is recorded in the observation's history.
- **History:** revisions, promotions and retractions keep bounded history, 16 entries per observation.
- **Dedup:**
  - A repeated `ObservationId` with the same provenance returns `NoChange`; with different provenance it returns `Conflict`.
  - A repeated `ProvenanceKey` for the same evidence returns `NoChange`.
  - Two witnesses are never merged just because their text matches.
- **Player links:** `AddInterpretationLink` records the player's board interpretation. It **never** changes evaluation or the evidence revision.
- **Registration:** observations must reference a registered evidence definition.

## Evaluation

For a hypothesis over a coherent owner state:

1. **Disqualifiers:** any established, visible observation of a `DisqualifyingEvidenceIds` item means **Contradicted**.
2. **Dependencies** (`DependentHypothesisIds`): each must be Supported. A Contradicted dependency contradicts this hypothesis too.
3. **Alternative evidence sets:** the first fully established set, in authored order, supports. `SatisfiedSetIndex` reports which one.
4. **Typed contradictions** among the supporting observations (`bContradictionsDisqualify`):
   - *MutuallyExclusiveStatement:* the same `SubjectId` with a different `ClaimValue`.
   - *IncompatibleTimestamp:* the same subject and `EventFrameId`, with event times further apart than the sum of their declared precisions. Unknown precision never contradicts.
   - Display strings are never compared.

**Outcomes:**

| Outcome | Meaning |
|---|---|
| Supported | All support present and nothing contradicts it. |
| Contradicted | A disqualifier, contradicted dependency or typed contradiction applies. |
| Inconclusive | Missing evidence. This is never treated as proof of absence. |
| Unavailable | Hidden, unknown, or an invalid graph. |

`ConfidenceScore` is an authored gameplay score (`SupportedScore`), not a probability.

**Graph limits at registration:**

- Cycles are rejected with a path diagnostic, for example `H_B -> H_A -> H_B`.
- Also rejected: depth over 16, more than 32 dependencies, and empty evidence sets.
- Duplicate ids return `Conflict`.

## Audience

Ordinary queries (`EvaluateHypothesis`, `QueryExplanation`, `QueryAvailableHypotheses`) and `CommitConclusion` evaluate **only evidence visible to the player** (excluding `bIsHiddenFromPlayer`). Payloads are filtered *before* they are returned:

- **Hidden solutions** (`bIsHiddenSolution`) return `PermissionDenied` with an empty payload, and are not listed.
- **Undiscovered evidence is never named.** `MissingEvidenceIds` is empty and only `MissingEvidenceCount` is given.
- **Secret evidence definitions** (`bIsSecret`) are redacted from support and contradiction lists, and `bRedacted` is set.
- **Debug explanations:** `QueryDebugExplanation` (full view, with missing ids and hidden evidence) needs `bAuthorizeDebugExplanations`, set by a development tool. It is compiled out in Shipping (`Unsupported`).

## Conclusions

- **Commit is separate from evaluation.** `CommitConclusion(Id, Hypothesis, ExpectedRevision, ReceiptKey, Owner)` refuses:
  - a stale owner revision (`Conflict`)
  - a reused receipt (`Conflict`)
  - a second live conclusion in a mutually exclusive group (`Conflict`)
  - an unsupported hypothesis (`Failed`)
- **Idempotent:** re-committing the same conclusion returns `NoChange` and issues no second effect.
- **Commit-before-event:** the record stores the supporting `Observation@Revision` list and commits before `OnConclusionCommitted` fires.
- **Other conclusions are untouched:** choosing one conclusion never deletes the evidence behind another.
- **Challenges:** adding, revising, establishing or retracting evidence re-evaluates only the dependent conclusions. One that is no longer Supported is marked **Challenged**, with a reason, and fires one event. It is never rewritten or removed.

## Persistence

- `CaptureState(Owner)` saves the owner's observations (with history), links, conclusion history, receipts and quarantined conclusions.
- `RestoreState` validates first: schema, ids, duplicate receipts, duplicate links. It then replaces that owner's scope, **without events**, so nothing is awarded again.
- A conclusion whose hypothesis definition no longer exists is **quarantined**: it is kept but inactive.

## Tests

`Doc.Evidence.*`, 10 tests: EVD-01 to EVD-10.
