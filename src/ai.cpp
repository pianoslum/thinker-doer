#include <float.h>
#include <math.h>
#include <vector>
#include <unordered_set>
#include <map>
#include <unordered_map>
#include "ai.h"
#include "wtp.h"
#include "game.h"
#include "aiProduction.h"
#include "aiTerraforming.h"

// global variables

// Controls which faction uses WtP algorithm.
// for debugging
// -1 : all factions

int wtpAIFactionId = -1;

// global variables for faction upkeep

int aiFactionId;

std::vector<int> baseIds;
std::set<int> presenceRegions;
std::vector<MAP_INFO> baseLocations;
std::unordered_map<MAP *, int> mapBases;
std::map<int, std::vector<int>> regionBaseGroups;
std::map<int, BASE_STRATEGY> baseStrategies;
std::vector<int> combatVehicleIds;
std::vector<int> outsideCombatVehicleIds;
std::vector<int> prototypes;
std::vector<int> colonyVehicleIds;
std::vector<int> formerVehicleIds;

/*
AI strategy.
*/
void aiStrategy(int id)
{
	// set faction

	aiFactionId = id;

	// no natives

	if (id == 0)
		return;

	// use WTP algorithm for selected faction only

	if (wtpAIFactionId != -1 && aiFactionId != wtpAIFactionId)
		return;

	debug("aiStrategy: aiFactionId=%d\n", aiFactionId);

	// populate shared strategy lists

	populateSharedLists();

	// prepare production

	aiProductionStrategy();

	// prepare former orders

	aiTerraformingStrategy();

	// prepare combat orders

	aiCombatStrategy();

}

void populateSharedLists()
{
	// clear lists

	baseIds.clear();
	presenceRegions.clear();
	baseLocations.clear();
	mapBases.clear();
	regionBaseGroups.clear();
	baseStrategies.clear();
	combatVehicleIds.clear();
	outsideCombatVehicleIds.clear();
	prototypes.clear();
	colonyVehicleIds.clear();
	formerVehicleIds.clear();

	// populate bases

	debug("baseStrategies\n");

	for (int id = 0; id < *total_num_bases; id++)
	{
		BASE *base = &(tx_bases[id]);

		// exclude not own bases

		if (base->faction_id != aiFactionId)
			continue;

		// add base

		baseIds.push_back(id);

		// add base location

		MAP *baseMapTile = getBaseMapTile(id);
		presenceRegions.insert(baseMapTile->region);
		baseLocations.push_back({base->x, base->y, baseMapTile});
		mapBases[baseMapTile] = id;

		// add base strategy

		baseStrategies[id] = {};
		baseStrategies[id].base = base;

		debug("\n[%3d] %-25s\n", id, baseStrategies[id].base->name);

		// add base regions

		std::set<int> baseConnectedRegions = getBaseConnectedRegions(id);

		for (int region : baseConnectedRegions)
		{
			if (regionBaseGroups.find(region) == regionBaseGroups.end())
			{
				regionBaseGroups[region] = std::vector<int>();
			}

			regionBaseGroups[region].push_back(id);

		}

	}

	debug("\n");

	// populate vehicles

	for (int id = 0; id < *total_num_vehicles; id++)
	{
		VEH *vehicle = &(tx_vehicles[id]);

		// store all vehicle current id in pad_0 field

		vehicle->pad_0 = id;

		// further process only own vehicles

		if (vehicle->faction_id != aiFactionId)
			continue;

		// combat vehicles

		if (isCombatVehicle(id))
		{
			// add vehicle

			combatVehicleIds.push_back(id);

			// find if vehicle is at base

			MAP *vehicleLocation = getMapTile(vehicle->x, vehicle->y);
			std::unordered_map<MAP *, int>::iterator mapBasesIterator = mapBases.find(vehicleLocation);

			if (mapBasesIterator == mapBases.end())
			{
				// add outside vehicle

				outsideCombatVehicleIds.push_back(id);

			}
			else
			{
				BASE_STRATEGY *baseStrategy = &(baseStrategies[mapBasesIterator->second]);

				// add to garrison

				baseStrategy->garrison.push_back(id);

				// add to native protection

				double nativeProtection = calculateNativeDamageDefense(id) / 10.0;

				if (vehicle_has_ability(vehicle, ABL_TRANCE))
				{
					nativeProtection *= (1 + (double)tx_basic->combat_bonus_trance_vs_psi / 100.0);
				}

				baseStrategy->nativeProtection += nativeProtection;

			}

		}
		else if (isVehicleColony(id))
		{
			colonyVehicleIds.push_back(id);
		}
		else if (isVehicleFormer(id))
		{
			formerVehicleIds.push_back(id);
		}

	}

	// populate prototypes

    for (int i = 0; i < 128; i++)
	{
        int id = (i < 64 ? i : (*active_faction - 1) * 64 + i);

        UNIT *unit = &tx_units[id];

		// skip not enabled

		if (id < 64 && !has_tech(*active_faction, unit->preq_tech))
			continue;

        // skip empty

        if (strlen(unit->name) == 0)
			continue;

		// add prototype

		prototypes.push_back(id);

	}

}

VEH *getVehicleByAIId(int aiId)
{
	// check if ID didn't change

	VEH *oldVehicle = &(tx_vehicles[aiId]);

	if (oldVehicle->pad_0 == aiId)
		return oldVehicle;

	// otherwise, scan all vehicles

	for (int id = 0; id < *total_num_vehicles; id++)
	{
		VEH *vehicle = &(tx_vehicles[id]);

		if (vehicle->pad_0 == aiId)
			return vehicle;

	}

	return NULL;

}

bool isreachable(int id, int x, int y)
{
	VEH *vehicle = &(tx_vehicles[id]);
	int triad = veh_triad(id);
	MAP *vehicleLocation = getMapTile(vehicle->x, vehicle->y);
	MAP *destinationLocation = getMapTile(x, y);

	return (triad == TRIAD_AIR || getConnectedRegion(vehicleLocation->region) == getConnectedRegion(destinationLocation->region));

}

int getNearestOwnBaseRange(int region, int x, int y)
{
	// iterate over own bases

	int nearestOwnBaseRange = 9999;

	for (int baseId : baseIds)
	{
		BASE *base = &(tx_bases[baseId]);
		MAP *baseMapTile = getBaseMapTile(baseId);

		// matching region only

		if (region != -1 && baseMapTile->region != region)
			continue;

		// get range

		int range = map_range(x, y, base->x, base->y);

		// update best value

		nearestOwnBaseRange = min(nearestOwnBaseRange, range);

	}

	return nearestOwnBaseRange;

}

