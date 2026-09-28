# Engine Compatibility

Facts in this file were read from the installed engine on 2026-09-26. Nothing here claims that DocModular code has compiled.

## Engine identity

| Item | Value | How verified |
|---|---|---|
| Install path | `C:\Program Files\UE_5.8` | Given by the developer; directory inspected |
| Version | 5.8.3 | `Engine/Build/Build.version` |
| Changelist | 58210709 | `Build.version` |
| CompatibleChangelist | 55116800 | `Build.version` |
| Branch | `++UE5+Release-5.8` | `Build.version` |
| Promoted / licensee build | IsPromotedBuild = 1, IsLicenseeVersion = 0 | `Build.version` |
| Editor binaries | `Engine/Binaries/Win64/UnrealEditor.exe`, `UnrealEditor-Cmd.exe` present | Directory listing |
| Build scripts | `Engine/Build/BatchFiles/Build.bat`, `RunUAT.bat` present | Directory listing |
| `.uproject` association | `"EngineAssociation": "5.8"` in `DocModularDev.uproject` | **Verified**: resolves and builds cleanly with UE 5.8.3. |

## Toolchain

| Item | Value |
|---|---|
| Engine-preferred MSVC | 14.50.35717–14.50.x (VS 2026 18.0) or 14.44.35207–14.44.x (VS 2022 17.14), per `Engine/Config/Windows/Windows_SDK.json` |
| Known-bad MSVC | 14.50.35717 on VS 2026 18.0.0–18.2.0 (ICEs, fixed in 14.50.35723); see `BannedVisualCppVersions` |
| Windows SDK | Main 10.0.22621.0, minimum 10.0.19041.0 (Installed: 10.0.26100.0) |
| Installed compiler on this machine | Visual Studio 14.44.35227 (`14.44.35207` BuildTools), Windows SDK `10.0.26100.0`, ISPC `1.24.0`. Verified working. |

## Build settings used by the dev host

| Setting | Value | Source |
|---|---|---|
| `DefaultBuildSettings` | `BuildSettingsVersion.Latest` (= V7 in this engine) | `UnrealBuildTool/Configuration/Rules/TargetRules.cs` |
| `IncludeOrderVersion` | `EngineIncludeOrderVersion.Latest` (= Unreal5_8; oldest supported Unreal5_6) | same |

## APIs and modules verified in installed headers

| API / type | Owning module / header | Notes |
|---|---|---|
| `FInstancedStruct` | CoreUObject — `Runtime/CoreUObject/Public/StructUtils/InstancedStruct.h` | The `StructUtils` plugin still ships under `Plugins/Experimental/StructUtils` but is `"DeprecatedEngineVersion": "5.5"` and disabled by default. **Do not depend on it.** Use CoreUObject. |
| `UE_DECLARE_GAMEPLAY_TAG_EXTERN` / `UE_DEFINE_GAMEPLAY_TAG_COMMENT` | GameplayTags — `Runtime/GameplayTags/Public/NativeGameplayTags.h` | DEFINE macros static_assert they are used in a `.cpp` |
| `UGameplayTagsManager::RequestGameplayTag(FName, bool)` | GameplayTags — `Classes/GameplayTagsManager.h` | |
| `IGameplayTagAssetInterface` | GameplayTags | Not Blueprint-implementable; reason `IDocGameplayTagProvider` exists |
| `UDeveloperSettings` | DeveloperSettings module | Not used yet (no shared settings exist) |
| `EAutomationTestFlags` | Core — `Misc/AutomationTest.h` | `enum class`; use `EAutomationTestFlags_ApplicationContextMask` (inline constexpr) |
| `IMPLEMENT_SIMPLE_AUTOMATION_TEST` | Core — `Misc/AutomationTest.h` | Generic `TestEqual`/`TestNotEqual` require `operator!=`/`operator==` |
| `UWorld::CreateWorld(EWorldType::Type, bool, ...)` / `DestroyWorld(bool, UWorld*)` | Engine — `Classes/Engine/World.h` | `DestroyWorld` calls `RemoveFromRoot()`; same pattern as `EngineAutomationTests.cpp` |
| `FProperty::ShouldSerializeValue` | CoreUObject — `Private/UObject/Property.cpp` | Skips `CPF_Transient` when `Ar.IsPersistent()`; basis for `FDocRequestHandle` non-durability |
| `FSHA1::HashBuffer(const void*, uint64, uint8*)` | Core — `Misc/SecureHash.h` | Used for instance-scope composition |
| `FGuid::ParseExact(FStringView, EGuidFormats, FGuid&)` | Core — `Misc/Guid.h` | |
| `FObjectKey` | CoreUObject — `UObject/ObjectKey.h` | Scope keys in `TDocHandleTable` |

## Resolved
- Installed MSVC version: MSVC 14.44.35227 (VS 2022 17.14 BuildTools), Windows SDK 10.0.26100.0, ISPC 1.24.0.
- `5.8` association: Successfully resolved and accepted by UBT/UHT.
- Compilation: All 4 targets (Editor unity, Editor non-unity, Game Development, Game Shipping) compiled and passed cleanly.
- M1.1 (DocModularCore): 12/12 automated tests passed on NullRHI.
