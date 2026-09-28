#include "DocAssemblySubsystem.h"

void UDocAssemblySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UDocAssemblySubsystem::Deinitialize()
{
	RegisteredAssemblies.Empty();
	Super::Deinitialize();
}

void UDocAssemblySubsystem::RegisterAssembly(UDocAssemblyComponent* Component)
{
	if (Component && !RegisteredAssemblies.Contains(Component))
	{
		RegisteredAssemblies.Add(Component);
	}
}

void UDocAssemblySubsystem::UnregisterAssembly(UDocAssemblyComponent* Component)
{
	RegisteredAssemblies.Remove(Component);
}

UDocAssemblyComponent* UDocAssemblySubsystem::FindAssembly(FName AssemblyId) const
{
	for (UDocAssemblyComponent* Comp : RegisteredAssemblies)
	{
		if (Comp && Comp->GetAssemblyId() == AssemblyId)
		{
			return Comp;
		}
	}
	return nullptr;
}
