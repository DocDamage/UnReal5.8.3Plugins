#include "DocGameplayContext.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Actor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocGameplayContext)

FDocGameplayContext FDocGameplayContext::Make(UWorld* InWorld, AActor* InInstigator, AActor* InTarget)
{
	FDocGameplayContext Context;
	Context.World = InWorld;
	Context.Instigator = InInstigator;
	Context.Target = InTarget;
	return Context;
}

bool FDocGameplayContext::IsValidForWorld(const UWorld* ExpectedWorld) const
{
	const UWorld* ContextWorld = World.Get();
	if (!ContextWorld || !ExpectedWorld || ContextWorld != ExpectedWorld)
	{
		return false;
	}

	// A set-but-dead weak pointer means a stale context.
	auto ActorOk = [ContextWorld](const TWeakObjectPtr<AActor>& Ptr)
	{
		if (Ptr.IsExplicitlyNull())
		{
			return true;
		}
		const AActor* Actor = Ptr.Get();
		return Actor && Actor->GetWorld() == ContextWorld;
	};

	if (!ActorOk(Instigator) || !ActorOk(Target))
	{
		return false;
	}

	if (!LocalPlayer.IsExplicitlyNull())
	{
		const ULocalPlayer* Player = LocalPlayer.Get();
		if (!Player || Player->GetWorld() != ContextWorld)
		{
			return false;
		}
	}
	return true;
}

EDocNetAuthority FDocGameplayContext::ResolveAuthority() const
{
	const UWorld* ContextWorld = World.Get();
	if (!ContextWorld)
	{
		return EDocNetAuthority::Unknown;
	}

	const ENetMode NetMode = ContextWorld->GetNetMode();
	if (NetMode == NM_Standalone)
	{
		return EDocNetAuthority::Standalone;
	}

	if (const AActor* InstigatorActor = Instigator.Get())
	{
		return InstigatorActor->HasAuthority() ? EDocNetAuthority::Authority : EDocNetAuthority::Remote;
	}

	return NetMode == NM_Client ? EDocNetAuthority::Remote : EDocNetAuthority::Authority;
}

bool FDocGameplayContext::HasAuthority() const
{
	const EDocNetAuthority Authority = ResolveAuthority();
	return Authority == EDocNetAuthority::Standalone || Authority == EDocNetAuthority::Authority;
}
