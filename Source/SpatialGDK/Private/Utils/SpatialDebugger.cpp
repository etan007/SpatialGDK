// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "Utils/SpatialDebugger.h"

#include "EngineClasses/SpatialNetDriver.h"
#include "EngineClasses/SpatialVirtualWorkerTranslator.h"
#include "EngineClasses/SpatialWorldSettings.h"
#include "Interop/Connection/SpatialWorkerConnection.h"
#include "Interop/SpatialReceiver.h"
#include "Interop/SpatialSender.h"
#include "LoadBalancing/GridBasedLBStrategy.h"
#include "LoadBalancing/LayeredLBStrategy.h"
#include "LoadBalancing/WorkerRegion.h"
#include "Schema/SpatialDebugging.h"
#include "SpatialCommonTypes.h"
#include "SpatialConstants.h"
#include "Utils/InspectionColors.h"
#include "Utils/SpatialDebuggerSystem.h"

#include "Debug/DebugDrawService.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "GenericPlatform/GenericPlatformMath.h"
#include "Kismet/GameplayStatics.h"
#include "Modules/ModuleManager.h"
#include "Net/UnrealNetwork.h"

#if UE_EDITOR
#include "Editor.h"
#endif

using namespace SpatialGDK;

DEFINE_LOG_CATEGORY(LogSpatialDebugger);

namespace
{
// Background material for worker region
const FString DEFAULT_WORKER_REGION_MATERIAL =
	TEXT("/SpatialGDK/SpatialDebugger/Materials/TranslucentWorkerRegion.TranslucentWorkerRegion");
const FString DEFAULT_WIREFRAME_MATERIAL = TEXT("/SpatialGDK/SpatialDebugger/Materials/GlowingWireframeMaterial.GlowingWireframeMaterial");
// Improbable primary font - Muli regular
const FString DEFAULT_WORKER_TEXT_FONT = TEXT("/SpatialGDK/SpatialDebugger/Fonts/MuliFont.MuliFont");
// Material to combine both the background and the worker information in one material
const FString DEFAULT_WORKER_COMBINED_MATERIAL =
	TEXT("/SpatialGDK/SpatialDebugger/Materials/WorkerRegionCombinedMaterial.WorkerRegionCombinedMaterial");
} // anonymous namespace

ASpatialDebugger::ASpatialDebugger(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	bAlwaysRelevant = true;
	bNetLoadOnClient = false;
	bReplicates = true;

	NetUpdateFrequency = 1.f;

	HoverIndex = 0;

	NetDriver = Cast<USpatialNetDriver>(GetNetDriver());

	OnConfigUIClosed.BindDynamic(this, &ASpatialDebugger::DefaultOnConfigUIClosed);
}

void ASpatialDebugger::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(ASpatialDebugger, WorkerRegions, COND_SimulatedOnly);
}

void ASpatialDebugger::BeginPlay()
{
	Super::BeginPlay();

	if (!ensureAlwaysMsgf(NetDriver != nullptr, TEXT("Failed to call BeginPlay on SpatialDebugger. NetDriver was nullptr")))
	{
		return;
	}

	NetDriver->RegisterSpatialDebugger(this);

	if (!NetDriver->IsServer())
	{
		GetDebuggerSystem()->OnEntityActorAddedDelegate.AddUObject(this, &ASpatialDebugger::OnEntityAdded);

		for (const TPair<Worker_EntityId_Key, TWeakObjectPtr<AActor>>& PresentActorPair : GetDebuggerSystem()->GetActors())
		{
			AActor* PresentActor = PresentActorPair.Value.Get();

			if (!ensureAlwaysMsgf(IsValid(PresentActor), TEXT("Actor was invalid when iterating through debugger system")))
			{
				continue;
			}

			OnEntityAdded(PresentActor);
		}

		LoadIcons();

		FontRenderInfo.bClipText = true;
		FontRenderInfo.bEnableShadow = true;

		RenderFont = GEngine->GetSmallFont();

		if (bAutoStart)
		{
			SpatialToggleDebugger();
		}
		WireFrameMaterial = LoadObject<UMaterial>(nullptr, *DEFAULT_WIREFRAME_MATERIAL);
		if (WireFrameMaterial == nullptr)
		{
			UE_LOG(LogSpatialDebugger, Warning, TEXT("SpatialDebugger enabled but unable to get WireFrame Material."));
		}
	}
}

void ASpatialDebugger::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!ensureAlwaysMsgf(NetDriver != nullptr, TEXT("Failed to call SpatialDebugger::Tick. NetDriver was nullptr")))
	{
		return;
	}

	if (!NetDriver->IsServer())
	{
		// Since we have no guarantee on the order we'll receive the PC/Pawn/PlayerState
		// over the wire, we check here once per tick (currently 1 Hz tick rate) to setup our local pointers.
		// Note that we can capture the PC in OnEntityAdded() since we know we will only receive one of those.
		if (LocalPawn.IsValid() == false && LocalPlayerController.IsValid())
		{
			LocalPawn = LocalPlayerController->GetPawn();
		}

		if (LocalPlayerState.IsValid() == false && LocalPawn.IsValid())
		{
			LocalPlayerState = LocalPawn->GetPlayerState();
		}
	}
}

