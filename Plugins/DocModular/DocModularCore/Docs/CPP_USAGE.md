# DocModularCore — C++ Usage

```csharp
// YourModule.Build.cs
PrivateDependencyModuleNames.Add("DocModularCoreRuntime");
```

```cpp
#include "DocSystemResult.h"
#include "DocRequestHandle.h"
#include "DocCoreTags.h"

// A world subsystem owning leases:
struct FMyLease { TWeakObjectPtr<UObject> Owner; };
TDocHandleTable<FMyLease> Leases;

FDocRequestHandle UMySubsystem::Acquire(UObject* Owner)
{
    return Leases.Add(GetWorld(), FMyLease{ Owner });
}

FDocSystemResult UMySubsystem::Release(const FDocRequestHandle& Handle)
{
    FMyLease Removed;
    if (!Leases.Remove(Handle, GetWorld(), &Removed))
    {
        return FDocSystemResult::MakeFailure(EDocResultOutcome::NotFound,
            FString::Printf(TEXT("Release of %s: %s"), *Handle.ToString(),
                *UEnum::GetValueAsString(Leases.Validate(Handle, GetWorld()))),
            DocCoreTags::Error_Handle_Stale);
    }
    // release Removed's resources here
    return FDocSystemResult::MakeSuccess(Handle.GetOperationId());
}

void UMySubsystem::Deinitialize()
{
    Leases.Reset(); // after releasing every payload's resources
}
```

Persistent identity for a placed level instance nested in another:

```cpp
const FGuid OuterScope = FDocPersistentObjectId::ComposeInstanceScope(FGuid(), OuterPlacementGuid);
const FGuid InnerScope = FDocPersistentObjectId::ComposeInstanceScope(OuterScope, InnerPlacementGuid);
const FDocPersistentObjectId Id(WorldNamespace, InnerScope, AuthoredLocalGuid);
```

Calling interfaces (works for C++ and Blueprint implementers):

```cpp
if (Object->Implements<UDocPersistentIdentity>())
{
    const FDocPersistentObjectId Id = IDocPersistentIdentity::Execute_GetDocPersistentId(Object);
}
```
