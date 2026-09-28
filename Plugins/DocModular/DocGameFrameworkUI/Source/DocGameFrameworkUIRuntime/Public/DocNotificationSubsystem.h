#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "DocUIProviders.h"
#include "DocNotificationSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDocNotificationsChangedEvent);

/**
 * Per-local-player notification queue (NotificationSubsystem, handoff 12.8).
 *
 * - Bounded visible and queued counts; stable priority then FIFO ordering.
 * - Anti-starvation aging: queued entries gain effective priority over time, so high-priority
 *   traffic cannot starve lower-priority messages indefinitely (configurable; 0 = strict).
 * - Merge keys are scoped by owner; merging updates presentation only and never repeats the
 *   gameplay action that produced the notification.
 * - Durations and TTLs run on the real-time clock (they keep counting while the game is paused).
 * - Overflow drops the lowest-priority, oldest droppable entry; Persistent entries are never
 *   dropped, and a request that cannot fit is rejected with Conflict.
 * - Actions resolve an approved handler by key and revalidate before executing exactly once.
 */
UCLASS()
class DOCGAMEFRAMEWORKUIRUNTIME_API UDocNotificationSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	static UDocNotificationSubsystem* Get(const ULocalPlayer* LocalPlayer);

	UFUNCTION(BlueprintCallable, Category = "Doc|UI")
	FDocSystemResult Enqueue(FDocNotificationRequest Request, FGuid& OutId);
	UFUNCTION(BlueprintCallable, Category = "Doc|UI")
	FDocSystemResult EnqueueDefinition(const UDocNotificationDefinition* Definition, const FDocOwnerScope& Owner, FGuid& OutId);
	UFUNCTION(BlueprintCallable, Category = "Doc|UI")
	FDocSystemResult Dismiss(const FGuid& Id);
	/** Runs the entry's approved action once; later calls return NoChange. */
	UFUNCTION(BlueprintCallable, Category = "Doc|UI")
	FDocSystemResult InvokeAction(const FGuid& Id);
	UFUNCTION(BlueprintCallable, Category = "Doc|UI")
	void ClearAll();

	void RegisterActionHandler(FName Key, TSharedPtr<IDocNotificationActionHandler> Handler);
	/** Owned sound provider; called once when an entry first becomes visible. */
	void SetSoundPlayer(TFunction<void(const FDocNotificationEntry&)> InPlayer) { SoundPlayer = MoveTemp(InPlayer); }

	UFUNCTION(BlueprintPure, Category = "Doc|UI")
	TArray<FDocNotificationEntry> GetVisible() const;
	UFUNCTION(BlueprintPure, Category = "Doc|UI")
	TArray<FDocNotificationEntry> GetQueued() const;
	bool FindEntry(const FGuid& Id, FDocNotificationEntry& OutEntry) const;
	int32 GetDroppedCount() const { return Dropped; }

	/** Real-time step; auto-driven by the core ticker unless disabled in settings. */
	void AdvanceClock(double Seconds);

	UPROPERTY(BlueprintAssignable, Category = "Doc|UI") FDocNotificationsChangedEvent OnNotificationsChanged;
	FSimpleMulticastDelegate OnNotificationsChangedNative;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	int32 EffectivePriority(const FDocNotificationEntry& E) const;
	FDocNotificationEntry* Find(const FGuid& Id);
	bool IsDroppable(const FDocNotificationEntry& E) const;
	void Promote();
	void Show(FDocNotificationEntry& E);
	void Changed();
	bool Tick(float DeltaSeconds);

	TArray<FDocNotificationEntry> Entries; // visible and queued
	TSet<FGuid> SeenIds;
	TSet<FGuid> Announced;
	TArray<FGuid> SeenOrder;
	TMap<FName, TSharedPtr<IDocNotificationActionHandler>> Handlers;
	TFunction<void(const FDocNotificationEntry&)> SoundPlayer;
	int64 NextSequence = 0;
	int32 Dropped = 0;
	bool bInAction = false;
	FTSTicker::FDelegateHandle TickHandle;
};