void ASpatialDebugger::OnEntityAdded(AActor* Actor)
{
	// Each client will only receive a PlayerController once.
	if (Actor->IsA<APlayerController>())
	{
		LocalPlayerController = Cast<APlayerController>(Actor);

		if (GetNetMode() == NM_Client)
		{
			LocalPlayerController->InputComponent->BindKey(ConfigUIToggleKey, IE_Pressed, this, &ASpatialDebugger::OnToggleConfigUI)
				.bConsumeInput = false;
			LocalPlayerController->InputComponent->BindKey(SelectActorKey, IE_Pressed, this, &ASpatialDebugger::OnSelectActor)
				.bConsumeInput = false;
			LocalPlayerController->InputComponent->BindKey(HighlightActorKey, IE_Pressed, this, &ASpatialDebugger::OnHighlightActor)
				.bConsumeInput = false;
		}
	}
}

void ASpatialDebugger::OnAuthorityGained()
{
	if (UAbstractLBStrategy* LoadBalanceStrategy = Cast<UAbstractLBStrategy>(NetDriver->LoadBalanceStrategy))
	{
		if (const UGridBasedLBStrategy* GridBasedLBStrategy =
				Cast<UGridBasedLBStrategy>(LoadBalanceStrategy->GetLBStrategyForVisualRendering()))
		{
			const UGridBasedLBStrategy::LBStrategyRegions LBStrategyRegions = GridBasedLBStrategy->GetLBStrategyRegions();
			WorkerRegions.SetNum(LBStrategyRegions.Num());
			for (int i = 0; i < LBStrategyRegions.Num(); i++)
			{
				FWorkerRegionInfo WorkerRegionInfo;
				const TPair<VirtualWorkerId, FBox2D>& LBStrategyRegion = LBStrategyRegions[i];
				WorkerRegionInfo.VirtualWorkerID = LBStrategyRegion.Key;
				const PhysicalWorkerName* WorkerName =
					NetDriver->VirtualWorkerTranslator->GetPhysicalWorkerForVirtualWorker(LBStrategyRegion.Key);
				WorkerRegionInfo.WorkerName = (WorkerName == nullptr) ? "" : *WorkerName;
				WorkerRegionInfo.Color = (WorkerName == nullptr) ? InvalidServerTintColor : SpatialGDK::GetColorForWorkerName(*WorkerName);
				WorkerRegionInfo.Extents = LBStrategyRegion.Value;
				WorkerRegions[i] = WorkerRegionInfo;
			}
		}
	}
}

void ASpatialDebugger::CreateWorkerRegions()
{
	UMaterial* WorkerRegionMaterial = LoadObject<UMaterial>(nullptr, *DEFAULT_WORKER_REGION_MATERIAL);
	if (WorkerRegionMaterial == nullptr)
	{
		UE_LOG(LogSpatialDebugger, Error, TEXT("Worker regions were not rendered. Could not find default material: %s"),
			   *DEFAULT_WORKER_REGION_MATERIAL);
		return;
	}

	UMaterial* WorkerCombinedMaterial = LoadObject<UMaterial>(nullptr, *DEFAULT_WORKER_COMBINED_MATERIAL);
	if (WorkerCombinedMaterial == nullptr)
	{
		UE_LOG(LogSpatialDebugger, Error, TEXT("Worker regions were not rendered. Could not find default material: %s"),
			   *DEFAULT_WORKER_COMBINED_MATERIAL);
	}

	UFont* WorkerInfoFont = LoadObject<UFont>(nullptr, *DEFAULT_WORKER_TEXT_FONT);
	if (WorkerInfoFont == nullptr)
	{
		UE_LOG(LogSpatialDebugger, Error, TEXT("Worker information was not rendered. Could not find default font: %s"),
			   *DEFAULT_WORKER_TEXT_FONT);
	}

	// Create new actors for all new worker regions
	FActorSpawnParameters SpawnParams;
	SpawnParams.bNoFail = true;
	UWorld* World = GetWorld();
#if WITH_EDITOR
	if (World == nullptr)
	{
		// We are in the editor at design time
		World = GEditor->GetEditorWorldContext().World();
	}
	SpawnParams.bHideFromSceneOutliner = true;
#endif

	if (!ensureAlwaysMsgf(World != nullptr, TEXT("Failed to call SpatialDebugger::CreateWorkerRegions. World was nullptr")))
	{
		return;
	}

	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (const FWorkerRegionInfo& WorkerRegionData : WorkerRegions)
	{
		AWorkerRegion* WorkerRegion = World->SpawnActor<AWorkerRegion>(SpawnParams);
		FString WorkerInfo = FString::Printf(TEXT("You are looking at virtual worker number %d\n%s"), WorkerRegionData.VirtualWorkerID,
											 *WorkerRegionData.WorkerName);
		WorkerRegion->Init(WorkerRegionMaterial, WorkerCombinedMaterial, WorkerInfoFont, WorkerRegionData.Color, WorkerRegionOpacity,
						   WorkerRegionData.Extents, WorkerRegionHeight, WorkerRegionVerticalScale, WorkerInfo);
		WorkerRegion->SetActorEnableCollision(false);
	}
}

