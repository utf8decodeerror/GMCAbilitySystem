﻿// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/GMCAbilityComponent.h"

#include "GMCAbilitySystem.h"
#include "GMCOrganicMovementComponent.h"
#include "GMCPlayerController.h"
#include "InterchangeResult.h"
#include "Attributes/GMCAttributesData.h"
#include "Effects/GMCAbilityEffect.h"
#include "Engine/ObjectLibrary.h"
#include "Net/UnrealNetwork.h"


// Sets default values for this component's properties
UGMC_AbilitySystemComponent::UGMC_AbilitySystemComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UGMC_AbilitySystemComponent::BindReplicationData()
{
	// Attribute Binds
	//
	InstantiateAttributes();
	
	// Sync'd Action Timer
	GMCMovementComponent->BindDoublePrecisionFloat(ActionTimer,
		EGMC_PredictionMode::ServerAuth_Output_ClientValidated,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::None,
		EGMC_InterpolationFunction::TargetValue);

	// Granted Abilities
	GMCMovementComponent->BindGameplayTagContainer(GrantedAbilityTags,
		EGMC_PredictionMode::ServerAuth_Output_ClientValidated,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::None,
		EGMC_InterpolationFunction::TargetValue);

	// Active Tags
	GMCMovementComponent->BindGameplayTagContainer(ActiveTags,
		EGMC_PredictionMode::ServerAuth_Output_ClientValidated,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::Periodic_Output,
		EGMC_InterpolationFunction::TargetValue);

	// Attributes
	BindInstancedStruct(BoundAttributes,
		EGMC_PredictionMode::ServerAuth_Output_ClientValidated,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::Periodic_Output,
		EGMC_InterpolationFunction::TargetValue);
	
	// AbilityData Binds
	// These are mostly client-inputs made to the server as Ability Requests
	GMCMovementComponent->BindInt(AbilityData.AbilityActivationID,
		EGMC_PredictionMode::ClientAuth_Input,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::None,
		EGMC_InterpolationFunction::TargetValue);

	GMCMovementComponent->BindGameplayTag(AbilityData.AbilityTag,
		EGMC_PredictionMode::ClientAuth_Input,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::None,
		EGMC_InterpolationFunction::TargetValue);
	
	// TaskData Bind
	BindInstancedStruct(TaskData,
		EGMC_PredictionMode::ClientAuth_Input,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::None,
		EGMC_InterpolationFunction::TargetValue);

	GMCMovementComponent->BindBool(bJustTeleported,
		EGMC_PredictionMode::ServerAuth_Output_ClientValidated,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::PeriodicAndOnChange_Output,
		EGMC_InterpolationFunction::TargetValue);
	
}
void UGMC_AbilitySystemComponent::GenAncillaryTick(float DeltaTime, bool bIsCombinedClientMove)
{
	
}

void UGMC_AbilitySystemComponent::GrantAbilityByTag(FGameplayTag AbilityTag)
{
	if (!GrantedAbilityTags.HasTag(AbilityTag))
	{
		GrantedAbilityTags.AddTag(AbilityTag);
	}
}

void UGMC_AbilitySystemComponent::RemoveGrantedAbilityByTag(FGameplayTag AbilityTag)
{
	if (GrantedAbilityTags.HasTag(AbilityTag))
	{
		GrantedAbilityTags.RemoveTag(AbilityTag);
	}
}

void UGMC_AbilitySystemComponent::AddActiveTag(FGameplayTag AbilityTag)
{
	ActiveTags.AddTag(AbilityTag);
}

void UGMC_AbilitySystemComponent::RemoveActiveTag(FGameplayTag AbilityTag)
{
	if (ActiveTags.HasTag(AbilityTag))
	{
		ActiveTags.RemoveTag(AbilityTag);
	}
}

bool UGMC_AbilitySystemComponent::HasActiveTag(FGameplayTag GameplayTag) const
{
	return ActiveTags.HasTag(GameplayTag);
}

