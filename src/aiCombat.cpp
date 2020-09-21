#include "aiCombat.h"
#include "ai.h"
#include "engine.h"

std::unordered_map<int, COMBAT_ORDER> combatOrders;

/*
Prepares combat orders.
*/
void aiCombatStrategy()
{
	// populate lists

	populateCombatLists();

	// natives

	aiNativeCombatStrategy();

}

void populateCombatLists()
{
	combatOrders.clear();

}

/*
Composes anti-native combat strategy.
*/
void aiNativeCombatStrategy()
{
//	// protect bases
//
//	for (int id : baseIds)
//	{
//		BASE *base = &(tx_bases[id]);
//		BASE_STRATEGY *baseStrategy = &(baseStrategies[id]);
//
//		// maintain native protection
//
//		double totalNativeProtection = 0.0;
//
//		// police x2
//
//		for (int vehicleId : baseStrategy->garrison)
//		{
//			VEH *vehicle = &(tx_vehicles[vehicleId]);
//
//			// only police x2
//
//			if (!vehicle_has_ability(vehicle, ABL_POLICE_2X))
//				continue;
//
//			// vehicle native protection
//
//			double nativeProtection = estimateVehicleBaseLandNativeProtection(base->faction_id, vehicleId);
//
//			// update total
//
//			totalNativeProtection += nativeProtection;
//
//			// check if vehicle is needed
//
//			if (totalNativeProtection < conf.ai_production_max_native_protection)
//			{
//				setDefendOrder(vehicleId, base->x, base->y);
//			}
//
//		}
//
//		// trance
//
//		for (int vehicleId : baseStrategy->garrison)
//		{
//			VEH *vehicle = &(tx_vehicles[vehicleId]);
//
//			// only trance
//
//			if (!vehicle_has_ability(vehicle, ABL_TRANCE))
//				continue;
//
//			// vehicle native protection
//
//			double nativeProtection = estimateVehicleBaseLandNativeProtection(base->faction_id, vehicleId);
//
//			// update total
//
//			totalNativeProtection += nativeProtection;
//
//			// check if vehicle is needed
//
//			if (totalNativeProtection < conf.ai_production_max_native_protection)
//			{
//				setDefendOrder(vehicleId, base->x, base->y);
//			}
//
//		}
//
//		// scout patrols
//
//		for (int vehicleId : baseStrategy->garrison)
//		{
//			VEH *vehicle = &(tx_vehicles[vehicleId]);
//
//			// only scout patrols
//
//			if (vehicle->proto_id != BSC_SCOUT_PATROL)
//				continue;
//
//			// vehicle native protection
//
//			double nativeProtection = estimateVehicleBaseLandNativeProtection(base->faction_id, vehicleId);
//
//			// update total
//
//			totalNativeProtection += nativeProtection;
//
//			// check if vehicle is needed
//
//			if (totalNativeProtection < conf.ai_production_max_native_protection)
//			{
//				setDefendOrder(vehicleId, base->x, base->y);
//			}
//
//		}
//
//	}
//
	// check native artillery

	for (int id = 0; id < *total_num_vehicles; id++)
	{
		VEH *vehicle = &(tx_vehicles[id]);

		// native units only

		if (vehicle->faction_id != 0)
			continue;

		// spore launchers

		if (vehicle->proto_id == BSC_SPORE_LAUNCHER)
		{
			attackNativeArtillery(id);
		}

	}

}

