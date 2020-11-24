#ifndef __AI_H__
#define __AI_H__

#include <float.h>
#include <map>
#include "main.h"
#include "game.h"
#include "move.h"
#include "wtp.h"
#include "aiTerraforming.h"
#include "aiCombat.h"

enum THREAT_TYPE
{
	THREAT_INFANTRY = 10,
	THREAT_INFANTRY_ECM = 11,
	THREAT_INFANTRY_AAA = 12,
	THREAT_INFANTRY_ECM_AAA = 13,
	THREAT_MOBILE = 20,
	THREAT_SHIP = 30,
	THREAT_AIR = 40,
	THREAT_PSI_LAND = 50,
	THREAT_PSI_SEA = 60,
	THREAT_PSI_AIR = 70,
};

enum MILITARY_TYPE
{
	MILITARY_ARTILLERY = 0,
	MILITARY_DEFENDER = 10,
	MILITARY_DEFENDER_ECM = 11,
	MILITARY_DEFENDER_AAA = 12,
	MILITARY_MOBILE = 20,
	MILITARY_SHIP = 30,
	MILITARY_AIR_BOMBER = 40,
	MILITARY_AIR_INTERCEPTOR = 41,
	MILITARY_DEFENDER_TRANCE = 51,
	MILITARY_MOBILE_EMPATH = 52,
	MILITARY_SEA_EMPATH = 53,
	MILITARY_AIR_EMPATH = 54,
};

struct FACTION_INFO
{
	double offenseMultiplier;
	double defenseMultiplier;
	double fanaticBonusMultiplier;
	double threatKoefficient;
};

struct BASE_STRATEGY
{
	BASE *base;
	std::vector<int> garrison;
	double nativeProtection;
	double nativeThreat;
	double nativeDefenseProductionDemand;
	int unpopulatedTileCount;
	int unpopulatedTileRangeSum;
	double averageUnpopulatedTileRange;
};

extern int wtpAIFactionId;
extern int aiFactionId;

extern std::vector<int> baseIds;
extern std::set<int> presenceRegions;
extern std::unordered_map<MAP *, int> mapBases;
extern std::map<int, std::vector<int>> regionBaseGroups;
extern std::map<int, BASE_STRATEGY> baseStrategies;
extern std::vector<int> combatVehicleIds;
extern std::vector<int> outsideCombatVehicleIds;
extern std::vector<int> prototypes;
extern std::vector<int> colonyVehicleIds;
extern std::vector<int> formerVehicleIds;

void aiStrategy(int id);
void populateSharedLists();
VEH *getVehicleByAIId(int aiId);
bool isreachable(int id, int x, int y);
double estimateVehicleBaseLandNativeProtection(int factionId, int vehicleId);
int getNearestOwnBaseRange(int region, int x, int y);

#endif // __AI_H__