bool UGMC_AbilitySystemComponent::TryActivateAbility(FGameplayTag AbilityTag, UInputAction* InputAction)
{
	// UE_LOG(LogTemp, Warning, TEXT("Trying To Activate Ability: %d"), AbilityData.GrantedAbilityIndex);
	if (const TSubclassOf<UGMCAbility> ActivatedAbility = GetGrantedAbilityByTag(AbilityTag))
	{
		// Generated ID is based on ActionTimer so it always lines up on client/server
		// Also helps when dealing with replays
		const int AbilityID = GenerateAbilityID();

		// Replays may try to create duplicate abilities
		if (ActiveAbilities.Contains(AbilityID)) return false;
		
		//UE_LOG(LogGMCAbilitySystem, Warning, TEXT("Generated Ability Activation ID: %d"), InAbilityData.AbilityActivationID);
		
		UGMCAbility* Ability = NewObject<UGMCAbility>(this, ActivatedAbility);
		
		// Check to make sure there's enough resource to use
		if (!CanAffordAbilityCost(Ability)) return false;

		Ability->Execute(this, AbilityID, InputAction);
		ActiveAbilities.Add(AbilityID, Ability);
		
		return true;
	}

	return false;
}

void UGMC_AbilitySystemComponent::QueueAbility(FGameplayTag AbilityTag, UInputAction* InputAction)
{
	if (GetOwnerRole() != ROLE_AutonomousProxy && GetOwnerRole() != ROLE_Authority) return;
	
	FGMCAbilityData Data;
	Data.AbilityTag = AbilityTag;
	Data.ActionInput = InputAction;
	QueuedAbilities.Push(Data);
}

void UGMC_AbilitySystemComponent::QueueTaskData(const FInstancedStruct& InTaskData)
{
	QueuedTaskData.Push(InTaskData);
}	

void UGMC_AbilitySystemComponent::GenPredictionTick(float DeltaTime, bool bIsReplayingPrediction)
{
	bJustTeleported = false;
	ActionTimer += DeltaTime;
	
	// Startup Effects
	if (StartingEffects.Num() > 0)
	{
		for (const TSubclassOf<UGMCAbilityEffect> Effect : StartingEffects)
		{
			ApplyAbilityEffect(Effect, FGMCAbilityEffectData{});
		}
		StartingEffects.Empty();
	}
	
	TickActiveEffects(DeltaTime, bIsReplayingPrediction);
	TickActiveAbilities(DeltaTime);
	
	// Abilities
	CleanupStaleAbilities();

	// Was an ability used?
	if (AbilityData.AbilityTag != FGameplayTag::EmptyTag)
	{
		TryActivateAbility(AbilityData.AbilityTag, AbilityData.ActionInput);
	}

	// Ability Task Data
	const FGMCAbilityTaskData TaskDataFromInstance = TaskData.Get<FGMCAbilityTaskData>();
	if (TaskDataFromInstance != FGMCAbilityTaskData{} && /*safety check*/ TaskDataFromInstance.TaskID >= 0)
	{
			if (ActiveAbilities.Contains(TaskDataFromInstance.AbilityID))
			{
				ActiveAbilities[TaskDataFromInstance.AbilityID]->ProgressTask(TaskDataFromInstance.TaskID, TaskData);
			}
	}
	
	AbilityData = FGMCAbilityData{};
	TaskData = FInstancedStruct::Make(FGMCAbilityTaskData{});
}

void UGMC_AbilitySystemComponent::GenSimulationTick(float DeltaTime)
{
	if (GMCMovementComponent->GetSmoothingTargetIdx() == -1) return;	
	
	const FVector TargetLocation = GMCMovementComponent->MoveHistory[GMCMovementComponent->GetSmoothingTargetIdx()].OutputState.ActorLocation.Read();
	if (bJustTeleported)
	{
		// UE_LOG(LogTemp, Warning, TEXT("Teleporting %f Units"), FVector::Distance(GetOwner()->GetActorLocation(), TargetLocation));
		GetOwner()->SetActorLocation(TargetLocation);
		bJustTeleported = false;
	}
}

void UGMC_AbilitySystemComponent::PreLocalMoveExecution()
{
	if (QueuedAbilities.Num() > 0)
	{
		AbilityData = QueuedAbilities.Pop();
	}
	if (QueuedTaskData.Num() > 0)
	{
		TaskData = QueuedTaskData.Pop();
	}
}

