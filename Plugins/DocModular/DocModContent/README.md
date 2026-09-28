# DocModContent

**Module 39 — Validated Data-Pack Catalogs and Controlled Registration**

## Purpose and Scope
`DocModContent` provides structured data-only mod content pack discovery, strict manifest schema validation, topological dependency ordering, staged two-phase catalog transactions, definition pinning, and missing-content quarantine.

The plugin enforces strict data-only boundaries:
- Rejects executable files (.dll, .exe, scripts) and binary assets (.uasset, .umap).
- Prevents directory traversal (`..`, absolute paths, path escapes).
- Guarantees transactional two-phase commit: any provider failure during staging triggers complete rollback, keeping the prior catalog wholly intact.
- Enforces content byte-hash identity between validation and activation.
- Protects in-use definitions from unsafe deactivation via ref-counted pins.
- Quarantines missing mod data from saved games without destructive deletion.
- Operates with zero sibling plugin dependencies, verified via `UDocModSampleDefinitionProvider`.

## Architectural Rules and Invariants
- **Data-Only Manifests (`MOD-01`)**: All packs define `manifest.json` with semantic versioning (`FDocModSemVer`), dependencies (`Requires`), conflicts (`Conflicts`), and definition records (`Definitions`).
- **Path Containment (`MOD-02`)**: Paths must resolve strictly within the designated pack directory.
- **Deterministic Dependency Ordering (`MOD-03`)**: Resolves dependencies via Kahn's algorithm with stable tie-breaks, identifying missing requirements, version mismatches, cycles, and conflicts.
- **Namespace Collision Defense (`MOD-04`)**: All definition IDs must be namespaced with their owning `pack_id:`. Collisions fail validation rather than silently overwriting.
- **Transactional Staged Activation (`MOD-05`)**: Two-phase commit (`StageDefinition` -> `CommitStagedDefinitions` / `RollbackStagedDefinitions`).
- **Validated Byte Identity (`MOD-06`)**: Hashes calculated during validation must match bytes at activation.
- **Pinning & Safe Deactivation (`MOD-07`)**: Active pins prevent deactivation (`InUse`).
- **Missing Content Quarantine (`MOD-08`)**: Missing mod packs preserve data records without overwriting saves.
- **No Executable Content (`MOD-09`)**: Code, executables, scripts, and binary assets are blocked by default.
- **Isolated Provider (`MOD-10`)**: Operates independently with built-in sample provider.
