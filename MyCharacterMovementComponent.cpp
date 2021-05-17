// Fill out your copyright notice in the Description page of Project Settings.

#include "MyCharacterMovementComponent.h"
#include "MyProjectCharacter.h"
#include "DrawDebugHelpers.h"

namespace MyCharacterMovementCVars
{
	int32 ShowMotionDebug = 0;
	FAutoConsoleVariableRef CVarShowMotionDebug(
		TEXT("p.ShowMotionDebug"),
		ShowMotionDebug,
		TEXT("Whether to draw motion debug.\n")
		TEXT("0: Disable, 1: Enable"),
		ECVF_Cheat);
}

FCharacterMotionData::FCharacterMotionData(const FVector& InStartLocation, const FVector& InTargetLocation, const FRotator& InStartRotation, const FRotator& InTargetRotation, uint8 InDuration)
{
	StartLocation = InStartLocation;
	TargetLocation = InTargetLocation;
	StartRotation = InStartRotation;
	TargetRotation = InTargetRotation;
	Duration = InDuration;
}

void FCharacterNetworkMoveData_Custom::ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType)
{
	Super::ClientFillNetworkMoveData(ClientMove, MoveType);

	const FSavedMove_Character_Custom& ClientMoveCustom = static_cast<const FSavedMove_Character_Custom&>(ClientMove);

	bMotionDataValid = ClientMoveCustom.SavedMotionData.bHasValidData;
}

bool FCharacterNetworkMoveData_Custom::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType)
{
	bool bReturn = Super::Serialize(CharacterMovement, Ar, PackageMap, MoveType);
	if (bReturn)
	{
		static FCharacterMotionData EmptyMotionData;
		UMyCharacterMovementComponent* MyMoveComp = Cast<UMyCharacterMovementComponent>(&CharacterMovement);
		const bool bIsSaving = Ar.IsSaving();
	
		FCharacterMotionData* SerializingMotionData = nullptr;
		if (bIsSaving)
		{
			SerializingMotionData = bMotionDataValid && !MyMoveComp->GetCurrentMotionData().IsAcked() ? &MyMoveComp->GetCurrentMotionData() : &EmptyMotionData;
		}
		else
		{
			SerializingMotionData = &NetMotionData;
		}

		NetSerializeOptionalValue<FCharacterMotionData>(bIsSaving, Ar, *SerializingMotionData, EmptyMotionData, PackageMap);

		bReturn &= !Ar.IsError();
	}
	return bReturn;
}

void FCharacterMoveResponseDataContainer_Custom::ServerFillResponseData(const UCharacterMovementComponent& CharacterMovement, const FClientAdjustment& PendingAdjustment)
{
	Super::ServerFillResponseData(CharacterMovement, PendingAdjustment);

	const UMyCharacterMovementComponent* MyMoveComp = Cast<const UMyCharacterMovementComponent>(&CharacterMovement);
	//ServerTotalTime = MyMoveComp->MotionData.TotalTime;
}

bool FCharacterMoveResponseDataContainer_Custom::Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap)
{
	bool bReturn = Super::Serialize(CharacterMovement, Ar, PackageMap);
	if (bReturn && IsCorrection())
	{
		// Add here custom values to send to the client
		//Ar << ServerTotalTime;
		bReturn &= !Ar.IsError();
	}
	return bReturn;
}

UMyCharacterMovementComponent::UMyCharacterMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetNetworkMoveDataContainer(CustomNetworkMoveDataContainer);
	SetMoveResponseDataContainer(CustomMoveResponseContainer);

	const auto CVarNetPackedMovementMaxBits = IConsoleManager::Get().FindConsoleVariable(TEXT("p.NetPackedMovementMaxBits"));
	CVarNetPackedMovementMaxBits->Set(int32(CVarNetPackedMovementMaxBits->GetInt() + sizeof(FCharacterMotionData) * 8));
}

void UMyCharacterMovementComponent::ServerMove_PerformMovement(const FCharacterNetworkMoveData& MoveData)
{
	const FCharacterNetworkMoveData_Custom* NetMoveData = static_cast<const FCharacterNetworkMoveData_Custom*>(&MoveData);
	if (!MotionData.IsActive() && !MotionData.HasValidData() && NetMoveData->NetMotionData.HasValidData())
	{
		// Start Motion on the server the first time we receive a motion data struct.
		// Client will stop sending the data once acked.
		StartMotion(NetMoveData->NetMotionData);
	}

	Super::ServerMove_PerformMovement(MoveData);
}