void UGMC_AbilitySystemComponent::BeginPlay()
{
	Super::BeginPlay();
	InitializeAbilityMap();
	InitializeEffectAssetClasses();
	InitializeStartingAbilities();
}


bool UGMC_AbilitySystemComponent::CanAffordAbilityCost(UGMCAbility* Ability)
{
	if (Ability->AbilityCost == nullptr) return true;
	
	// UGMCAbilityEffect* AbilityEffect = Ability->AbilityCost->GetDefaultObject<UGMCAbilityEffect>();
	// for (FGMCAttributeModifier AttributeModifier : AbilityEffect->Modifiers)
	// {
	// 	if (const FAttribute* AffectedAttribute = Attributes->GetAttributeByName(AttributeModifier.AttributeName))
	// 	{
	// 		// If current - proposed is less than 0, cannot afford. Cost values are negative, so these are added.
	// 		if (AffectedAttribute->Value + AttributeModifier.Value < 0) return false;
	// 	}
	// }

	return true;
}

void UGMC_AbilitySystemComponent::InstantiateAttributes()
{
	BoundAttributes = FInstancedStruct::Make<FGMCAttributeSet>();
	UnBoundAttributes = FInstancedStruct::Make<FGMCAttributeSet>();
	if(AttributeDataAssets.IsEmpty()) return;

	// Loop through each of the data assets inputted into the component to create new attributes.
	for(UGMCAttributesData* AttributeDataAsset : AttributeDataAssets){
		for(const FAttributeData AttributeData : AttributeDataAsset->AttributeData){
			FAttribute NewAttribute;
			NewAttribute.Tag = AttributeData.Tag;
			NewAttribute.Value = AttributeData.DefaultValue;
			NewAttribute.bIsGMCBound = AttributeData.bShouldBind;
			if(AttributeData.bShouldBind){
				BoundAttributes.GetMutable<FGMCAttributeSet>().AddAttribute(NewAttribute);
			}
			else{
				UnBoundAttributes.GetMutable<FGMCAttributeSet>().AddAttribute(NewAttribute);
			}
		}
	}
}

void UGMC_AbilitySystemComponent::CleanupStaleAbilities()
{
	for (auto It = ActiveAbilities.CreateIterator(); It; ++It)
	{
		// If the contained ability is in the Ended state, delete it
		if (It.Value()->AbilityState == EAbilityState::Ended)
		{
			It.RemoveCurrent();
		}
	}
}

void UGMC_AbilitySystemComponent::TickActiveEffects(float DeltaTime, bool bIsReplayingPrediction)
{
	CheckRemovedEffects();
	
	TArray<UGMCAbilityEffect*> CompletedPredictedEffects;
	TArray<int> CompletedActiveEffects;

	// Tick Effects
	for (const TPair<int, UGMCAbilityEffect*>& Effect : ActiveEffects)
	{
		Effect.Value->Tick(DeltaTime, bIsReplayingPrediction);
		if (Effect.Value->bCompleted) {CompletedActiveEffects.Push(Effect.Key);}
	}
	
	// Clean expired effects
	for (const int EffectID : CompletedActiveEffects)
	{
		ActiveEffects.Remove(EffectID);
		ActiveEffectsData.RemoveAll([EffectID](const FGMCAbilityEffectData& EffectData) {return EffectData.EffectID == EffectID;});
	}
}

void UGMC_AbilitySystemComponent::TickActiveAbilities(float DeltaTime)
{

	for (const TPair<int, UGMCAbility*>& Ability : ActiveAbilities)
	{
		Ability.Value->Tick(DeltaTime);
	}
}

void UGMC_AbilitySystemComponent::OnRep_ActiveEffectsData()
{
	for (FGMCAbilityEffectData ActiveEffectData : ActiveEffectsData)
	{
		if (ActiveEffectData.EffectID == 0) continue;
		
		if (!ProcessedEffectIDs.Contains(ActiveEffectData.EffectID))
		{
			UGMCAbilityEffect* EffectCDO = DuplicateObject(UGMCAbilityEffect::StaticClass()->GetDefaultObject<UGMCAbilityEffect>(), this);
			FGMCAbilityEffectData EffectData = ActiveEffectData;
			ApplyAbilityEffect(EffectCDO, EffectData);
			ProcessedEffectIDs.Add(EffectData.EffectID, true);
		// 	UE_LOG(LogGMCAbilitySystem, Warning, TEXT("Replicated Effect: %d"), ActiveEffectData.EffectID);
		}
		
		ProcessedEffectIDs[ActiveEffectData.EffectID] = true;
	}
}

