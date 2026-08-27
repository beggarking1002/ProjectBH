// Copyright ProjectBH. All Rights Reserved.

#include "DataAsset_EnemyConfig.h"

const FBHEnemyAttackConfig* UDataAsset_EnemyConfig::FindAttackById(FName AttackId) const
{
	return Attacks.FindByPredicate(
		[AttackId](const FBHEnemyAttackConfig& Attack)
		{
			return Attack.AttackId == AttackId;
		});
}

const FBHEnemyAttackConfig* UDataAsset_EnemyConfig::FindDefaultAttack() const
{
	return FindAttackById(DefaultAttackId);
}
