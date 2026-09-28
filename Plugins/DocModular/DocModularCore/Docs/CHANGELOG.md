# DocModularCore Changelog

## 0.2.0 — 2026-09-27 (unreleased, unverified)
- Expansion primitives, each added with its first consumer (D-015): `FDocOwnerScope`, `FDocConditionResult` (+ `CombineAll` / `CombineAny`), `EDocClockDomain`, `FDocRecordRevision`, `FDocFeatureRecord`, `DocCoreSerialization`, `FDocEffectKey` / `FDocEffectReceipt` / `FDocReceiptLedger`.
- Control: `FDocControlClaimArbiter`, `UDocReferencePlayerControlProvider`, `UDocPlayerControlSubsystem`; tags `Doc.Control.Cursor` and `Doc.Control.Focus` (D-014).
- Development-only `FDocScopedTestWorld`.
- Tests added: `Doc.Core.OwnerScope`, `Doc.Core.ReceiptLedger`, `Doc.Core.ControlArbiter`, `Doc.Core.ConditionCombine`, `Doc.Core.FeatureRecordSerialization` (12 in total).
- Not yet compiled or tested on 5.8.3.

## 0.1.0 — 2026-09-26 (unreleased, unverified)
- Initial M1.1 source: results, native tags, context, request handles + handle table, persistent identity (scope composition v1), four narrow interfaces, Blueprint library, `Doc.Core.*` automation tests.
- Result outcomes `NoChange` (success, `IsChanged()` false), `InvalidInput`, `Unavailable`, `Conflict`; tags `Doc.Error.InvalidInput/Unavailable/Conflict/Storage`; `MakeNoChange`, `IsChanged`, `IsSuccessOutcome`; Blueprint `Is Result Changed`, `Make No Change Result` (D-012).
- Not yet compiled or tested on 5.8.3.