void UGMC_AbilitySystemComponent::CheckRemovedEffects()
{
	for (TPair<int, UGMCAbilityEffect*> Effect : ActiveEffects)
	{
		// Ensure this effect has been processed locally
		if (!ProcessedEffectIDs.Contains(Effect.Key)){return;}

		// Ensure this effect has already been confirmed by the server so that if it's now missing,
		// it means the server removed it
		if (!ProcessedEffectIDs[Effect.Key]){return;}
		
		if (!ActiveEffectsData.ContainsByPredicate([Effect](const FGMCAbilityEffectData& EffectData) {return EffectData.EffectID == Effect.Key;}))
		{
			RemoveActiveAbilityEffect(Effect.Value);
		}
	}
}

TSubclassOf<UGMCAbility> UGMC_AbilitySystemComponent::GetGrantedAbilityByTag(FGameplayTag AbilityTag)
{
	if (!GrantedAbilityTags.HasTag(AbilityTag))
	{
		UE_LOG(LogGMCAbilitySystem, Warning, TEXT("Ability Tag Not Granted: %s"), *AbilityTag.ToString());
		return nullptr;
	}

	if (!AbilityMap.Contains(AbilityTag))
	{
		UE_LOG(LogGMCAbilitySystem, Warning, TEXT("Ability Tag Not Found: %s"), *AbilityTag.ToString());
		return nullptr;
	}

	return AbilityMap[AbilityTag];
}

void UGMC_AbilitySystemComponent::InitializeEffectAssetClasses()
{
	UObjectLibrary* Library = UObjectLibrary::CreateLibrary(UGMCAbilityEffect::StaticClass(), true, GIsEditor);
	Library->bRecursivePaths = true;
	Library->LoadBlueprintsFromPath("/Game");
	Library->GetObjects<UBlueprintGeneratedClass>(EffectBPClasses);
}

void UGMC_AbilitySystemComponent::InitializeAbilityMap()
{
	return;
	// This doesn't seem to work when abilities are contained inside plugins
	// Works super well if they're in the main game content
	// Forcing people to manually define the map for now
	UObjectLibrary* Library = UObjectLibrary::CreateLibrary(UGMCAbility::StaticClass(), true, GIsEditor);
	Library->bRecursivePaths = true;
	Library->LoadBlueprintsFromPath("/Game");
	TArray<UBlueprintGeneratedClass *> AbilityBPClasses;
	Library->GetObjects<UBlueprintGeneratedClass>(AbilityBPClasses);

	for (UBlueprintGeneratedClass* AbilityBPClass : AbilityBPClasses)
	{
		UGMCAbility* CDO = AbilityBPClass->GetDefaultObject<UGMCAbility>();

		// Skip the BP generated SKELs
		if (CDO->GetName().Contains("SKEL")) continue;

		// Check for missing tags and if SKEL is present (BP generated)
		if (CDO->AbilityTag == FGameplayTag::EmptyTag)
		{
			UE_LOG(LogGMCAbilitySystem, Warning, TEXT("Ability Class Missing Tag: %s"), *AbilityBPClass->GetName());
			continue;
		}

		if (AbilityMap.Contains(CDO->AbilityTag))
		{
			UE_LOG(LogGMCAbilitySystem, Error, TEXT("Duplicate Ability Tag: %s"), *CDO->AbilityTag.ToString());
			continue;
		}
		
		AbilityMap.Add(CDO->AbilityTag, AbilityBPClass);
	}
}

