﻿// Fill out your copyright notice in the Description page of Project Settings.


#include "Debug/GameplayDebuggerCategory_GMCAbilitySystem.h"

#include "Components/GMCAbilityComponent.h"

FGameplayDebuggerCategory_GMCAbilitySystem::FGameplayDebuggerCategory_GMCAbilitySystem()
{
	SetDataPackReplication<FRepData>(&DataPack);
	bShowOnlyWithDebugActor = false;
}

void FGameplayDebuggerCategory_GMCAbilitySystem::CollectData(APlayerController* OwnerPC, AActor* DebugActor)
{
	if (OwnerPC)
	{
		DataPack.ActorName = OwnerPC->GetPawn()->GetName();

		if (const UGMC_AbilitySystemComponent* AbilityComponent = OwnerPC->GetPawn()->FindComponentByClass<UGMC_AbilitySystemComponent>())
		{
			DataPack.GrantedAbilities = AbilityComponent->GetGrantedAbilities().ToStringSimple();
			DataPack.ActiveTags = AbilityComponent->GetActiveTags().ToStringSimple();
		}
	}
}

void FGameplayDebuggerCategory_GMCAbilitySystem::DrawData(APlayerController* OwnerPC,
	FGameplayDebuggerCanvasContext& CanvasContext)
{
	if (!DataPack.ActorName.IsEmpty())
	{
		CanvasContext.Printf(TEXT("{yellow}Actor name: {white}%s"), *DataPack.ActorName);

		// Abilities
		CanvasContext.Printf(TEXT("{blue}[server] {yellow}Granted Abilities: {white}%s"), *DataPack.GrantedAbilities);
		// Show client-side data
		if (const UGMC_AbilitySystemComponent* AbilityComponent = OwnerPC->GetPawn()->FindComponentByClass<UGMC_AbilitySystemComponent>())
		{
			CanvasContext.Printf(TEXT("{green}[client] {yellow}Granted Abilities: {white}%s"), *AbilityComponent->GetGrantedAbilities().ToStringSimple());
		}

		// Tags
		CanvasContext.Printf(TEXT("{blue}[server] {yellow}Active Tags: {white}%s"), *DataPack.ActiveTags);
		// Show client-side data
		if (const UGMC_AbilitySystemComponent* AbilityComponent = OwnerPC->GetPawn()->FindComponentByClass<UGMC_AbilitySystemComponent>())
		{
			CanvasContext.Printf(TEXT("{green}[client] {yellow}Active Tags: {white}%s"), *AbilityComponent->GetActiveTags().ToStringSimple());
		}
		
	}
}

TSharedRef<FGameplayDebuggerCategory> FGameplayDebuggerCategory_GMCAbilitySystem::MakeInstance()
{
	return MakeShareable(new FGameplayDebuggerCategory_GMCAbilitySystem());
}

void FGameplayDebuggerCategory_GMCAbilitySystem::FRepData::Serialize(FArchive& Ar)
{
	Ar << ActorName;
	Ar << GrantedAbilities;
	Ar << ActiveTags;
}
