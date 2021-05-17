// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MyCharacterMovementComponent.generated.h"

USTRUCT()
struct MYPROJECT_API FCharacterMotionData
{
	friend class UMyCharacterMovementComponent;

	GENERATED_BODY()

	UPROPERTY()
	FVector_NetQuantize StartLocation;

	UPROPERTY()
	FVector_NetQuantize TargetLocation;

	UPROPERTY()
	FRotator StartRotation;

	UPROPERTY()
	FRotator TargetRotation;

	UPROPERTY()
	UCurveFloat* MovementSpeedCurve = nullptr;

	UPROPERTY()
	UCurveFloat* MovementZMultiplierCurve = nullptr;

	UPROPERTY()
	uint8 MaxZOffset = 0;

	UPROPERTY()
	uint8 Duration = 0;

	UPROPERTY()
	bool bSweepDuringMotion = false;

	UPROPERTY()
	TEnumAsByte<EMovementMode> MovementModeOnEnd = EMovementMode::MOVE_Walking;

private:

	float TotalTime = 0.0f;
	bool bActive = false;
	bool bAcked = false;

public:

	FCharacterMotionData() = default;
	FCharacterMotionData(const FVector& InStartLocation, const FVector& InTargetLocation, const FRotator& InStartRotation, const FRotator& InTargetRotation, uint8 Duration);

	void Start()
	{
		Resume(0.0f);
	}

	void Stop()
	{
		bActive = false;
	}

	void Resume(float CurrentTotalTime)
	{
		TotalTime = CurrentTotalTime;
		bActive = true;
	}

	void Ack()
	{
		bAcked = true;
	}

	void Clear()
	{
		StartLocation = TargetLocation = FVector::ZeroVector;
		StartRotation = TargetRotation = FRotator::ZeroRotator;
		MaxZOffset = 0;
		Duration = 0;
		bSweepDuringMotion = false;

		TotalTime = 0.0f;
		bActive = false;
		bAcked = false;
	}

	bool HasValidData() const { return Duration > 0; }
	bool IsActive() const { return bActive;	}
	bool IsAcked() const { return bAcked; }
	float GetTotalTime() const { return TotalTime; }

	bool operator==(const FCharacterMotionData& Other) const
	{
		return !(*this != Other);
	}

	bool operator!=(const FCharacterMotionData& Other) const
	{
		return StartLocation != Other.StartLocation || TargetLocation != Other.TargetLocation || StartRotation != Other.StartRotation || TargetRotation != Other.TargetRotation
			|| Duration != Duration;
	}

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		bool bLocalSuccess = true;

		StartLocation.NetSerialize(Ar, Map, bLocalSuccess);
		TargetLocation.NetSerialize(Ar, Map, bLocalSuccess);

		uint32 YawPitchINT = 0;
		uint8 RollBYTE = 0;

		if (Ar.IsSaving())
		{
			// Compress rotation down to 5 bytes
			// StartRotation
			YawPitchINT = UCharacterMovementComponent::PackYawAndPitchTo32(StartRotation.Yaw, StartRotation.Pitch);
			RollBYTE = FRotator::CompressAxisToByte(StartRotation.Roll);

			Ar << YawPitchINT;
			Ar << RollBYTE;

			// TargetRotation
			YawPitchINT = UCharacterMovementComponent::PackYawAndPitchTo32(TargetRotation.Yaw, TargetRotation.Pitch);
			RollBYTE = FRotator::CompressAxisToByte(TargetRotation.Roll);

			Ar << YawPitchINT;
			Ar << RollBYTE;
		}
		else
		{
			Ar << YawPitchINT;
			Ar << RollBYTE;
			uint16 Pitch = (YawPitchINT & 65535);
			uint16 Yaw = (YawPitchINT >> 16);

			StartRotation.Pitch = FRotator::DecompressAxisFromShort(Pitch);
			StartRotation.Yaw = FRotator::DecompressAxisFromShort(Yaw);
			StartRotation.Roll = FRotator::DecompressAxisFromByte(RollBYTE);

			Ar << YawPitchINT;
			Ar << RollBYTE;
			Pitch = (YawPitchINT & 65535);
			Yaw = (YawPitchINT >> 16);

			TargetRotation.Pitch = FRotator::DecompressAxisFromShort(Pitch);
			TargetRotation.Yaw = FRotator::DecompressAxisFromShort(Yaw);
			TargetRotation.Roll = FRotator::DecompressAxisFromByte(RollBYTE);
		}

		Ar << MovementSpeedCurve;
		Ar << MovementZMultiplierCurve;
		Ar << MaxZOffset;
		Ar << Duration;
		Ar << bSweepDuringMotion;
		Ar << MovementModeOnEnd;
		
		bOutSuccess = bLocalSuccess;
		return !Ar.IsError();
	}
};

template<>
struct TStructOpsTypeTraits<FCharacterMotionData> : public TStructOpsTypeTraitsBase2<FCharacterMotionData>
{
	enum
	{
		WithNetSerializer = true,
	};
};

// Data used by the saved move structure to save data about the current character motion
struct FSavedCharacterMotionData
{
	float TotalTime = 0.0;
	bool bHasValidData = false;
	bool bIsActive = false;
};

struct FCharacterNetworkMoveData_Custom : public FCharacterNetworkMoveData
{
	using Super = FCharacterNetworkMoveData;