void UGMC_AbilitySystemComponent::InitializeStartingAbilities()
{
	for (const TSubclassOf<UGMCAbility> Ability : StartingAbilities)
	{
		if (Ability == nullptr) return;
		
		const UGMCAbility* CDO = Ability->GetDefaultObject<UGMCAbility>();

		if (CDO->AbilityTag == FGameplayTag::EmptyTag)
		{
			UE_LOG(LogGMCAbilitySystem, Error, TEXT("Starting Ability Missing Tag: %s"), *Ability->GetName());
			continue;
		}

		GrantedAbilityTags.AddTag(CDO->AbilityTag);
	}
}

void UGMC_AbilitySystemComponent::OnRep_UnBoundAttributes(FInstancedStruct OldStruct){
	
}

void UGMC_AbilitySystemComponent::ApplyAbilityCost(UGMCAbility* Ability)
{
	if (Ability->AbilityCost != nullptr)
	{
		const UGMCAbilityEffect* EffectCDO = DuplicateObject(Ability->AbilityCost->GetDefaultObject<UGMCAbilityEffect>(), this);
		FGMCAbilityEffectData EffectData = EffectCDO->EffectData;
		EffectData.OwnerAbilityComponent = this;
		ApplyAbilityEffect(DuplicateObject(EffectCDO, this), EffectData);
	}
}

//BP Version
UGMCAbilityEffect* UGMC_AbilitySystemComponent::ApplyAbilityEffect(TSubclassOf<UGMCAbilityEffect> Effect, FGMCAbilityEffectData InitializationData)
{
	if (Effect == nullptr) return nullptr;
	
	UGMCAbilityEffect* AbilityEffect = DuplicateObject(Effect->GetDefaultObject<UGMCAbilityEffect>(), this);
	
	FGMCAbilityEffectData EffectData;
	if (InitializationData.IsValid())
	{
		EffectData = InitializationData;
	}
	else
	{
		EffectData = AbilityEffect->EffectData;
	}
	
	ApplyAbilityEffect(AbilityEffect, EffectData);
	return AbilityEffect;
}

UGMCAbilityEffect* UGMC_AbilitySystemComponent::ApplyAbilityEffect(UGMCAbilityEffect* Effect, FGMCAbilityEffectData InitializationData)
{
	if (Effect == nullptr) return nullptr;
	// Force the component this is being applied to to be the owner
	InitializationData.OwnerAbilityComponent = this;
	
	Effect->InitializeEffect(InitializationData);
	
	if (Effect->EffectData.EffectID == 0)
	{
		// Todo: Need a better way to generate EffectIDs
		int NewEffectID = static_cast<int>(ActionTimer * 1000);
		while (ActiveEffects.Contains(NewEffectID))
		{
			NewEffectID++;
		}
		Effect->EffectData.EffectID = NewEffectID;
		// UE_LOG(LogGMCAbilitySystem, Warning, TEXT("Generated Effect ID: %d"), Effect->EffectData.EffectID);
	}

	// This is Replicated, so only server needs to manage it
	if (HasAuthority())
	{
		ActiveEffectsData.Push(Effect->EffectData);
	}
	else
	{
		ProcessedEffectIDs.Add(Effect->EffectData.EffectID, false);
	}
	
	ActiveEffects.Add(Effect->EffectData.EffectID, Effect);
	return Effect;
}

void UGMC_AbilitySystemComponent::RemoveActiveAbilityEffect(UGMCAbilityEffect* Effect)
{
	if (Effect == nullptr)
	{
		return;
	}
	
	if (!ActiveEffects.Contains(Effect->EffectData.EffectID)) return;
	Effect->EndEffect();
}


TArray<const FAttribute*> UGMC_AbilitySystemComponent::GetAllAttributes() const{
	TArray<const FAttribute*> AllAttributes;
	for (const FAttribute& Attribute : UnBoundAttributes.Get<FGMCAttributeSet>().Attributes){
		AllAttributes.Add(&Attribute);
	}
	for (const FAttribute& Attribute : BoundAttributes.Get<FGMCAttributeSet>().Attributes){
		AllAttributes.Add(&Attribute);
	}
	return AllAttributes;
}

const FAttribute* UGMC_AbilitySystemComponent::GetAttributeByTag(FGameplayTag AttributeTag) const{
	TArray<const FAttribute*> AllAttributes = GetAllAttributes();
	const FAttribute** FoundAttribute = AllAttributes.FindByPredicate([AttributeTag](const FAttribute* Attribute){
		return Attribute->Tag.MatchesTagExact(AttributeTag);
	});
	return *FoundAttribute;
}

