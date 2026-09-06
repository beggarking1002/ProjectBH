// Copyright ProjectBH. All Rights Reserved.
#include "BHCameraOcclusionComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
const FName AmountParameter(TEXT("BH_OcclusionAmount"));
const FName CameraParameter(TEXT("BH_OcclusionCamera"));
const FName FocusParameter(TEXT("BH_OcclusionFocus"));
const FName RadiusParameter(TEXT("BH_OcclusionRadius"));
const FName OrthoParameter(TEXT("BH_OcclusionOrtho"));
const FName ForwardParameter(TEXT("BH_OcclusionForward"));
}

UBHCameraOcclusionComponent::UBHCameraOcclusionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	bAutoActivate = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	SilhouetteMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(
		TEXT("/Game/Camera/Occlusion/M_BH_PlayerSilhouette.M_BH_PlayerSilhouette")));
}

void UBHCameraOcclusionComponent::BeginLocalView(APlayerController* Controller)
{
	LocalController = Controller;
	Camera = GetOwner()->FindComponentByClass<UCameraComponent>();
	SpringArm = GetOwner()->FindComponentByClass<USpringArmComponent>();
	if (SpringArm.IsValid() && bKeepCameraDistance)
	{
		bSavedCollisionTest = SpringArm->bDoCollisionTest;
		SpringArm->bDoCollisionTest = false;
		bOwnsSpringArm = true;
	}
	// Material overrides are process-local, but shared by split-screen views.
	// Split-screen uses PlayerController hidden primitives instead.
	const UGameInstance* Instance = GetWorld()->GetGameInstance();
	bUsePerViewHiding = Instance && Instance->GetLocalPlayers().Num() > 1;
	ProbeElapsed = FMath::Max(0.02f, ProbeInterval);
	const IConsoleVariable* CustomDepth = IConsoleManager::Get().FindConsoleVariable(TEXT("r.CustomDepth"));
	if (!bUsePerViewHiding && bEnablePlayerSilhouette && Camera.IsValid()
		&& CustomDepth && CustomDepth->GetInt() == 3)
	{
		ACharacter* Character = Cast<ACharacter>(GetOwner());
		UMaterialInterface* Material = SilhouetteMaterial.LoadSynchronous();
		if (Character && Character->GetMesh() && Material && Material->GetMaterial()
			&& Material->GetMaterial()->MaterialDomain == MD_PostProcess)
		{
			SilhouetteMesh = Character->GetMesh();
			bSavedCustomDepth = SilhouetteMesh->bRenderCustomDepth;
			SavedStencil = SilhouetteMesh->CustomDepthStencilValue;
			SavedStencilMask = static_cast<uint8>(SilhouetteMesh->CustomDepthStencilWriteMask);
			SilhouetteMesh->SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_Default);
			SilhouetteMesh->SetCustomDepthStencilValue(FMath::Clamp(SilhouetteStencil, 1, 255));
			SilhouetteMesh->SetRenderCustomDepth(true);
			SilhouetteInstance = UMaterialInstanceDynamic::Create(Material, this);
			SilhouetteInstance->SetScalarParameterValue(TEXT("BH_PlayerStencil"), SilhouetteStencil);
			Camera->AddOrUpdateBlendable(SilhouetteInstance, 1.0f);
		}
	}
}

void UBHCameraOcclusionComponent::TrackMesh(UMeshComponent* Mesh)
{
	if (!Mesh || !Mesh->IsVisible() || !Mesh->GetOwner() || Mesh->GetOwner()->IsHidden()) return;
	for (FBHCameraOccluderState& State : Occluders)
	{
		if (State.Mesh == Mesh)
		{
			State.LastSeenTime = GetWorld()->GetTimeSeconds();
			return;
		}
	}
	bool bPrepared = !bUsePerViewHiding && Mesh->GetNumMaterials() > 0;
	for (int32 Index = 0; bPrepared && Index < Mesh->GetNumMaterials(); ++Index)
	{
		UMaterialInterface* Material = Mesh->GetMaterial(Index);
		TArray<FMaterialParameterInfo> Infos;
		TArray<FGuid> Ids;
		if (Material) Material->GetAllScalarParameterInfo(Infos, Ids);
		bPrepared = Material && Material->GetBlendMode() == BLEND_Masked
			&& Infos.ContainsByPredicate([](const FMaterialParameterInfo& Info) { return Info.Name == AmountParameter; });
	}
	if (!bPrepared && !bHideUnpreparedOccluders && !bUsePerViewHiding) return;
	FBHCameraOccluderState& State = Occluders.AddDefaulted_GetRef();
	State.Mesh = Mesh;
	State.LastSeenTime = GetWorld()->GetTimeSeconds();
	if (!bPrepared)
	{
		if (!LocalController->HiddenPrimitiveComponents.Contains(Mesh))
		{
			LocalController->HiddenPrimitiveComponents.Add(Mesh);
			State.bHiddenByThisComponent = true;
		}
		return;
	}
	for (int32 Index = 0; Index < Mesh->GetNumMaterials(); ++Index)
	{
		UMaterialInterface* Original = Mesh->GetMaterial(Index);
		State.Originals.Add(Original);
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Original, this);
		MID->SetScalarParameterValue(AmountParameter, 0.0f);
		State.Overrides.Add(MID);
		Mesh->SetMaterial(Index, MID);
	}
}