void ASpatialDebugger::DestroyWorkerRegions()
{
	TArray<AActor*> WorkerRegionsToRemove;
	UGameplayStatics::GetAllActorsOfClass(this, AWorkerRegion::StaticClass(), WorkerRegionsToRemove);
	for (AActor* WorkerRegion : WorkerRegionsToRemove)
	{
		WorkerRegion->Destroy();
	}
}

void ASpatialDebugger::OnRep_SetWorkerRegions()
{
	if (NetDriver != nullptr && !NetDriver->IsServer() && DrawDebugDelegateHandle.IsValid() && bShowWorkerRegions)
	{
		DestroyWorkerRegions();
		CreateWorkerRegions();
	}
}

void ASpatialDebugger::Destroyed()
{
	if (DrawDebugDelegateHandle.IsValid())
	{
		UDebugDrawService::Unregister(DrawDebugDelegateHandle);
	}

	DestroyWorkerRegions();

	Super::Destroyed();
}

void ASpatialDebugger::LoadIcons()
{
	if (!ensureAlwaysMsgf(NetDriver != nullptr && !NetDriver->IsServer(),
						  TEXT("Failed to call SpatialDebugger::LoadIcons. NetDriver was nullptr OR running on server")))
	{
		return;
	}

	UTexture2D* DefaultTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));

	const float IconWidth = 16.0f;
	const float IconHeight = 16.0f;

	Icons[ICON_AUTH] = UCanvas::MakeIcon(AuthTexture != nullptr ? AuthTexture : DefaultTexture, 0.0f, 0.0f, IconWidth, IconHeight);
	Icons[ICON_AUTH_INTENT] =
		UCanvas::MakeIcon(AuthIntentTexture != nullptr ? AuthIntentTexture : DefaultTexture, 0.0f, 0.0f, IconWidth, IconHeight);
	Icons[ICON_UNLOCKED] =
		UCanvas::MakeIcon(UnlockedTexture != nullptr ? UnlockedTexture : DefaultTexture, 0.0f, 0.0f, IconWidth, IconHeight);
	Icons[ICON_LOCKED] = UCanvas::MakeIcon(LockedTexture != nullptr ? LockedTexture : DefaultTexture, 0.0f, 0.0f, IconWidth, IconHeight);
	Icons[ICON_BOX] = UCanvas::MakeIcon(BoxTexture != nullptr ? BoxTexture : DefaultTexture, 0.0f, 0.0f, IconWidth, IconHeight);
}

void ASpatialDebugger::OnToggleConfigUI()
{
	if (ConfigUIWidget == nullptr)
	{
		if (ConfigUIClass != nullptr)
		{
			ConfigUIWidget = CreateWidget<USpatialDebuggerConfigUI>(LocalPlayerController.Get(), ConfigUIClass);
			if (ConfigUIWidget == nullptr)
			{
				UE_LOG(LogSpatialDebugger, Error,
					   TEXT("SpatialDebugger config UI will not load. Couldn't create config UI widget for class: %s"),
					   *GetNameSafe(ConfigUIClass));
				return;
			}
			else
			{
				ConfigUIWidget->SetSpatialDebugger(this);
			}
		}
		else
		{
			UE_LOG(LogSpatialDebugger, Error,
				   TEXT("SpatialDebugger config UI will not load. ConfigUIClass is not set on the spatial debugger."));
			return;
		}

		ConfigUIWidget->AddToViewport();

		FInputModeGameAndUI InputModeSettings;
		InputModeSettings.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputModeSettings.SetWidgetToFocus(ConfigUIWidget->TakeWidget());

		LocalPlayerController->SetInputMode(InputModeSettings);
		LocalPlayerController->bShowMouseCursor = true;

		ConfigUIWidget->OnShow();
	}
	else
	{
		ConfigUIWidget->RemoveFromParent();
		ConfigUIWidget = nullptr;
		OnConfigUIClosed.ExecuteIfBound();
	}
}

void ASpatialDebugger::ToggleSelectActor()
{
	// This should only be toggled when the config UI window is open
	if (!ConfigUIWidget->IsVisible())
	{
		return;
	}

	bSelectActor = !bSelectActor;
	if (bSelectActor)
	{
		if (CrosshairTexture != nullptr)
		{
			// Hide the mouse cursor as we will draw our own custom crosshair
			LocalPlayerController->bShowMouseCursor = false;
			// Sets back the focus to the game viewport - need to hide mouse cursor instantly
			FSlateApplication::Get().SetAllUserFocusToGameViewport();
		}

		// Set the object types to query in the raycast based
		for (TEnumAsByte<ECollisionChannel> ActorTypeToQuery : SelectCollisionTypesToQuery)
		{
			CollisionObjectParams.AddObjectTypesToQuery(ActorTypeToQuery);
		}
	}
	else
	{
		// Change mouse cursor back to normal
		LocalPlayerController->bShowMouseCursor = true;

		RevertHoverMaterials();

		// Clear selected actors
		SelectedActors.Empty();
		HoverIndex = 0;
		HitActors.Empty();
	}
}