#pragma region ToStringHelpers

FString UGMC_AbilitySystemComponent::GetAllAttributesString() const{
	FString FinalString = TEXT("\n");
	for (const FAttribute* Attribute : GetAllAttributes()){
		FinalString += Attribute->ToString() + TEXT("\n");
	}
	return FinalString;
}

FString UGMC_AbilitySystemComponent::GetActiveEffectsDataString() const{
	FString FinalString = TEXT("\n");
	for(const FGMCAbilityEffectData& ActiveEffectData : ActiveEffectsData){
		FinalString += ActiveEffectData.ToString() + TEXT("\n");
	}
	return FinalString;
}

FString UGMC_AbilitySystemComponent::GetActiveEffectsString() const{
	FString FinalString = TEXT("\n");
	for(const TTuple<int, UGMCAbilityEffect*> ActiveEffect : ActiveEffects){
		FinalString += ActiveEffect.Value->ToString() + TEXT("\n");
	}
	return FinalString;
}

FString UGMC_AbilitySystemComponent::GetActiveAbilitiesString() const{
	FString FinalString = TEXT("\n");
	for(const TTuple<int, UGMCAbility*> ActiveAbility : ActiveAbilities){
		FinalString += ActiveAbility.Value->ToString() + TEXT("\n");
	}
	return FinalString;
}

#pragma endregion  ToStringHelpers

void UGMC_AbilitySystemComponent::ApplyAbilityEffectModifier(FGMCAttributeModifier AttributeModifier, bool bNegateValue, UGMC_AbilitySystemComponent* SourceAbilityComponent)
{
	// Provide an opportunity to modify the attribute modifier before applying it
	UGMCAttributeModifierContainer* AttributeModifierContainer = DuplicateObject(UGMCAttributeModifierContainer::StaticClass()->GetDefaultObject<UGMCAttributeModifierContainer>(), this);
	AttributeModifierContainer->AttributeModifier = AttributeModifier;
	OnAttributeChanged.Broadcast(AttributeModifierContainer, SourceAbilityComponent);

	// Apply the modified attribute modifier. If no changes were made, it's just the same as the original
	// Extra copying going on here? Can this be done with a reference? BPs are weird.
	AttributeModifier = AttributeModifierContainer->AttributeModifier;
	
	if (const FAttribute* AffectedAttribute = GetAttributeByTag(AttributeModifier.AttributeTag))
	{
		// If we are unbound that means we shouldn't predict.
		if(!AffectedAttribute->bIsGMCBound && !HasAuthority()) return;
		
		if (bNegateValue)
		{
			AffectedAttribute->Value += -AttributeModifier.Value;
		}
		else
		{
			AffectedAttribute->Value += AttributeModifier.Value;
		}
	}
}

void UGMC_AbilitySystemComponent::RPCServerApplyEffect_Implementation(const FString& EffectClassName, FGMCAbilityEffectData EffectInitializationData)
{
	for (int32 i = 0; i < EffectBPClasses.Num(); ++i)
	{
		if (const UBlueprintGeneratedClass * BlueprintClass = EffectBPClasses[i]; EffectClassName == BlueprintClass->GetName())
		{
			UGMCAbilityEffect* Effect = DuplicateObject(BlueprintClass->GetDefaultObject<UGMCAbilityEffect>(), this);
			ApplyAbilityEffect(Effect, EffectInitializationData);
			// UE_LOG(LogTemp, Warning, TEXT("[GMCABILITY] Successfully Found Effect Class From Server: %s"), *EffectClassName);
			return;
		}
	}
	UE_LOG(LogGMCAbilitySystem, Error, TEXT("[GMCABILITY] Received Invalid Effect Class Name From Server: %s"), *EffectClassName);
}

// ReplicatedProps
void UGMC_AbilitySystemComponent::GetLifetimeReplicatedProps(TArray< FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UGMC_AbilitySystemComponent, ActiveEffectsData, COND_OwnerOnly);
	DOREPLIFETIME(UGMC_AbilitySystemComponent, UnBoundAttributes);
}