void attackNativeArtillery(int enemyVehicleId)
{
	VEH *enemyVehicle = &(tx_vehicles[enemyVehicleId]);

	// check if there are our improvements in bombardment range

	bool danger = false;

	for (int dx = -4; dx <= +4; dx++)
	{
		for (int dy = -(4 - abs(dx)); dy <= +(4 - abs(dx)); dy += 2)
		{
			int x = enemyVehicle->x + dx;
			int y = enemyVehicle->y + dy;
			MAP *tile = getMapTile(x, y);

			if (!tile)
				continue;

			if (tile->owner != aiFactionId)
				continue;

			if (map_base(tile) || isImprovedTile(x, y))
			{
				danger = true;
				break;
			}

		}
	}

	if (!danger)
		return;

	// assemble attacking forces

	debug("attackNativeArtillery (%3d,%3d)\n", enemyVehicle->x, enemyVehicle->y);

	// list available units

	std::vector<VEHICLE_DAMAGE_VALUE> vehicleDamageValues;

	for (int id : combatVehicleIds)
	{
		VEH *vehicle = &(tx_vehicles[id]);

		// do not bother if unreachable

		if (!isreachable(id, enemyVehicle->x, enemyVehicle->y))
			continue;

		// calculate damage

		double damage = (*current_turn < 15 ? 2.0 : 1.0) * calculatePsiDamageAttack(id, enemyVehicleId);

		// calculate value

		int distance = map_range(vehicle->x, vehicle->y, enemyVehicle->x, enemyVehicle->y);
		int cost = tx_units[vehicle->proto_id].cost;
		double value = damage / (double)cost / (double)distance;

		// store vehicle

		vehicleDamageValues.push_back({id, damage, value});

	}

	// sort vehicles

	std::sort(vehicleDamageValues.begin(), vehicleDamageValues.end(), compareVehicleDamageValue);

	// select attackers

	double enemyVehicleHealth = (double)(10 - enemyVehicle->damage_taken);
	double requiredDamage = 2.0 * enemyVehicleHealth;
	double combinedDamage = 0.0;

	debug
	(
		"enemyVehicleHealth=%4.1f, requiredDamage=%4.1f\n",
		enemyVehicleHealth,
		requiredDamage
	);

	for (VEHICLE_DAMAGE_VALUE vehicleDamageValue : vehicleDamageValues)
	{
		// store order

		combatOrders[vehicleDamageValue.id] = {};
		combatOrders[vehicleDamageValue.id].enemyAIId = enemyVehicleId;

		// deliver damage

		combinedDamage += vehicleDamageValue.damage;

		debug
		(
			"\t[%3d] value=%4.1f, damage=%4.1f, combinedDamage=%4.1f\n",
			vehicleDamageValue.id,
			vehicleDamageValue.value,
			vehicleDamageValue.damage,
			combinedDamage
		);

		// enough!

		if (combinedDamage >= requiredDamage)
			break;

	}

	debug("\n");

}

int compareVehicleDamageValue(VEHICLE_DAMAGE_VALUE o1, VEHICLE_DAMAGE_VALUE o2)
{
	return (o1.value > o2.value);
}

int enemyMoveCombat(int id)
{
	// use WTP algorithm for selected faction only

	if (wtpAIFactionId != -1 && aiFactionId != wtpAIFactionId)
		return SYNC;

	// get vehicle

	VEH *vehicle = &(tx_vehicles[id]);

	// restore ai id

	int aiVehicleId = vehicle->pad_0;

	debug("[%d->%d] (%3d,%3d)\n", aiVehicleId, id, vehicle->x, vehicle->y);

	// find vehicle order

	std::unordered_map<int, COMBAT_ORDER>::iterator combatOrdersIterator = combatOrders.find(aiVehicleId);

	// skip vehicles without orders

	if (combatOrdersIterator == combatOrders.end())
		return tx_enemy_move(id);

	// get order

	COMBAT_ORDER *combatOrder = &(combatOrdersIterator->second);

	// apply order

	return applyCombatOrder(id, combatOrder);

}

int applyCombatOrder(int id, COMBAT_ORDER *combatOrder)
{
	if (combatOrder->defendX != -1 && combatOrder->defendY != -1)
	{
		return applyDefendOrder(id, combatOrder->defendX, combatOrder->defendY);
	}
	if (combatOrder->enemyAIId != -1)
	{
		return applyAttackOrder(id, combatOrder);
	}
	else
	{
		return tx_enemy_move(id);
	}

}

int applyDefendOrder(int id, int x, int y)
{
	VEH *vehicle = &(tx_vehicles[id]);

	// at destination

	if (vehicle->x == x && vehicle->y == y)
	{
		// hold position

		setVehicleOrder(id, ORDER_HOLD);

	}

	// not at destination

	else
	{
		set_move_to(id, x, y);
	}

	return SYNC;

}

int applyAttackOrder(int id, COMBAT_ORDER *combatOrder)
{
	// find enemy vehicle by AI ID

	VEH *enemyVehicle = getVehicleByAIId(combatOrder->enemyAIId);

	// enemy not found

	if (enemyVehicle == NULL)
		return tx_enemy_move(id);

	// enemy is unreachable

	if (!isreachable(id, enemyVehicle->x, enemyVehicle->y))
		return tx_enemy_move(id);

	// set move to order

	return set_move_to(id, enemyVehicle->x, enemyVehicle->y);

}

void setDefendOrder(int id, int x, int y)
{
	if (combatOrders.find(id) == combatOrders.end())
	{
		combatOrders[id] = {};
		combatOrders[id].defendX = x;
		combatOrders[id].defendY = y;
	}

}