void ASpatialDebugger::OnSelectActor()
{
	if (HitActors.Num() > 0)
	{
		TWeakObjectPtr<AActor> SelectedActor = GetHitActor();

		if (SelectedActor.IsValid())
		{
			if (SelectedActors.Contains(SelectedActor))
			{
				// Already selected so deselect
				SelectedActors.Remove(SelectedActor);
			}
			else
			{
				// Add selected actor to enable drawing tags
				SelectedActors.Add(SelectedActor);
			}
		}
	}
}

void ASpatialDebugger::OnHighlightActor()
{
	HoverIndex++;
}

void ASpatialDebugger::DefaultOnConfigUIClosed()
{
	if (LocalPlayerController.IsValid())
	{
		FInputModeGameOnly InputModeSettings;
		LocalPlayerController->SetInputMode(InputModeSettings);
		LocalPlayerController->bShowMouseCursor = false;
	}
}

void ASpatialDebugger::SetShowWorkerRegions(const bool bNewShow)
{
	if (bNewShow != bShowWorkerRegions)
	{
		if (IsEnabled())
		{
			if (bNewShow)
			{
				CreateWorkerRegions();
			}
			else
			{
				DestroyWorkerRegions();
			}
		}

		bShowWorkerRegions = bNewShow;
	}
}

bool ASpatialDebugger::IsSelectActorEnabled() const
{
	return bSelectActor;
}

void ASpatialDebugger::DrawTag(UCanvas* Canvas, const FVector2D& ScreenLocation, const Worker_EntityId EntityId, const FString& ActorName,
							   const bool bCentre)
{
	SCOPE_CYCLE_COUNTER(STAT_DrawTag);

	if (!ensureAlwaysMsgf(NetDriver != nullptr && !NetDriver->IsServer(),
						  TEXT("Failed to call SpatialDebugger::DrawTag. NetDriver was nullptr OR running on server")))
	{
		return;
	}

	// TODO: UNR-5481 - Fix this hack for fixing spatial debugger crash after client travel
	if (!NetDriver->Connection->HasValidCoordinator())
	{
		return;
	}

	const TOptional<SpatialDebugging> DebuggingInfo = GetDebuggerSystem()->GetDebuggingData(EntityId);

	if (!DebuggingInfo.IsSet())
	{
		return;
	}

	if (!FApp::CanEverRender()) // DrawIcon can attempt to use the underlying texture resource even when using nullrhi
	{
		return;
	}

	static const float BaseHorizontalOffset = 16.0f;
	static const float NumberScale = 0.75f;
	static const float TextScale = 0.5f;
	const float AuthIdWidth = NumberScale * GetNumberOfDigitsIn(DebuggingInfo->AuthoritativeVirtualWorkerId);
	const float AuthIntentIdWidth = NumberScale * GetNumberOfDigitsIn(DebuggingInfo->IntentVirtualWorkerId);
	const float EntityIdWidth = NumberScale * GetNumberOfDigitsIn(EntityId);

	int32 HorizontalOffset = 0;
	if (bCentre)
	{
		// If tag should be centered, calculate the total width of the icons and text to be rendered
		float TagWidth = 0;
		if (bShowLock)
		{
			// If showing the lock, add the lock icon width
			TagWidth += BaseHorizontalOffset;
		}
		if (bShowAuth)
		{
			// If showing the authority, add the authority icon width and the width of the authoritative virtual worker ID
			TagWidth += BaseHorizontalOffset;
			TagWidth += (BaseHorizontalOffset * AuthIdWidth);
		}
		if (bShowAuthIntent)
		{
			// If showing the authority intent, add the authority intent icon width and the width of the authoritative intent virtual worker
			// ID
			TagWidth += BaseHorizontalOffset;
			TagWidth += (BaseHorizontalOffset * AuthIntentIdWidth);
		}
		if (bShowEntityId)
		{
			// If showing the entity ID, add the width of the entity ID
			TagWidth += (BaseHorizontalOffset * EntityIdWidth);
		}
		if (bShowActorName)
		{
			// If showing the actor name, add the width of the actor name
			const float ActorNameWidth = TextScale * ActorName.Len();
			TagWidth += (BaseHorizontalOffset * ActorNameWidth);
		}

		// Calculate the offset based on the total width of the tag
		HorizontalOffset = TagWidth / -2;
	}

	// Draw icons and text based on the offset
	if (bShowLock)
	{
		SCOPE_CYCLE_COUNTER(STAT_DrawIcons);
		const bool bIsLocked = DebuggingInfo->IsLocked;
		const EIcon LockIcon = bIsLocked ? ICON_LOCKED : ICON_UNLOCKED;

		Canvas->SetDrawColor(FColor::White);
		Canvas->DrawIcon(Icons[LockIcon], ScreenLocation.X + HorizontalOffset, ScreenLocation.Y, 1.0f);
		HorizontalOffset += BaseHorizontalOffset;
	}

	if (bShowAuth)
	{
		SCOPE_CYCLE_COUNTER(STAT_DrawIcons);
		const FColor& ServerWorkerColor = DebuggingInfo->AuthoritativeColor;
		Canvas->SetDrawColor(FColor::White);
		Canvas->DrawIcon(Icons[ICON_AUTH], ScreenLocation.X + HorizontalOffset, ScreenLocation.Y, 1.0f);
		HorizontalOffset += BaseHorizontalOffset;
		Canvas->SetDrawColor(ServerWorkerColor);
		Canvas->DrawScaledIcon(Icons[ICON_BOX], ScreenLocation.X + HorizontalOffset, ScreenLocation.Y, FVector(AuthIdWidth, 1.f, 1.f));
		Canvas->SetDrawColor(GetTextColorForBackgroundColor(ServerWorkerColor));
		Canvas->DrawText(RenderFont, FString::FromInt(DebuggingInfo->AuthoritativeVirtualWorkerId), ScreenLocation.X + HorizontalOffset + 1,
						 ScreenLocation.Y, 1.1f, 1.1f, FontRenderInfo);
		HorizontalOffset += (BaseHorizontalOffset * AuthIdWidth);
	}

	if (bShowAuthIntent)
	{
		SCOPE_CYCLE_COUNTER(STAT_DrawIcons);
		const FColor& VirtualWorkerColor = DebuggingInfo->IntentColor;
		Canvas->SetDrawColor(FColor::White);
		Canvas->DrawIcon(Icons[ICON_AUTH_INTENT], ScreenLocation.X + HorizontalOffset, ScreenLocation.Y, 1.0f);
		HorizontalOffset += BaseHorizontalOffset;
		Canvas->SetDrawColor(VirtualWorkerColor);
		Canvas->DrawScaledIcon(Icons[ICON_BOX], ScreenLocation.X + HorizontalOffset, ScreenLocation.Y,
							   FVector(AuthIntentIdWidth, 1.f, 1.f));
		Canvas->SetDrawColor(GetTextColorForBackgroundColor(VirtualWorkerColor));
		Canvas->DrawText(RenderFont, FString::FromInt(DebuggingInfo->IntentVirtualWorkerId), ScreenLocation.X + HorizontalOffset + 1,
						 ScreenLocation.Y, 1.1f, 1.1f, FontRenderInfo);
		HorizontalOffset += (BaseHorizontalOffset * AuthIntentIdWidth);
	}

	FString Label;
	if (bShowEntityId)
	{
		SCOPE_CYCLE_COUNTER(STAT_BuildText);
		Label += FString::Printf(TEXT("%lld "), EntityId);
	}

	if (bShowActorName)
	{
		SCOPE_CYCLE_COUNTER(STAT_BuildText);
		Label += FString::Printf(TEXT("(%s)"), *ActorName);
	}

	if (bShowEntityId || bShowActorName)
	{
		SCOPE_CYCLE_COUNTER(STAT_DrawText);
		Canvas->SetDrawColor(FColor::Green);
		Canvas->DrawText(RenderFont, Label, ScreenLocation.X + HorizontalOffset, ScreenLocation.Y, 1.0f, 1.0f, FontRenderInfo);
	}
}

