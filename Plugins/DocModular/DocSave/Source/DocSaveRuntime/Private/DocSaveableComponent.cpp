#include "DocSaveableComponent.h"
#include "DocSaveGameSubsystem.h"
#include "DocSaveLog.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocSaveableComponent)

UDocSaveableComponent::UDocSaveableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDocSaveableComponent::PostInitProperties()
{
	Super::PostInitProperties();
	if (!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject | RF_NeedLoad | RF_WasLoaded) && !LocalObjectGuid.IsValid())
	{
		LocalObjectGuid = FGuid::NewGuid();
	}
}

void UDocSaveableComponent::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);
	if (!bDuplicateForPIE)
	{
		LocalObjectGuid = FGuid::NewGuid(); // editor duplicate = new authored identity
	}
}

#if WITH_EDITOR
void UDocSaveableComponent::PostEditImport()
{
	Super::PostEditImport();
	LocalObjectGuid = FGuid::NewGuid(); // paste
}
#endif

void UDocSaveableComponent::SetInstanceScope(const FGuid& InScope)
{
	if (bRegistered)
	{
		UE_LOG(LogDocSave, Warning, TEXT("%s: SetInstanceScope after registration is ignored"), *GetPathName());
		return;
	}
	InstanceScope = InScope;
}

bool UDocSaveableComponent::AssignPersistentId(const FDocPersistentObjectId& Id)
{
	if (bRegistered || !Id.IsValid())
	{
		return false;
	}
	LocalObjectGuid = Id.LocalObjectGuid;
	InstanceScope = Id.InstanceScope;
	AssignedNamespace = Id.WorldNamespace;
	bExplicitNamespaceAssigned = true;
	return true;
}

FGuid UDocSaveableComponent::ResolveWorldNamespace() const
{
	if (bExplicitNamespaceAssigned)
	{
		return AssignedNamespace;
	}
	if (bOverrideWorldNamespace && WorldNamespaceOverride.IsValid())
	{
		return WorldNamespaceOverride;
	}
	const UWorld* World = GetWorld();
	if (!World)
	{
		return FGuid();
	}
	// Stable across PIE: strip the PIE prefix from the package name.
	const FString PackageName = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
	return FGuid::NewDeterministicGuid(PackageName);
}

FDocPersistentObjectId UDocSaveableComponent::GetDocPersistentId_Implementation() const
{
	return FDocPersistentObjectId(ResolveWorldNamespace(), InstanceScope, LocalObjectGuid);
}

void UDocSaveableComponent::GatherParticipants(TArray<UObject*>& Out) const
{
	Out.Reset();
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	if (Owner->Implements<UDocSaveParticipant>() || Cast<IDocSaveParticipant>(Owner))
	{
		Out.Add(Owner);
	}
	for (UActorComponent* Component : Owner->GetComponents())
	{
		if (Component && Component != this && (Component->Implements<UDocSaveParticipant>() || Cast<IDocSaveParticipant>(Component)))
		{
			Out.Add(Component);
		}
	}
}

FDocSaveObjectRecord UDocSaveableComponent::CaptureRecord() const
{
	FDocSaveObjectRecord Record;
	Record.Id = GetDocPersistentId_Implementation();
	Record.bRuntimeSpawned = bRuntimeSpawned;
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return Record;
	}
	if (bRuntimeSpawned)
	{
		Record.SpawnClass = FSoftClassPath(Owner->GetClass());
	}
	if (bSaveTransform)
	{
		Record.bHasTransform = true;
		Record.Transform = Owner->GetActorTransform();
	}
	if (bSaveEnabledState)
	{
		Record.bHasEnabledState = true;
		Record.bEnabled = !Owner->IsHidden() && Owner->GetActorEnableCollision();
	}

	const UDocSaveGameSubsystem* Save = UDocSaveGameSubsystem::Get(this);
	TArray<UObject*> Participants;
	GatherParticipants(Participants);
	for (UObject* Participant : Participants)
	{
		TArray<FDocSavePayload> Payloads;
		IDocSaveParticipant::Execute_CaptureDocSaveState(Participant, Payloads);
		for (const FDocSavePayload& Payload : Payloads)
		{
			FDocSaveComponentRecord Encoded;
			const FDocSystemResult Result = Save ? Save->EncodePayload(Payload, Encoded)
				: FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("No save subsystem"));
			if (Result.IsSuccess())
			{
				Record.Components.Add(MoveTemp(Encoded));
			}
			else
			{
				UE_LOG(LogDocSave, Error, TEXT("%s: payload %s not saved: %s"), *GetPathNameSafe(Participant), *Payload.ComponentKey.ToString(), *Result.ToString());
			}
		}
	}
	return Record;
}

