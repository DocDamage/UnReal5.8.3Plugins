#pragma once

// Development-only helpers for DocModular automation tests. Nothing here is
// compiled into Shipping (guarded by WITH_DEV_AUTOMATION_TESTS).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"

/**
 * Creates an initialized, unregistered game world (world subsystems are created by
 * InitWorld) and destroys it on scope exit. Worlds created this way do not tick;
 * tests drive subsystems explicitly.
 */
struct FDocScopedTestWorld
{
	UWorld* World = nullptr;

	explicit FDocScopedTestWorld(EWorldType::Type Type = EWorldType::Game)
	{
		World = UWorld::CreateWorld(Type, /*bInformEngineOfWorld*/ false);
		if (World)
		{
			FWorldContext& Context = GEngine->CreateNewWorldContext(Type);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}
	}

	~FDocScopedTestWorld()
	{
		if (World)
		{
			if (World->HasBegunPlay())
			{
				World->BeginTearingDown();
				World->EndPlay(EEndPlayReason::Quit);
			}
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(/*bInformEngineOfWorld*/ false);
			World = nullptr;
		}
	}

	FDocScopedTestWorld(const FDocScopedTestWorld&) = delete;
	FDocScopedTestWorld& operator=(const FDocScopedTestWorld&) = delete;

	template <typename T>
	T* Spawn(const FVector& Location = FVector::ZeroVector)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World ? World->SpawnActor<T>(T::StaticClass(), FTransform(Location), Params) : nullptr;
	}

	template <typename T>
	T* GetSubsystem() const { return World ? World->GetSubsystem<T>() : nullptr; }
};

#endif // WITH_DEV_AUTOMATION_TESTS