void UBHCameraOcclusionComponent::Probe(const FVector& CameraLocation, const FVector& Focus)
{
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BHCameraOcclusion), false, GetOwner());
	TArray<FHitResult> Hits;
	GetWorld()->SweepMultiByObjectType(Hits, CameraLocation, Focus, FQuat::Identity,
		Objects, FCollisionShape::MakeSphere(FMath::Max(1.0f, ProbeRadius)), Params);
	// Object queries gather stacked occluders, not just the first blocking wall.
	for (const FHitResult& Hit : Hits)
	{
		UMeshComponent* Mesh = Cast<UMeshComponent>(Hit.GetComponent());
		AActor* Actor = Hit.GetActor();
		if (!Mesh || !Actor || Actor->IsA<APawn>() || OccluderTag.IsNone()) continue;
		if (Actor->ActorHasTag(OccluderTag) || Mesh->ComponentHasTag(OccluderTag)) TrackMesh(Mesh);
	}
}

void UBHCameraOcclusionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	APawn* Pawn = Cast<APawn>(GetOwner());
	APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!bEnabled || !IsActive() || !Controller || !Controller->IsLocalController()
		|| GetNetMode() == NM_DedicatedServer || Controller->GetViewTarget() != Pawn)
	{
		RestoreLocalView();
		return;
	}
	if (LocalController.Get() != Controller)
	{
		RestoreLocalView();
		BeginLocalView(Controller);
	}
	if (!Camera.IsValid()) return;
	const FVector Focus = Pawn->GetActorLocation() + FocusOffset;
	const FVector CameraLocation = Controller->PlayerCameraManager
		? Controller->PlayerCameraManager->GetCameraLocation() : Camera->GetComponentLocation();
	const FVector ViewForward = Controller->PlayerCameraManager
		? Controller->PlayerCameraManager->GetCameraRotation().Vector() : Camera->GetForwardVector();
	ProbeElapsed += DeltaTime;
	if (ProbeElapsed >= FMath::Max(0.02f, ProbeInterval))
	{
		ProbeElapsed = 0.0f;
		Probe(CameraLocation, Focus);
	}
	const float Now = GetWorld()->GetTimeSeconds();
	for (int32 Index = Occluders.Num() - 1; Index >= 0; --Index)
	{
		FBHCameraOccluderState& State = Occluders[Index];
		const bool bOccluded = Now - State.LastSeenTime <= FMath::Max(0.02f, ProbeInterval) + FMath::Max(0.0f, RestoreDelay);
		State.Amount = FMath::FInterpConstantTo(State.Amount, bOccluded ? 1.0f : 0.0f,
			DeltaTime, 1.0f / FMath::Max(0.01f, FadeDuration));
		if (!State.Mesh.IsValid() || (!bOccluded && State.Amount <= UE_KINDA_SMALL_NUMBER))
		{
			RestoreMesh(State);
			Occluders.RemoveAtSwap(Index);
			continue;
		}
		for (UMaterialInstanceDynamic* MID : State.Overrides)
		{
			MID->SetScalarParameterValue(AmountParameter, State.Amount);
			MID->SetVectorParameterValue(CameraParameter, FLinearColor(CameraLocation.X, CameraLocation.Y, CameraLocation.Z));
			MID->SetVectorParameterValue(FocusParameter, FLinearColor(Focus.X, Focus.Y, Focus.Z));
			MID->SetVectorParameterValue(ForwardParameter, FLinearColor(ViewForward.X, ViewForward.Y, ViewForward.Z));
			MID->SetScalarParameterValue(RadiusParameter, FMath::Max(1.0f, CutoutRadius));
			MID->SetScalarParameterValue(OrthoParameter, Camera->ProjectionMode == ECameraProjectionMode::Orthographic ? 1.0f : 0.0f);
		}
	}
}

void UBHCameraOcclusionComponent::RestoreMesh(FBHCameraOccluderState& State)
{
	UMeshComponent* Mesh = State.Mesh.Get();
	if (!Mesh) return;
	for (int32 Index = 0; Index < State.Overrides.Num(); ++Index)
	{
		// Do not overwrite a newer material change made by another gameplay system.
		if (Mesh->GetMaterial(Index) == State.Overrides[Index]) Mesh->SetMaterial(Index, State.Originals[Index]);
	}
	if (State.bHiddenByThisComponent && LocalController.IsValid())
		LocalController->HiddenPrimitiveComponents.Remove(Mesh);
}

void UBHCameraOcclusionComponent::RestoreLocalView()
{
	for (FBHCameraOccluderState& State : Occluders) RestoreMesh(State);
	Occluders.Reset();
	if (bOwnsSpringArm && SpringArm.IsValid()) SpringArm->bDoCollisionTest = bSavedCollisionTest;
	if (SilhouetteInstance && Camera.IsValid()) Camera->RemoveBlendable(SilhouetteInstance);
	if (SilhouetteMesh.IsValid())
	{
		SilhouetteMesh->SetRenderCustomDepth(bSavedCustomDepth);
		SilhouetteMesh->SetCustomDepthStencilValue(SavedStencil);
		SilhouetteMesh->SetCustomDepthStencilWriteMask(static_cast<ERendererStencilMask>(SavedStencilMask));
	}
	SilhouetteInstance = nullptr;
	SilhouetteMesh.Reset();
	Camera.Reset();
	SpringArm.Reset();
	LocalController.Reset();
	bOwnsSpringArm = false;
}

void UBHCameraOcclusionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestoreLocalView();
	Super::EndPlay(EndPlayReason);
}

void UBHCameraOcclusionComponent::Deactivate()
{
	RestoreLocalView();
	Super::Deactivate();
}
