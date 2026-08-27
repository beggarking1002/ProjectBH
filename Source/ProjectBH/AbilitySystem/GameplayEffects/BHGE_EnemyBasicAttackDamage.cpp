// Copyright ProjectBH. All Rights Reserved.

#include "BHGE_EnemyBasicAttackDamage.h"

#include "../BHAttributeSet.h"
#include "../../BHGameplayTags.h"

UBHGE_EnemyBasicAttackDamage::UBHGE_EnemyBasicAttackDamage()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FGameplayModifierInfo DamageModifier;
	DamageModifier.Attribute = UBHAttributeSet::GetHealthAttribute();
	DamageModifier.ModifierOp = EGameplayModOp::Additive;
	FSetByCallerFloat SetByCallerDamage;
	SetByCallerDamage.DataTag = BHGameplayTags::Data_Damage;
	DamageModifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCallerDamage);
	Modifiers.Add(DamageModifier);
}
