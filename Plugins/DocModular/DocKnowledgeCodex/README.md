# DocKnowledgeCodex

DocKnowledgeCodex stores persistent discoveries: tutorials, locations, characters, creatures, items, history, clues, documents, mechanics and custom knowledge. It has no UI dependency (Modules 11–20 handoff, Section 9; Module 17).

**Status:** Implemented / Unverified. Never compiled or run.

| | |
|---|---|
| Service | `UDocKnowledgeSubsystem` (GameInstance). State is partitioned by owner scope (player, shared or party; the scope carries the campaign namespace) |
| Data | `UDocKnowledgeEntry` and `UDocKnowledgeCatalog` (primary data assets). Categories are `Knowledge.*` tags, which are data, not a fixed taxonomy |
| Depends on | DocModularCore; engine modules Core, CoreUObject, Engine, GameplayTags, DeveloperSettings |
| Not included | Discovery-source bridges (Inspection, Dialogue, Quest, Region, Item, Event, Unlock); notification/UI and media playback adapters; DocSave bridge; editor Knowledge Browser, Category Browser, Missing Reference Validator, Related Entry Graph Preview and Search Tool (`ValidateCatalog` is the runtime check they would use) |

Handoff names map to Doc-prefixed types:

| Handoff | This plugin |
|---|---|
| KnowledgeSubsystem | `UDocKnowledgeSubsystem` |
| KnowledgeEntry | `UDocKnowledgeEntry` |
| KnowledgeCategory | `Knowledge.*` tags |
| KnowledgeRuntimeState | `FDocKnowledgeRuntimeState` |

## State dimensions

The original labels (Unknown, Discovered, Updated, Read, Completed, Hidden) overlap, so each fact is stored separately:

| Fact | Field |
|---|---|
| Reveal stage | `Stage` (0 = unknown) |
| Visibility | `bHidden` |
| Visible content revision | `VisibleRevision` (monotonic) |
| Read revision | `ReadRevision` |
| Completion | `bCompleted` |

Each entry also stores its discovery, last-update and read times, its update count, the gated sections revealed so far, and the content revision already seen.

`GetDisplayLabel()` derives the old labels for compatibility. **Updated** means visible content exists beyond the read revision.

**Stages.** Stage 0 Unknown, 1 Name, 2 Summary, 3 Full is the default, and entries can change it (`TitleStage`, `SummaryStage`, `BodyStage`, `MaxStage`, `StageLabels`). Sections and media reveal at a given stage.

**Gated sections.** A section with `RevealStage = 0` is gated: only `UpdateEntry(SectionId)` reveals it, and only when the entry's `bCanUpdate` is set.

## Commands

- **Discovering.** `DiscoverEntry` and `RevealEntry` are monotonic. Repeating a stage returns NoChange, raises no event and adds no update count.
- **Grant keys.** A grant may carry a Core effect key. The owner's receipt ledger returns the original result for a duplicate key and Conflict when the same key comes with a different payload.
- **Update count.** It counts committed visible updates according to `UpdateCountPolicy`: stage reveals plus sections, or sections only. UI refreshes, asset loads and restores never count.
- **MarkRead** takes the **displayed** revision. It rejects revisions that were never displayed. When an update arrived after display, the entry stays Updated.

**Separate authorized operations:**

- **`ConcealEntry`** hides an entry but keeps its discovery history.
- **`RevokeReveal`** lowers the stage. Removed content is not "unread", and completion is cleared only on request.
- **`SetEntryState`** is the administrative compatibility call, checked against legal transitions. Moving a discovered entry to Unknown is refused (use `RevokeReveal`), and Updated is derived, never set.

**Notifications.** `OnEntryDiscovered`, `OnEntryUpdated` and `OnEntryRead` each carry the owner, entry id, old and new revision, stage, cause, and the title only when it is visible.

**Queries.** `IsDiscovered` reports the discovered fact for progression conditions. It does not depend on whether a widget was ever opened.

## Relationships and spoiler-safe queries

**Relationships** are typed (Character, Location, Item, Event, Document, Custom) and directed, and each has its own reveal stage.

- **Visibility.** A link is shown only when its target is visible to the requesting owner. Dangling links are hidden and reported by `ValidateCatalog`.
- **Traversal.** `GetRelatedVisibleEntries` walks the graph with a visited set, a depth limit and a result limit, so cycles are safe.

**Player queries.** `GetVisibleEntry`, `GetEntriesByCategory`, `GetEntriesByTag` and search expose only what the owner can see.

- **Undiscovered entries** expose no text, tags, media names or links, and are left out of counts.
- **Placeholders.** An entry with `bHiddenUntilDiscovered = false` is listed as a placeholder with no text.

This prevents disclosure through the runtime API only. It is not protection against someone extracting cooked assets.

**Search index.** Search runs over a per-owner visible index. The index is built on the game thread from copies of the text, lowercased and sorted with culture awareness, and is versioned by owner revision, linked shared revision, culture generation and catalog generation.

- **Results.** Results are paginated and bounded, and `TotalMatches` counts only visible matches.
- **Async search.** `SearchEntries` matches on a worker thread using only the copied data. A stale result is reported as a Conflict and never applied.
- **Cancellation.** `CancelSearch` and `RemoveOwner` cancel with exactly one callback.
- **Culture changes** invalidate the index; record ids stay the same.

## Media

Images, audio, video and external sources are optional descriptors.

- **No loading or playback.** The base never loads or plays them.
- **Always a text fallback.** Each descriptor carries a caption, a transcript and a `FallbackText` (transcript, caption or title).
- **Availability** comes from a configured `IDocKnowledgeMediaProvider`. Without one, a declared asset path counts as available and external sources count as unavailable; external URLs are never opened or executed.

## Owners

Scopes are separate.

**Shared discoveries.** `LinkSharedScope(Player, Shared)` lets a player see a shared scope's discoveries, while read state stays per player. With linked scopes, `RevokeReveal`'s read adjustment applies only to the scope being revoked.

## Persistence

`CaptureState` and `RestoreState` cover per-owner entry states, receipts and shared links.

**Restore flow:**

1. **Validate** the save.
2. **Migrate.** Entries are redirected through catalog `EntryRedirects` and states for the same entry are merged. An entry whose definition was removed becomes a tombstone: it is hidden from player APIs, preserved in future saves, and never inferred complete. A newer content revision marks the entry unread, but raises no event and adds no update count.
3. **Apply and refresh.** Searches in flight are cancelled, and one `OnStateRefreshed` is sent. There are no discovery toasts and no rewards.

## Tests

The tests are under `Doc.Knowledge.*`:

- Discover
- Update
- MultiStage
- Relationships
- MarkRead
- SaveRestore
- SearchPrivacy
- CultureChange
- AsyncSearch
- MediaFallback
- OwnerScope

These still need a packaged build or profiling:

- a cooked second culture (KNO-08)
- large-catalog budgets (KNO-12)
