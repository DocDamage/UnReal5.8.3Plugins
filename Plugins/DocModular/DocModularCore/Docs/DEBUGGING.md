# DocModularCore — Debugging

- Log category: `LogDocCore` (`log LogDocCore Verbose` in the console).
- `FDocSystemResult::ToString()` → `[Outcome] Op=<id> Tag=<error tag> <diagnostic>`.
- `FDocRequestHandle::ToString()` → `Op<id>@E<epoch>`. A handle whose epoch differs from its table's `GetEpoch()` came from a reset/destroyed owner or a previous session.
- `FDocPersistentObjectId::ToString()` is the canonical text form; paste it back through `Parse()` / `Parse Persistent Id`.
- No editor panel exists yet (CORE-07 Not Started).