void UMyCharacterMovementComponent::ClientAckGoodMove_Implementation(float TimeStamp)
{
	Super::ClientAckGoodMove_Implementation(TimeStamp);

	FNetworkPredictionData_Client_Character_Custom* ClientData = static_cast<FNetworkPredictionData_Client_Character_Custom*>(GetPredictionData_Client_Character());
	check(ClientData);

	const FSavedMove_Character_Custom* LastAckedClientMoveCustom = static_cast<const FSavedMove_Character_Custom*>(ClientData->LastAckedMove.Get());
	if (LastAckedClientMoveCustom && LastAckedClientMoveCustom->SavedMotionData.bIsActive)
	{
		if (MotionData.IsActive() && !MotionData.IsAcked() && MotionData.HasValidData())
		{
			MotionData.Ack();
			UE_LOG(LogTemp, Log, TEXT("ClientAckGoodMove_Implementation - acking motion data"));
		}
	}
}

void UMyCharacterMovementComponent::OnClientCorrectionReceived(FNetworkPredictionData_Client_Character& ClientData, float TimeStamp, FVector NewLocation, FVector NewVelocity, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode)
{
	FCharacterMoveResponseDataContainer_Custom& MoveResponseDataCustom = static_cast<FCharacterMoveResponseDataContainer_Custom&>(GetMoveResponseDataContainer());
	// Use MoveResponseDataCustom to read data sent from the server

	if (MotionData.IsActive() && MotionData.HasValidData())
	{
		MotionData.Stop();

		const FSavedMove_Character_Custom* LastAckedClientMoveCustom = static_cast<const FSavedMove_Character_Custom*>(ClientData.LastAckedMove.Get());
		if (LastAckedClientMoveCustom && LastAckedClientMoveCustom->SavedMotionData.bHasValidData)
		{
			if (!MotionData.IsAcked())
			{
				MotionData.Ack();
				UE_LOG(LogTemp, Log, TEXT("OnClientCorrectionReceived - acking motion data"));
			}
		}
	}

	Super::OnClientCorrectionReceived(ClientData, TimeStamp, NewLocation, NewVelocity, NewBase, NewBaseBoneName, bHasBase, bBaseRelativePosition, ServerMovementMode);
}

class FNetworkPredictionData_Client* UMyCharacterMovementComponent::GetPredictionData_Client() const
{
	if (ClientPredictionData == nullptr)
	{
		UMyCharacterMovementComponent* MutableThis = const_cast<UMyCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_Character_Custom(*this);
	}

	return ClientPredictionData;
}

void UMyCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
}

void UMyCharacterMovementComponent::PhysCustom(float deltaTime, int32 Iterations)
{
	if (MotionData.IsActive())
	{
		PhysCustomMotion(deltaTime);
	}

	Super::PhysCustom(deltaTime, Iterations);
}

void UMyCharacterMovementComponent::StartMotion(const FCharacterMotionData& NewMotionData)
{
	if (MotionData.IsActive() || MotionData.HasValidData())
	{
		// custom motion already in progress
		return;
	}

	MotionData = NewMotionData;

	ResumeMotion(0.0f);

	if (MyCharacterMovementCVars::ShowMotionDebug != 0)
	{
		DrawDebugCapsule(GetWorld(), MotionData.StartLocation, CharacterOwner->GetSimpleCollisionHalfHeight(), CharacterOwner->GetSimpleCollisionRadius(), FQuat::Identity, FColor::Red, false, 15.0f);
		DrawDebugCapsule(GetWorld(), MotionData.TargetLocation, CharacterOwner->GetSimpleCollisionHalfHeight(), CharacterOwner->GetSimpleCollisionRadius(), FQuat::Identity, FColor::Red, false, 15.0f);
	}
}

void UMyCharacterMovementComponent::PhysCustomMotion(float DeltaTime)
{
	MotionData.TotalTime += DeltaTime;

	float MoveValue = FMath::Min(1.0f, MotionData.TotalTime / MotionData.Duration);
	float LerpValue = MoveValue;
	bool bEnded = false;

	if (FMath::IsNearlyEqual(MoveValue, 1.0f))
	{
		bEnded = true;
		LerpValue = 1.0f;
	}

	if (!bEnded && MotionData.MovementSpeedCurve)
	{
		LerpValue = MotionData.MovementSpeedCurve->GetFloatValue(LerpValue);
	}

	FVector_NetQuantize NewLocation = FMath::Lerp(MotionData.StartLocation, MotionData.TargetLocation, LerpValue);

	if (!bEnded && MotionData.MovementZMultiplierCurve)
	{
		NewLocation.Z += MotionData.MaxZOffset * MotionData.MovementZMultiplierCurve->GetFloatValue(LerpValue);
	}
	
	FRotator NewRotation = FMath::Lerp(MotionData.StartRotation, MotionData.TargetRotation, LerpValue);

	FHitResult Hit;
	SafeMoveUpdatedComponent(NewLocation - GetActorLocation(), NewRotation.Quaternion(), MotionData.bSweepDuringMotion, Hit, ETeleportType::TeleportPhysics);

	if (bEnded)
	{
		SetMovementMode(MotionData.MovementModeOnEnd);
		MotionData.Clear();
	}

	if (MyCharacterMovementCVars::ShowMotionDebug != 0)
	{
		FColor Color = bEnded ? FColor::Yellow : FColor::Blue;
		DrawDebugCapsule(GetWorld(), UpdatedComponent->GetComponentLocation(), CharacterOwner->GetSimpleCollisionHalfHeight(), CharacterOwner->GetSimpleCollisionRadius(), FQuat::Identity, Color, false, 5.0f);
	}
}

