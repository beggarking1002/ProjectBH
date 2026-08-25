// Copyright ProjectBH. All Rights Reserved.

#include "BHGE_BasicAttackDamage.h"

#include "ProjectBH/AbilitySystem/BHAttributeSet.h"

UBHGE_BasicAttackDamage::UBHGE_BasicAttackDamage()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FGameplayModifierInfo DamageModifier;
	DamageModifier.Attribute = UBHAttributeSet::GetHealthAttribute();
	DamageModifier.ModifierOp = EGameplayModOp::Additive;
	DamageModifier.ModifierMagnitude = FScalableFloat(-20.0f);
	Modifiers.Add(DamageModifier);
}
