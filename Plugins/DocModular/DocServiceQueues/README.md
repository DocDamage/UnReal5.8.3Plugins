# DocServiceQueues

Waiting order, service stations, limited capacity, group reservations, batch admission, abandonment, expiry and closure (Modules 21–40 handoff, Section 8; Module 26). It decides who is eligible and which capacity is reserved; it does not move actors or run the served machine.

**Status:** Implemented / Unverified until a `Scripts/Output` run covers this revision.

| | |
|---|---|
| Subsystem | `UDocServiceQueueSubsystem` (World) |
| Components | `UDocQueueStationComponent`, `UDocQueueParticipantComponent` |
| Data | `UDocServiceQueueDefinition` |
| Depends on | DocModularCore; engine modules only |
| Not included | NPC Schedules / Smart Objects / movement / Save bridges, host evacuation provider, network views |

## Admission

`Waiting → Offered → Reserved → Arriving → Ready → InService → Completed`, or `Cancelled`, `Expired`, `Rejected`. `ReconciliationRequired` is used after restore.

**Capacity** always satisfies `ReservedSeats + InServiceSeats <= Capacity`.

- **Leases.** Each ticket holds its own lease: an offer id while Offered, and a reservation id while Reserved, Arriving or Ready. Leaving, expiring or closing releases exactly that lease.
- **Service.** `ConfirmArrival` only marks the ticket Ready. `StartService` consumes the reservation, and only `CompleteService` completes the ticket. A duplicate completion is a no-op.
- **Deadlines.** Offers expire at `OfferTimeoutSeconds` and reservations at `ArrivalTimeoutSeconds` (no-show). Both run on the queue clock: world time, or `SetClockOverride`. `CheckExpirations` sweeps them. A late accept or a late arrival cannot resurrect an expired lease.

## Ordering and fairness

- **FIFO** uses the join ordinal. `PriorityThenFIFO` sorts by granted priority, then ordinal.
- **Priority** that a participant asks for is clamped to `MaxSelfAssignedPriority`. `SetTicketPriority` is the authority's grant.
- **Groups** are admitted atomically. A group larger than a station's total capacity is skipped for that station with the reason `OversizedForStation`, so it never blocks the queue.
- **`FirstFitWithBypassLimit`** passes a group that doesn't fit yet, but only while its bypass count is under `MaxBypassCount` and its age is under `MaxBypassAgeSeconds`. After that it blocks with the reason `BypassLimitReached`. A ticket's bypass count grows only when someone behind it is actually admitted.
- **Batches.** `FormBatch` reserves a whole batch in one commit. It starts when the station is full, when the batch reaches `MinBatchSize` seats, or once the oldest selected ticket has waited `BatchWaitSeconds`.
- **Duplicate joins.** A second join with the same `JoinKey` (default: the participant id) and the same party returns the existing ticket. A different party is a `Conflict`.

## Closure and cleanup

- **Queues.** `SetQueueClosed` stops joins and new admissions. `UnregisterQueue` cancels every ticket and releases every seat those tickets held.
- **Station close policies:**
  - **Drain:** existing admissions continue, and no new offers are made.
  - **CancelPendingReservations:** offers and reservations return to Waiting.
  - **EmergencyAbort:** the same, and if anyone is in service the station becomes **Blocked** with its seats still occupied. `ConfirmEvacuated` then cancels those tickets and closes the station.
- **Station removal** (`UnregisterStation`, component EndPlay) returns its offers and reservations to Waiting and cancels in-service tickets.
- **Participants.** When a participant leaves, including from its component's EndPlay, it releases whatever it holds. Leaving a finished ticket is a no-op.

## Persistence

`CaptureQueue` / `StageRestore` (schema 2):

- The holds of the tickets being replaced are released first, and duplicate records are ignored.
- Offered and reserved tickets come back Waiting, and later offers create new leases.
- In-service tickets come back `ReconciliationRequired`, with no seat charged, until `ReconcileTicket` records what the service provider reports.

## Tests

`Doc.Queue.*`: FIFOAndPriority (QUE-01), MultiStationReservation (QUE-02), GroupAtomicity (QUE-03), BypassAndStarvation (QUE-04), OfferExpiry (QUE-05), ArrivalAndCompletion (QUE-06), BatchTimeout (QUE-07), ClosureAndOwnerLoss (QUE-08), RestoreRevalidation (QUE-09), IsolatedHeadless (QUE-10).