FColor ASpatialDebugger::GetTextColorForBackgroundColor(const FColor& BackgroundColor) const
{
	return BackgroundColor.ReinterpretAsLinear().GetLuminance() > 0.5 ? FColor::Black : FColor::White;
}

// This will break once we have more than 10,000 workers, happily kicking that can down the road.
int32 ASpatialDebugger::GetNumberOfDigitsIn(int32 SomeNumber) const
{
	SomeNumber = FMath::Abs(SomeNumber);
	return (SomeNumber < 10 ? 1 : (SomeNumber < 100 ? 2 : (SomeNumber < 1000 ? 3 : 4)));
}

void ASpatialDebugger::DrawDebug(UCanvas* Canvas, APlayerController* /* Controller */) // Controller is invalid.
{
	SCOPE_CYCLE_COUNTER(STAT_DrawDebug);

	if (!ensureAlwaysMsgf(NetDriver != nullptr && !NetDriver->IsServer(),
						  TEXT("Failed to call SpatialDebugger::DrawDebug. NetDriver was nullptr OR running on server")))
	{
		return;
	}

#if WITH_EDITOR
	// Prevent one client's data rendering in another client's view in PIE when using UDebugDrawService.  Lifted from EQSRenderingComponent.
	if (Canvas && Canvas->SceneView && Canvas->SceneView->Family && Canvas->SceneView->Family->Scene
		&& Canvas->SceneView->Family->Scene->GetWorld() != GetWorld())
	{
		return;
	}
#endif

	if (bSelectActor)
	{
		SelectActorsToTag(Canvas);
		return;
	}

	if (ActorTagDrawMode >= EActorTagDrawMode::LocalPlayer)
	{
		DrawDebugLocalPlayer(Canvas);
	}

	if (ActorTagDrawMode == EActorTagDrawMode::All)
	{
		const FVector PlayerLocation = GetLocalPawnLocation();

		for (const TPair<Worker_EntityId_Key, TWeakObjectPtr<AActor>>& EntityActorPair : GetDebuggerSystem()->GetActors())
		{
			const TWeakObjectPtr<AActor> Actor = EntityActorPair.Value;
			const Worker_EntityId EntityId = EntityActorPair.Key;
			FVector2D ScreenLocation;
			if (Actor != nullptr && ProjectActorToScreen(Actor->GetActorLocation(), PlayerLocation, ScreenLocation, Canvas))
			{
				DrawTag(Canvas, ScreenLocation, EntityId, Actor->GetName(), true /*bCentre*/);
			}
		}
	}
}