bool UDocSaveableComponent::ApplyRecord(const FDocSaveObjectRecord& Record, TArray<FString>& OutErrors)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		OutErrors.Add(TEXT("Owner missing"));
		return false;
	}
	if (Record.bHasTransform && bSaveTransform)
	{
		Owner->SetActorTransform(Record.Transform, false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (Record.bHasEnabledState && bSaveEnabledState)
	{
		Owner->SetActorHiddenInGame(!Record.bEnabled);
		Owner->SetActorEnableCollision(Record.bEnabled);
	}

	const UDocSaveGameSubsystem* Save = UDocSaveGameSubsystem::Get(this);
	TArray<UObject*> Participants;
	GatherParticipants(Participants);
	bool bAllOk = true;
	for (const FDocSaveComponentRecord& Component : Record.Components)
	{
		FDocSavePayload Payload;
		const FDocSystemResult Decoded = Save ? Save->DecodePayload(Component, Payload)
			: FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("No save subsystem"));
		if (!Decoded.IsSuccess())
		{
			OutErrors.Add(FString::Printf(TEXT("%s/%s: %s"), *Record.Id.ToString(), *Component.ComponentKey.ToString(), *Decoded.Diagnostic));
			bAllOk = false;
			continue;
		}
		bool bConsumed = false;
		for (UObject* Participant : Participants)
		{
			if (IDocSaveParticipant::Execute_RestoreDocSaveState(Participant, Payload))
			{
				bConsumed = true;
				break;
			}
		}
		if (!bConsumed)
		{
			OutErrors.Add(FString::Printf(TEXT("%s/%s: no participant accepted the payload"), *Record.Id.ToString(), *Component.ComponentKey.ToString()));
			bAllOk = false;
		}
	}
	return bAllOk;
}

void UDocSaveableComponent::ResolveReferences()
{
	TArray<UObject*> Participants;
	GatherParticipants(Participants);
	for (UObject* Participant : Participants)
	{
		IDocSaveParticipant::Execute_ResolveDocSaveReferences(Participant);
	}
}

FDocSystemResult UDocSaveableComponent::DestroyPersistently()
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::PermissionDenied, TEXT("Persistent destruction requires authority"));
	}
	UDocSaveGameSubsystem* Save = UDocSaveGameSubsystem::Get(this);
	if (!Save)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::Unavailable, TEXT("No save subsystem"));
	}
	const FDocSystemResult Result = Save->MarkPersistentlyDestroyed(GetDocPersistentId_Implementation());
	Owner->Destroy();
	return Result;
}

void UDocSaveableComponent::OnRegister()
{
	Super::OnRegister();
}

void UDocSaveableComponent::OnUnregister()
{
	if (!IsTemplate() && bRegistered)
	{
		if (UDocSaveGameSubsystem* Save = UDocSaveGameSubsystem::Get(this))
		{
			Save->UnregisterSaveable(this, EDocSaveLifecycle::StreamedOut);
		}
		bRegistered = false;
	}
	Super::OnUnregister();
}

void UDocSaveableComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!IsTemplate() && !bRegistered)
	{
		const AActor* Owner = GetOwner();
		if (!bRuntimeSpawnedAssigned && Owner && !Owner->IsNetStartupActor())
		{
			bRuntimeSpawned = true;
		}
		if (UDocSaveGameSubsystem* Save = UDocSaveGameSubsystem::Get(this))
		{
			bRegistered = Save->RegisterSaveable(this);
		}
	}
}

void UDocSaveableComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bRegistered)
	{
		if (UDocSaveGameSubsystem* Save = UDocSaveGameSubsystem::Get(this))
		{
			// EndPlay is never proof of intentional destruction (handoff 13.4).
			Save->UnregisterSaveable(this, EDocSaveLifecycle::StreamedOut);
		}
		bRegistered = false;
	}
	Super::EndPlay(EndPlayReason);
}