void UMyCharacterMovementComponent::ResumeMotion(float CurrentTotalTime)
{
	ensure(MotionData.HasValidData());

	StopMovementImmediately();
	SetMovementMode(EMovementMode::MOVE_Custom, 0);

	MotionData.Resume(CurrentTotalTime);
}

void FSavedMove_Character_Custom::Clear()
{
	Super::Clear();
}

uint8 FSavedMove_Character_Custom::GetCompressedFlags() const
{
	return Super::GetCompressedFlags();

	/*
	 * These are new abilities that we're stuffing into Result. There are 4 pre-defined custom flags:
	 * FLAG_JumpPressed = 0x01,	  Jump pressed
	 * FLAG_WantsToCrouch = 0x02, Wants to crouch
	 * FLAG_Reserved_1 = 0x04,	  Reserved for future use
	 * FLAG_Reserved_2 = 0x08,	  Reserved for future use
	 * FLAG_Custom_0 = 0x10,
	 * FLAG_Custom_1 = 0x20,
	 * FLAG_Custom_2 = 0x40,
	 * FLAG_Custom_3 = 0x80,
	 *
	 * You might notice that this is a single byte (0x00 - 0x80). It might be easier to see this as the
	 * 8 bits that this byte represents.
	 *  _ _ _ _  _ _ _ _
	 *  7 6 5 4  3 2 1 0
	 *  | | | |  | | | |- FLAG_JumpPressed
	 *  | | | |  | | |--- FLAG_WantsToCrouch
	 *  | | | |  | |----- FLAG_Reserved_1
	 *  | | | |  |------- FLAG_Reserved_2
	 *  | | | |---------- FLAG_Custom_0
	 *  | | |------------ FLAG_Custom_1
	 *  | |-------------- FLAG_Custom_2
	 *  |---------------- FLAG_Custom_3
	 *
	 * In UT, they combine the reserved flags with Custom_0 to get 3 bits of space. Combined that gives them
	 * 8 unique states they can check (2^3) vs 3. The caveat is a bit of shifting and OR'ing on this end and
	 * shifting and AND'ing on the UpdateFromCompressedFlags end to pull out the bits of interest.
	 */
}

bool FSavedMove_Character_Custom::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const
{
	const FSavedMove_Character_Custom* NewMoveCustom = ((const FSavedMove_Character_Custom*)&NewMove);

	if (SavedMotionData.bIsActive != NewMoveCustom->SavedMotionData.bIsActive ||
		SavedMotionData.bHasValidData != NewMoveCustom->SavedMotionData.bHasValidData)
	{
		return false;
	}

	return Super::CanCombineWith(NewMove, Character, MaxDelta);
}

void FSavedMove_Character_Custom::SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel, class FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);

	/* Saved Move <--- Character Movement Data */
	
	UMyCharacterMovementComponent* MyCharMoveComp = CastChecked<UMyCharacterMovementComponent>(Character->GetCharacterMovement());

	SavedMotionData.TotalTime = MyCharMoveComp->GetCurrentMotionData().GetTotalTime();
	SavedMotionData.bIsActive = MyCharMoveComp->GetCurrentMotionData().IsActive();
	SavedMotionData.bHasValidData = MyCharMoveComp->GetCurrentMotionData().HasValidData();
}

void FSavedMove_Character_Custom::PostUpdate(ACharacter* Character, EPostUpdateMode PostUpdateMode)
{
	Super::PostUpdate(Character, PostUpdateMode);
}

void FSavedMove_Character_Custom::PrepMoveFor(class ACharacter* Character)
{
	Super::PrepMoveFor(Character);

	/* Saved Move ---> Character Movement Data */

	UMyCharacterMovementComponent* MyCharMoveComp = CastChecked<UMyCharacterMovementComponent>(Character->GetCharacterMovement());
	if (MyCharMoveComp->GetCurrentMotionData().HasValidData() && !MyCharMoveComp->GetCurrentMotionData().IsActive() && SavedMotionData.bIsActive)
	{
		// When a correction is received from the server, the client state is rollbacked to what the server said (including movement mode), and the all the saved moves are replayed.
		// This move is the first one containing motion data after a correction, so resume motion from its accumulated total time.
		MyCharMoveComp->ResumeMotion(SavedMotionData.TotalTime);

		UE_LOG(LogTemp, Log, TEXT("--------- PrepMoveFor - move Timestamp %f - motion total time - : %f"), TimeStamp, SavedMotionData.TotalTime);
	}
}

FSavedMovePtr FNetworkPredictionData_Client_Character_Custom::AllocateNewMove()
{
	return FSavedMovePtr(new FSavedMove_Character_Custom());
}