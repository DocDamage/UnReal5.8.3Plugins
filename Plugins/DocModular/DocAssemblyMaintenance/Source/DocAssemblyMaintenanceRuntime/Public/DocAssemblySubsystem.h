#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DocAssemblyComponent.h"
#include "DocAssemblySubsystem.generated.h"

UCLASS()
class DOCASSEMBLYMAINTENANCERUNTIME_API UDocAssemblySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void RegisterAssembly(UDocAssemblyComponent* Component);
	void UnregisterAssembly(UDocAssemblyComponent* Component);

	UFUNCTION(BlueprintPure, Category = "Assembly")
	UDocAssemblyComponent* FindAssembly(FName AssemblyId) const;

	UFUNCTION(BlueprintPure, Category = "Assembly")
	const TArray<UDocAssemblyComponent*>& GetAllAssemblies() const { return RegisteredAssemblies; }

private:
	UPROPERTY()
	TArray<TObjectPtr<UDocAssemblyComponent>> RegisteredAssemblies;
};