void ASpatialDebugger::SelectActorsToTag(UCanvas* Canvas)
{
	if (LocalPlayerController.IsValid())
	{
		FVector2D NewMousePosition;

		if (LocalPlayerController->GetMousePosition(NewMousePosition.X, NewMousePosition.Y))
		{
			if (CrosshairTexture != nullptr)
			{
				// Display a crosshair icon for the mouse cursor
				// Offset by half of the texture's dimensions so that the center of the texture aligns with the center of the Canvas.
				FVector2D CrossHairDrawPosition(NewMousePosition.X - (CrosshairTexture->GetSurfaceWidth() * 0.5f),
												NewMousePosition.Y - (CrosshairTexture->GetSurfaceHeight() * 0.5f));

				// Draw the crosshair at the mouse position.
				FCanvasTileItem TileItem(CrossHairDrawPosition, CrosshairTexture->Resource, FLinearColor::White);
				TileItem.BlendMode = SE_BLEND_Translucent;
				Canvas->DrawItem(TileItem);
			}

			TWeakObjectPtr<AActor> NewHoverActor = GetActorAtPosition(NewMousePosition, Canvas);
			HighlightActorUnderCursor(NewHoverActor);
		}

		// Draw tags above selected actors
		for (TWeakObjectPtr<AActor> SelectedActor : SelectedActors)
		{
			if (SelectedActor.IsValid())
			{
				if (const Worker_EntityId_Key* HitEntityId = GetDebuggerSystem()->GetActorEntityId(SelectedActor.Get()))
				{
					FVector PlayerLocation = GetLocalPawnLocation();

					FVector2D ScreenLocation;
					if (ProjectActorToScreen(SelectedActor->GetActorLocation(), PlayerLocation, ScreenLocation, Canvas))
					{
						DrawTag(Canvas, ScreenLocation, *HitEntityId, SelectedActor->GetName(), true /*bCentre*/);
					}
				}
			}
		}
	}
}

void ASpatialDebugger::HighlightActorUnderCursor(TWeakObjectPtr<AActor>& NewHoverActor)
{
	// Check if highlighting feature is enabled and the glowing wire frame material is set
	if (!bShowHighlight || WireFrameMaterial == nullptr)
	{
		return;
	}

	if (!NewHoverActor.IsValid())
	{
		// No actor under the cursor so revert hover materials on previous actor
		RevertHoverMaterials();
	}
	else if (NewHoverActor != HoverActor)
	{
		// New actor under the cursor

		// Revert hover materials on previous actor
		RevertHoverMaterials();

		// Set hover materials on new actor
		TArray<UActorComponent*> ActorComponents;
		NewHoverActor->GetComponents(UMeshComponent::StaticClass(), ActorComponents, true);
		for (UActorComponent* NewActorComponent : ActorComponents)
		{
			// Store previous components
			TWeakObjectPtr<UMeshComponent> MeshComponent(Cast<UMeshComponent>(NewActorComponent));
			TWeakObjectPtr<UMaterialInterface> MeshMaterial = MeshComponent->GetMaterial(0);
			if (MeshComponent.IsValid() && MeshMaterial.IsValid())
			{
				ActorMeshComponents.Add(MeshComponent);
				// Store previous materials
				ActorMeshMaterials.Add(MeshMaterial);
				// Set wireframe material on new actor
				MeshComponent->SetMaterial(0, WireFrameMaterial);
			}
		}
		HoverActor = NewHoverActor;
	}
}

void ASpatialDebugger::RevertHoverMaterials()
{
	if (!bShowHighlight)
	{
		return;
	}

	if (HoverActor.IsValid())
	{
		// Revert materials on previous actor
		for (int i = 0; i < ActorMeshComponents.Num(); i++)
		{
			TWeakObjectPtr<UMeshComponent> ActorMeshComponent = ActorMeshComponents[i];
			TWeakObjectPtr<UMaterialInterface> ActorMeshMaterial = ActorMeshMaterials[i];
			if (ActorMeshComponent.IsValid() && ActorMeshMaterial.IsValid())
			{
				ActorMeshComponent->SetMaterial(0, ActorMeshMaterial.Get());
			}
		}

		// Clear previous materials
		ActorMeshMaterials.Empty();
		ActorMeshComponents.Empty();

		HoverActor = nullptr;
	}
}

