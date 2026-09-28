# DocAssemblyMaintenance

Module 36 of the DocModular suite. Models multipart machines: covers, fasteners, replaceable parts, compatibility, diagnostics, step order, reassembly and a functional test. Depends only on DocModularCore and engine modules.

## Structure
- `UDocAssemblyDefinition`: slots (allowed part tags or definition ids, capacity 1, acyclic parent, required fasteners, obstructing slots, lockout requirement), fasteners (optional tool tag), diagnostics (power requirement, evaluated slots, detectable faults, functional-test flag), known part definitions and the initial layout.
- `ValidateDefinition` rejects duplicate ids, dangling references, cyclic parents and inconsistent initial parts. Capacity above 1 is reported as `Unsupported`.
- Each part instance has exactly one logical location: installed, detached or quarantined. Parts still at an external provider are not ours until the transfer commits.

## Operations
`RequestOperation` → `CommitOperation` (or `ExecuteOperationImmediate`). The steps are:
1. Validate the session, the operator and the optional `ExpectedRevision`.
2. Refuse targets held by another operation or another operator's lease.
3. Validate against current state (never against a procedure list): fastener order, obstruction, parent presence, attached children, compatibility, tools and lockout.
4. Reserve any external part and stage the operation with exclusive claims.
5. On commit, re-validate, then commit the provider transfer and the logical change.
6. Spawn presentation, write a receipt and release the claims.

Failed validation mutates nothing. Two users can never stage the same part, slot or fastener.

Capabilities come from `IDocAssemblyResourceProvider`. Its defaults are the safe answers: no tools, unknown machine state, no external parts. A required tool, lockout or power state that no provider can confirm fails explicitly (`Unavailable` / `PermissionDenied`); it is never assumed. `UDocAssemblyLocalProvider` is the standalone implementation.

## Cancellation and transfers
- Cancelling before commit rolls back and releases claims and reservations. Ending a session does the same for that session's staged work. Cancelling after commit returns `NoChange`, and the committed outcome stands.
- External transfers that the provider cannot confirm become `InDoubt`. Nothing is applied locally, the claims stay held, and a durable receipt (including the part definition) is written. `ReconcileTransfers` resolves them, and they are re-armed after `StageRestore`. A failed transfer leaves the part with the provider, so nothing is lost or duplicated.

## Presentation
Visual proxies are created from committed state only. A failed spawn leaves the part logically installed with presentation pending. `RetryPresentation` is bounded to 3 attempts and never creates a second proxy or a second logical part.

## Diagnostics
- `QueryAssembly` removes hidden faults. Faults become known only through tests, and a test reports only the faults it can detect (plus missing parts).
- Tests that need power fail explicitly without a machine-state provider, and report `BlockedByPower` when the machine is unpowered.
- Replacing a part does not mark the machine working. `CompleteProcedure` requires all fasteners secured and a passing functional test at the current revision.

## Persistence
`CaptureAssemblyState` holds the full state, including faults, receipts and the functional-test revision. `StageRestore` handles migration:
- It validates that each part has one location.
- Parts in removed, changed or incompatible slots are detached.
- Unknown part definitions are quarantined with a reason.
- Duplicate records of one part are merged into its first location.
- Earlier quarantines are kept.
- Fasteners are reconciled with the definition.
- The revision advances on any migration.
- Proxies are recreated after the logical state is in place.

## Tests (Doc.Assembly.*)
| ID | Test | Covers |
|----|------|--------|
| ASM-01 | PrerequisiteOrder | fasteners, obstruction, tools, lockout, available operations |
| ASM-02 | PartUniqueness | one location per part across remove/replace/install |
| ASM-03 | SlotCompatibility | incompatible/unknown parts and slots, no partial mutation, definition validation |
| ASM-04 | ConcurrentManipulation | exclusive claims, leases, stale revision, session identity |
| ASM-05 | CancelCommitBoundary | cancel before/after commit, reservation release, session end |
| ASM-06 | ResourceReconciliation | provider failure, in-doubt transfer, durable reconciliation after reload |
| ASM-07 | VisualFailure | pending presentation, bounded retry, single proxy |
| ASM-08 | DiagnosticTruth | hidden faults, power, required post-repair test |
| ASM-09 | RestoreMigration | removed slots, missing content, duplicates, old quarantine, fasteners |
| ASM-10 | IsolatedMachine | full diagnose → repair → test → complete flow with the local provider only |

## Known limits
- Slot capacity is 1.
- Presentation is modelled as proxy bookkeeping; actual mesh spawning belongs to the host.
- Networking and authority are not implemented.
