# DocModular — Reusable Unreal Engine 5.8.3 Gameplay Systems

A project-agnostic library of gameplay plugins: one shared Core plus twenty independent feature plugins.

- Modules 1–10: Interaction, Events, Regions, Streaming, Time, Adaptive Audio, Sequences, Inspection, Save, World Activation.
- Modules 11–20 (expansion): Map Navigation, Weather, Surface Feedback, Dialogue, Quest Objectives, NPC Schedules, Knowledge Codex, Inventory Items, Unlocks Progression, Game Framework UI.

**Current state:** Core and all twenty feature base plugins exist as source (Implemented / Unverified). None has been compiled or run yet, and no bridge plugin exists. See `Docs/DEVELOPMENT_STATUS.md` for facts; nothing else in this repository should be read as a claim that a system works. Each plugin's `README.md` lists what it includes and what it leaves to bridges.

| Path | Contents |
|---|---|
| `DocModularDev.uproject`, `Source/`, `Config/` | Development host project (build/test only) |
| `Plugins/DocModular/` | The plugins (grouping folder, no descriptor) |
| `Plugins/DocModularBridges/` | Bridge plugins, once any exist (grouping folder, no descriptor) |
| `Bridges/`, `Samples/` | Future bridge staging and clean consumer hosts |
| `Scripts/` | Build/test wrappers that record evidence |
| `Docs/` | Specification, status, decisions, traceability, compatibility |