TWeakObjectPtr<AActor> ASpatialDebugger::GetActorAtPosition(const FVector2D& NewMousePosition, const UCanvas* Canvas)
{
	if (!LocalPlayerController.IsValid())
	{
		return nullptr;
	}
	else if (NewMousePosition != MousePosition)
	{
		// Mouse has moved so raycast to find actors currently under the mouse cursor
		MousePosition = NewMousePosition;

		FVector WorldLocation;
		FVector WorldRotation;
		LocalPlayerController->DeprojectScreenPositionToWorld(NewMousePosition.X, NewMousePosition.Y, WorldLocation,
															  WorldRotation); // Mouse cursor position
		FVector StartTrace = WorldLocation;
		FVector EndTrace = StartTrace + WorldRotation * MaxRange;

		HitActors.Empty();

		TArray<FHitResult> HitResults;
		bool bHit = GetWorld()->LineTraceMultiByObjectType(HitResults, StartTrace, EndTrace, CollisionObjectParams);
		if (bHit)
		{
			// When the raycast hits an actor then it is highlighted, whilst the actor remains under the crosshair. If there are multiple
			// hit results, the user can select the next by using the mouse scroll wheel
			for (const FHitResult& HitResult : HitResults)
			{
				const TWeakObjectPtr<AActor> HitActor = HitResult.GetActor();

				if (!HitActor.IsValid() || HitActors.Contains(HitActor))
				{
					// The hit results may include the same actor multiple times so just ignore duplicates
					continue;
				}

				// Only add actors to the list of hit actors if they have a valid entity id and screen position. As later when we scroll
				// through the actors, we only want to highlight ones that we can show a tag for.
				if (const Worker_EntityId_Key* HitEntityId = GetDebuggerSystem()->GetActorEntityId(HitResult.GetActor()))
				{
					FVector PlayerLocation = GetLocalPawnLocation();

					FVector2D ScreenLocation;
					if (CanProjectActorLocationToScreen(HitActor->GetActorLocation(), PlayerLocation, Canvas))
					{
						HitActors.Add(HitActor);
					}
				}
			}
		}
	}

	return GetHitActor();
}

// Return actor selected from list dependent on the hover index, which is selected independently with the mouse wheel (by default)
TWeakObjectPtr<AActor> ASpatialDebugger::GetHitActor()
{
	if (HitActors.Num() == 0)
	{
		return nullptr;
	}

	// Validate hover index
	if (HoverIndex >= HitActors.Num())
	{
		// Reset hover index
		HoverIndex = 0;
	}

	return HitActors[HoverIndex];
}

bool ASpatialDebugger::CanProjectActorLocationToScreen(const FVector& ActorLocation, const FVector& PlayerLocation, const UCanvas* Canvas)
{
	return !(ActorLocation.IsZero() || FVector::Dist(PlayerLocation, ActorLocation) > MaxRange);
}

bool ASpatialDebugger::ProjectActorToScreen(const FVector& ActorLocation, const FVector& PlayerLocation, FVector2D& OutLocation,
											const UCanvas* Canvas)
{
	if (CanProjectActorLocationToScreen(ActorLocation, PlayerLocation, Canvas))
	{
		SCOPE_CYCLE_COUNTER(STAT_Projection);
		OutLocation = FVector2D(Canvas->Project(ActorLocation + WorldSpaceActorTagOffset));
		return true;
	}
	return false;
}
FVector ASpatialDebugger::GetLocalPawnLocation()
{
	FVector PlayerLocation = FVector::ZeroVector;
	if (LocalPawn.IsValid())
	{
		PlayerLocation = LocalPawn->GetActorLocation();
	}
	return PlayerLocation;
}

void GetReplicatedActorsInHierarchy(const AActor* Actor, TArray<const AActor*>& HierarchyActors)
{
	if (Actor->GetIsReplicated() && !HierarchyActors.Contains(Actor))
	{
		HierarchyActors.Add(Actor);
	}
	for (const AActor* Child : Actor->Children)
	{
		GetReplicatedActorsInHierarchy(Child, HierarchyActors);
	}
}

void ASpatialDebugger::DrawDebugLocalPlayer(UCanvas* Canvas)
{
	if (LocalPawn == nullptr || LocalPlayerController == nullptr || LocalPlayerState == nullptr)
	{
		return;
	}

	TArray<const AActor*> ActorsToDisplay = { LocalPlayerState.Get(), LocalPlayerController.Get(), LocalPawn.Get() };

	if (bShowPlayerHierarchy)
	{
		GetReplicatedActorsInHierarchy(LocalPlayerController.Get(), ActorsToDisplay);
	}

	FVector2D ScreenLocation(PlayerPanelStartX, PlayerPanelStartY);

	for (int32 i = 0; i < ActorsToDisplay.Num(); ++i)
	{
		const Worker_EntityId EntityId = NetDriver->PackageMap->GetEntityIdFromObject(ActorsToDisplay[i]);
		DrawTag(Canvas, ScreenLocation, EntityId, ActorsToDisplay[i]->GetName(), false /*bCentre*/);
		ScreenLocation.Y += PLAYER_TAG_VERTICAL_OFFSET;
	}
}

