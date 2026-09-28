# DocModularCore — Setup

- **Project config:** none required. The plugin writes nothing to project config.
- **Tags:** `Doc.Error.*` and `Doc.Control.*` are registered natively when the module loads (Editor and packaged). Projects may add child tags in their own tag tables.
- **Assets / cooking:** Core has no content (`CanContainContent: false`), no primary asset types.
- **Components / input:** none in Core.
- **Authority:** `FDocGameplayContext::ResolveAuthority()` reports authority; Core itself performs no replicated operations.
- **Cleanup:** handle tables are owned by feature subsystems; they must `RemoveAllForScope`/`Reset` on world teardown.
- **Uninstall:** remove the plugin folder and the `.uproject` entry. Blueprints that use Core types must be updated first.
