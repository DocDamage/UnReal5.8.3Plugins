# Consumer host samples

These projects show two ways to consume the DocModular plugins from outside the development host.

- `CppConsumer` is a broad C++ consumer that discovers the suite from the external `Plugins/DocModular` directory. It depends on all 41 runtime modules and compiles the public headers for the suite.
- `NonCharacterInteraction/Project` is a small, nested game project. The verifier stages source copies of only Core and Interaction into its project-local `Plugins` folder, with all sibling plugin folders absent. Its default game mode runs an instant interaction between two plain `AActor` subclasses when launched headlessly.

Run both builds and the focused startup/interaction smoke check from the repository root:

```powershell
.\Scripts\Verify-ConsumerHosts.ps1 -EngineRoot 'C:\Program Files\UE_5.8'
```

The verifier writes a `summary.json` under `Scripts/Output/`. It records the source fingerprints for the staged plugins. The sample uses the engine's `/Engine/Maps/Entry` map and creates its actors at runtime; neither project requires project-authored `.uasset` or `.umap` files.