void ASpatialDebugger::SpatialToggleDebugger()
{
	if (!ensureAlwaysMsgf(NetDriver != nullptr && !NetDriver->IsServer(),
						  TEXT("Failed to call SpatialDebugger::SpatialToggleDebugger. NetDriver was nullptr OR running on server")))
	{
		return;
	}

	if (DrawDebugDelegateHandle.IsValid())
	{
		UDebugDrawService::Unregister(DrawDebugDelegateHandle);
		DrawDebugDelegateHandle.Reset();
		DestroyWorkerRegions();
	}
	else
	{
		DrawDebugDelegateHandle =
			UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateUObject(this, &ASpatialDebugger::DrawDebug));
		if (bShowWorkerRegions)
		{
			CreateWorkerRegions();
		}
	}
}

bool ASpatialDebugger::IsEnabled()
{
	return DrawDebugDelegateHandle.IsValid();
}

#if WITH_EDITOR
void ASpatialDebugger::EditorRefreshDisplay()
{
	if (GEditor != nullptr && GEditor->GetActiveViewport() != nullptr)
	{
		// Redraw editor window to show changes
		GEditor->GetActiveViewport()->Invalidate();
	}
}

void ASpatialDebugger::EditorSpatialToggleDebugger(bool bEnabled)
{
	bShowWorkerRegions = bEnabled;
	EditorRefreshWorkerRegions();
}

void ASpatialDebugger::EditorRefreshWorkerRegions()
{
	DestroyWorkerRegions();

	if (bShowWorkerRegions && EditorAllowWorkerBoundaries())
	{
		EditorInitialiseWorkerRegions();
		CreateWorkerRegions();
	}

	EditorRefreshDisplay();
}

bool ASpatialDebugger::EditorAllowWorkerBoundaries() const
{
	// Check if spatial networking is enabled.
	return GetDefault<UGeneralProjectSettings>()->UsesSpatialNetworking();
}

void ASpatialDebugger::EditorInitialiseWorkerRegions()
{
	WorkerRegions.Empty();

	const UWorld* World = GEditor->GetEditorWorldContext().World();

	if (!ensureAlwaysMsgf(World != nullptr, TEXT("Failed to EditorInitialiseWorkerRegions. Couldn't access World from GEditor")))
	{
		return;
	}

	const UAbstractSpatialMultiWorkerSettings* MultiWorkerSettings =
		USpatialStatics::GetSpatialMultiWorkerClass(World)->GetDefaultObject<UAbstractSpatialMultiWorkerSettings>();

	ULayeredLBStrategy* LoadBalanceStrategy = NewObject<ULayeredLBStrategy>();
	LoadBalanceStrategy->Init();
	LoadBalanceStrategy->SetLayers(MultiWorkerSettings->WorkerLayers);

	if (const UGridBasedLBStrategy* GridBasedLBStrategy =
			Cast<UGridBasedLBStrategy>(LoadBalanceStrategy->GetLBStrategyForVisualRendering()))
	{
		LoadBalanceStrategy->SetVirtualWorkerIds(1, LoadBalanceStrategy->GetMinimumRequiredWorkers());
		const UGridBasedLBStrategy::LBStrategyRegions LBStrategyRegions = GridBasedLBStrategy->GetLBStrategyRegions();

		WorkerRegions.SetNum(LBStrategyRegions.Num());
		for (int i = 0; i < LBStrategyRegions.Num(); i++)
		{
			const TPair<VirtualWorkerId, FBox2D>& LBStrategyRegion = LBStrategyRegions[i];
			FWorkerRegionInfo WorkerRegionInfo;
			// Generate our own unique worker name as we only need it to generate a unique colour
			const PhysicalWorkerName WorkerName = PhysicalWorkerName::Printf(TEXT("WorkerRegion%d%d%d"), i, i, i);
			WorkerRegionInfo.Color = GetColorForWorkerName(WorkerName);
			WorkerRegionInfo.Extents = LBStrategyRegion.Value;
			WorkerRegions[i] = WorkerRegionInfo;
		}
	}

	// Needed to clean up LoadBalanceStrategy memory, otherwise it gets duplicated exponentially
	GEngine->ForceGarbageCollection(true);
}

void ASpatialDebugger::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property != nullptr)
	{
		const FName PropertyName(PropertyChangedEvent.Property->GetFName());
		if (PropertyName == GET_MEMBER_NAME_CHECKED(ASpatialDebugger, WorkerRegionHeight)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(ASpatialDebugger, WorkerRegionVerticalScale)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(ASpatialDebugger, WorkerRegionOpacity))

		{
			EditorRefreshWorkerRegions();
		}
	}
}
#endif // WITH_EDITOR

SpatialDebuggerSystem* ASpatialDebugger::GetDebuggerSystem() const
{
	if (!ensureAlwaysMsgf(NetDriver->SpatialDebuggerSystem.IsValid(), TEXT("Failed to access invalid debugger system")))
	{
		return nullptr;
	}
	return NetDriver->SpatialDebuggerSystem.Get();
}
