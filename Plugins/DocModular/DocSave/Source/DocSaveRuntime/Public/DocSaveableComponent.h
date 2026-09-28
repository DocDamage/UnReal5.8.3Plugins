#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interfaces/DocPersistentIdentity.h"
#include "DocSaveTypes.h"
#include "DocSaveableComponent.generated.h"

/**
 * Makes its owning actor persistent (handoff 13.2).
 *
 * Identity = (WorldNamespace, InstanceScope, LocalObjectGuid):
 * - LocalObjectGuid is authored and serialized; regenerated only on editor
 *   duplicate/paste; preserved on load, stream-in and PIE duplication.
 * - WorldNamespace defaults to a deterministic GUID of the owning world's package
 *   (renaming a map therefore needs a migration). Override with bOverrideWorldNamespace.
 * - InstanceScope is set by whatever placed the actor inside a repeated level
 *   instance (e.g. from DocStreaming's instance key via FDocPersistentObjectId::ComposeInstanceScope).
 *
 * Runtime-spawned actors get an id once from the authority; on load, the save
 * system spawns them (allowlisted classes only) and assigns their saved id before
 * BeginPlay registration.
 */
UCLASS(ClassGroup = (Doc), meta = (BlueprintSpawnableComponent))
class DOCSAVERUNTIME_API UDocSaveableComponent : public UActorComponent, public IDocPersistentIdentity
{
	GENERATED_BODY()

public:
	UDocSaveableComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Save")
	bool bSaveTransform = true;

	/** Save hidden/collision state as "enabled". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Save")
	bool bSaveEnabledState = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Save")
	bool bOverrideWorldNamespace = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Save", meta = (EditCondition = "bOverrideWorldNamespace"))
	FGuid WorldNamespaceOverride;

	/** Set before BeginPlay (e.g. by a level-instance spawner). */
	UFUNCTION(BlueprintCallable, Category = "Doc|Save")
	void SetInstanceScope(const FGuid& InScope);

	/** Assign a saved identity (restore path) or an authority-issued one (runtime spawn). Only before registration. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Save")
	bool AssignPersistentId(const FDocPersistentObjectId& Id);

	UFUNCTION(BlueprintPure, Category = "Doc|Save")
	bool IsRuntimeSpawned() const { return bRuntimeSpawned; }

	/**
	 * Explicitly classify the owner before registration. Without this, BeginPlay treats any
	 * actor that is not a level (net-startup) actor as runtime-spawned. Spawners that
	 * deterministically recreate authored content pass false.
	 */
	void MarkRuntimeSpawned(bool bInRuntimeSpawned) { if (!bRegistered) { bRuntimeSpawned = bInRuntimeSpawned; bRuntimeSpawnedAssigned = true; } }

	/** Mark as intentionally and permanently destroyed, then destroy the owner. Authority only. */
	UFUNCTION(BlueprintCallable, Category = "Doc|Save")
	FDocSystemResult DestroyPersistently();

	/** Capture this actor's full record (transform, enabled state, participant payloads). */
	FDocSaveObjectRecord CaptureRecord() const;

	/** Apply a record's transform/enabled state and dispatch payloads to participants. Returns false if any payload failed. */
	bool ApplyRecord(const FDocSaveObjectRecord& Record, TArray<FString>& OutErrors);

	/** Dispatch the second restore phase to participants. */
	void ResolveReferences();

	//~ IDocPersistentIdentity
	virtual FDocPersistentObjectId GetDocPersistentId_Implementation() const override;

	virtual void PostInitProperties() override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
#if WITH_EDITOR
	virtual void PostEditImport() override;
#endif
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void GatherParticipants(TArray<UObject*>& Out) const;
	FGuid ResolveWorldNamespace() const;

	UPROPERTY(VisibleAnywhere, Category = "Save")
	FGuid LocalObjectGuid;

	UPROPERTY(VisibleAnywhere, Category = "Save")
	FGuid InstanceScope;

	/** Runtime-created (not loaded from a level). Serialized so re-saves keep the flag. */
	UPROPERTY(VisibleAnywhere, Category = "Save")
	bool bRuntimeSpawned = false;

	bool bRegistered = false;
	bool bRuntimeSpawnedAssigned = false;
	bool bExplicitNamespaceAssigned = false;
	FGuid AssignedNamespace;
};
