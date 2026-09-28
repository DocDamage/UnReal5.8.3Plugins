# DocModularCore — Blueprint Usage

Nodes are under **Doc|Result**, **Doc|Context**, **Doc|Identity**, **Doc|Handle**, **Doc|Tags**.

- Check a result: `Is Result Success` (true for Succeeded and No Change); `Is Result Changed` when you only care whether state actually changed. Branch on `Outcome` or match `Error Tag` against `Doc.Error.*`.
- Build a context: `Make Doc Gameplay Context` (world from the calling object; Instigator/Target optional).
- Give an actor a durable ID: implement **Doc Persistent Identity** and return a stored `Doc Persistent Object Id`. Store the ID in a variable that is saved with the actor; do not generate one in Construction Script or BeginPlay.
- Runtime spawns (authority only, once): `Make Runtime Persistent Id`, then store the result.
- Expose tags from a Blueprint actor: implement **Doc Gameplay Tag Provider** → `Get Doc Owned Tags`. Readers call `Get Owned Tags From Object`, which also works for C++/GAS actors that use the engine tag interface.
- Handles: compare with `Equal (Doc Request Handle)`. `Is Set` only means "was issued"; ask the owning system whether it is still active.

Status: no example Blueprint assets exist yet. They will be authored in the editor, not generated as text.