	/**
	 * Given a FSavedMove_Character from UCharacterMovementComponent, fill in data in this struct with relevant movement data.
	 * Note that the instance of the FSavedMove_Character is likely a custom struct of a derived struct of your own, if you have added your own saved move data.
	 * @see UCharacterMovementComponent::AllocateNewMove()
	 */
	virtual void ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType) override;

	/**
	 * Serialize the data in this struct to or from the given FArchive. This packs or unpacks the data in to a variable-sized data stream that is sent over the
	 * network from client to server.
	 * @see UCharacterMovementComponent::CallServerMovePacked
	 */
	virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap, ENetworkMoveType MoveType) override;

	// Used to determine whether of not serialize motion data
	bool bMotionDataValid = false;

	// Data de-serialized on the server
	FCharacterMotionData NetMotionData;
};


struct FCharacterNetworkMoveDataContainer_Custom : public FCharacterNetworkMoveDataContainer
{
public:

	FCharacterNetworkMoveDataContainer_Custom()
		: FCharacterNetworkMoveDataContainer()
	{
		NewMoveData = &MoveDataCustom[0];
		PendingMoveData = &MoveDataCustom[1];
		OldMoveData = &MoveDataCustom[2];
	}

private:

	FCharacterNetworkMoveData_Custom MoveDataCustom[3];
};

struct FCharacterMoveResponseDataContainer_Custom : public FCharacterMoveResponseDataContainer
{
	using Super = FCharacterMoveResponseDataContainer;

	/**
	 * Copy the FClientAdjustment and set a few flags relevant to that data.
	 */
	virtual void ServerFillResponseData(const UCharacterMovementComponent& CharacterMovement, const FClientAdjustment& PendingAdjustment);

	/**
	 * Serialize the FClientAdjustment data and other internal flags.
	 */
	virtual bool Serialize(UCharacterMovementComponent& CharacterMovement, FArchive& Ar, UPackageMap* PackageMap);

	// Add data to be sent from the server
	//float ServerTotalTime = 0.0f;
};

/**
 * 
 */
UCLASS()
class MYPROJECT_API UMyCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:

	UMyCharacterMovementComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void ServerMove_PerformMovement(const FCharacterNetworkMoveData& MoveData) override;
	virtual void ClientAckGoodMove_Implementation(float TimeStamp) override;

	virtual void StartMotion(const FCharacterMotionData& NewMotionData);
	virtual void ResumeMotion(float CurrentTotalTime);

	const FCharacterMotionData& GetCurrentMotionData() const { return MotionData; }
	FCharacterMotionData& GetCurrentMotionData() { return MotionData; }

protected:

	virtual void OnClientCorrectionReceived(FNetworkPredictionData_Client_Character& ClientData, float TimeStamp, FVector NewLocation, FVector NewVelocity, UPrimitiveComponent* NewBase, FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition, uint8 ServerMovementMode) override;
	virtual void PhysCustom(float deltaTime, int32 Iterations) override;

	virtual void PhysCustomMotion(float DeltaTime);

	FCharacterMotionData MotionData;

	FCharacterNetworkMoveDataContainer_Custom CustomNetworkMoveDataContainer;
	FCharacterMoveResponseDataContainer_Custom CustomMoveResponseContainer;

	/* Read the state flags provided by CompressedFlags and trigger the ability on the server */
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;

	/* A necessary override to make sure that our custom FNetworkPredictionData defined below is used */
	virtual class FNetworkPredictionData_Client* GetPredictionData_Client() const override;
};

/*
 * This custom implementation of FSavedMove_Character is used for 2 things:
 * 1. To replicate special ability flags using the compressed flags
 * 2. To shuffle saved move data around
 */
class FSavedMove_Character_Custom : public FSavedMove_Character
{
public:
	typedef FSavedMove_Character Super;

	/* Sets the default values for the saved move */
	virtual void Clear() override;

	/* Packs state data into a set of compressed flags. This is undone above in UpdateFromCompressedFlags */
	virtual uint8 GetCompressedFlags() const override;

	/* Checks if an old move can be combined with a new move for replication purposes (are they different or the same) */
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override;

	/* Populates the FSavedMove fields from the corresponding character movement controller variables. This is used when
	 * making a new SavedMove and the data will be used when playing back saved moves in the event that a correction needs to happen.*/
	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel, class FNetworkPredictionData_Client_Character& ClientData) override;

	/* This is used when the server plays back our saved move(s). This basically does the exact opposite of what
	 * SetMoveFor does. Here we set the character movement controller variables from the data in FSavedMove. */
	virtual void PrepMoveFor(class ACharacter* Character) override;

	/** Set the properties describing the final position, etc. of the moved pawn. */
	virtual void PostUpdate(ACharacter* Character, EPostUpdateMode PostUpdateMode) override;

	FSavedCharacterMotionData SavedMotionData;
};

/*
 * This subclass of FNetworkPredictionData_Client_Character is used to create new copies of
 * our custom FSavedMove_Character class defined above.
 */
class FNetworkPredictionData_Client_Character_Custom : public FNetworkPredictionData_Client_Character
{
public:
	using Super = FNetworkPredictionData_Client_Character;

	FNetworkPredictionData_Client_Character_Custom(const UCharacterMovementComponent& ClientMovement) : Super(ClientMovement) {}

	/* Allocates a new copy of our custom saved move */
	virtual FSavedMovePtr AllocateNewMove() override;
};
