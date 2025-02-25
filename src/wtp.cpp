#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <vector>
#include "wtp.h"
#include "main.h"
#include "patch.h"
#include "engine.h"

// game setup info

int initialColonyCount[8];

// global variables for all factions

FACTION_EXTRA factionExtras[8];
BASE_EXTRA baseExtras[BASES];

/*
Combat calculation placeholder.
All custom combat calculation goes here.
*/
HOOK_API int read_basic_rules()
{
    // run original code

    int value = tx_read_basic_rules();

    // update rules

    if (value == 0)
    {
        if (conf.tube_movement_rate_multiplier > 0)
        {
            // set road movement cost
            conf.road_movement_cost = conf.tube_movement_rate_multiplier;
            // set tube movement rate
            tx_basic->mov_rate_along_roads *= conf.tube_movement_rate_multiplier;

        }

    }

    return value;

}

/*
Combat calculation placeholder.
All custom combat calculation goes here.
*/
HOOK_API void battle_compute(int attackerVehicleId, int defenderVehicleId, int attackerStrengthPointer, int defenderStrengthPointer, int flags)
{
    debug
    (
        "battle_compute(attackerVehicleId=%d, defenderVehicleId=%d, attackerStrengthPointer=%d, defenderStrengthPointer=%d, flags=%d)\n",
        attackerVehicleId,
        defenderVehicleId,
        attackerStrengthPointer,
        defenderStrengthPointer,
        flags
    )
    ;

	// get attacker/defender vehicle

	VEH *attackerVehicle = &tx_vehicles[attackerVehicleId];
	VEH *defenderVehicle = &tx_vehicles[defenderVehicleId];

	// get combat map tile

	int combatLocationX = defenderVehicle->x;
	int combatLocationY = defenderVehicle->y;
	MAP *combatMapTile = getVehicleMapTile(defenderVehicleId);

    // run original function

    tx_battle_compute(attackerVehicleId, defenderVehicleId, attackerStrengthPointer, defenderStrengthPointer, flags);

    // PLANET combat bonus on defense

	int LABEL_OFFSET_PLANET = 0x271;

    if (conf.planet_combat_bonus_on_defense)
    {
        // PLANET bonus applies to psi combat only
        // psi combat is triggered by either attacker negative weapon value or defender negative armor value

        if (tx_weapon[tx_units[attackerVehicle->proto_id].weapon_type].offense_value < 0 || tx_defense[tx_units[defenderVehicle->proto_id].armor_type].defense_value < 0)
        {
            // get defender faction id

            int defenderFactionId = tx_vehicles[defenderVehicleId].faction_id;

			// PLANET bonus applies to normal factions only

            if (defenderFactionId > 0)
            {
                // get defender planet rating at the beginning of the turn (not pending)

                int defenderPlanetRating = tx_factions[defenderFactionId].SE_planet;

                if (defenderPlanetRating != 0)
                {
                    // calculate defender psi combat bonus

                    int defenderPsiCombatBonus = tx_basic->combat_psi_bonus_per_PLANET * defenderPlanetRating;

                    // modify defender strength

                    *(int *)defenderStrengthPointer = (int)round((double)(*(int *)defenderStrengthPointer) * (1.0 + (double)defenderPsiCombatBonus / 100.0));

                    // add effect description

                    if (*tx_battle_compute_defender_effect_count < 4)
                    {
                        strcpy((*tx_battle_compute_defender_effect_labels)[*tx_battle_compute_defender_effect_count], *(*tx_labels + LABEL_OFFSET_PLANET));
                        (*tx_battle_compute_defender_effect_values)[*tx_battle_compute_defender_effect_count] = defenderPsiCombatBonus;

                        (*tx_battle_compute_defender_effect_count)++;

                    }

                }

            }

        }

    }

    // territory combat bonus

	const char *LABEL_TERRITORY = "Territory";

    if (conf.combat_bonus_territory != 0)
    {
        // apply territory defense bonus for non natives combat only

        if (attackerVehicle->faction_id > 0 && defenderVehicle->faction_id > 0)
        {
            // attacker gets territory combat bonus for battling in own territory

            if (combatMapTile->owner == attackerVehicle->faction_id)
            {
                // modify attacker strength

                *(int *)attackerStrengthPointer = (int)round((double)(*(int *)attackerStrengthPointer) * (1.0 + (double)conf.combat_bonus_territory / 100.0));

                // add effect description

                if (*tx_battle_compute_attacker_effect_count < 4)
                {
                    strcpy((*tx_battle_compute_attacker_effect_labels)[*tx_battle_compute_attacker_effect_count], LABEL_TERRITORY);
                    (*tx_battle_compute_attacker_effect_values)[*tx_battle_compute_attacker_effect_count] = conf.combat_bonus_territory;

                    (*tx_battle_compute_attacker_effect_count)++;

                }

            }

            // defender gets territory combat bonus for battling in own territory

            if (combatMapTile->owner == defenderVehicle->faction_id)
            {
                // modify defender strength

                *(int *)defenderStrengthPointer = (int)round((double)(*(int *)defenderStrengthPointer) * (1.0 + (double)conf.combat_bonus_territory / 100.0));

                // add effect description

                if (*tx_battle_compute_defender_effect_count < 4)
                {
                    strcpy((*tx_battle_compute_defender_effect_labels)[*tx_battle_compute_defender_effect_count], LABEL_TERRITORY);
                    (*tx_battle_compute_defender_effect_values)[*tx_battle_compute_defender_effect_count] = conf.combat_bonus_territory;

                    (*tx_battle_compute_defender_effect_count)++;

                }

            }

        }

    }

    // sensor

    int LABEL_OFFSET_SENSOR = 0x265;

    // sensor bonus on attack

    if (conf.combat_bonus_sensor_offense)
	{
		if
		(
			// either combat is on land or sensor bonus is applicable on ocean
			(!is_ocean(combatMapTile))
			||
			(is_ocean(combatMapTile) && conf.combat_bonus_sensor_ocean)
		)
		{
			// attacker is in range of friendly sensor

			if (isInRangeOfFriendlySensor(combatLocationX, combatLocationY, 2, attackerVehicle->faction_id))
			{
				// modify attacker strength

				*(int *)attackerStrengthPointer = (int)round((double)(*(int *)attackerStrengthPointer) * (1.0 + (double)tx_basic->combat_defend_sensor / 100.0));

				// add effect description

				if (*tx_battle_compute_defender_effect_count < 4)
				{
					strcpy((*tx_battle_compute_attacker_effect_labels)[*tx_battle_compute_attacker_effect_count], *(*tx_labels + LABEL_OFFSET_SENSOR));
					(*tx_battle_compute_attacker_effect_values)[*tx_battle_compute_attacker_effect_count] = tx_basic->combat_defend_sensor;

					(*tx_battle_compute_attacker_effect_count)++;

				}

			}

		}

	}

    // sensor bonus on defense

	{
		if
		(
			// ocean combat only
			// land defence is already covered by vanilla
			(is_ocean(combatMapTile) && conf.combat_bonus_sensor_ocean)
		)
		{
			// defender is in range of friendly sensor

			if (isInRangeOfFriendlySensor(combatLocationX, combatLocationY, 2, defenderVehicle->faction_id))
			{
				// modify defender strength

				*(int *)defenderStrengthPointer = (int)round((double)(*(int *)defenderStrengthPointer) * (1.0 + (double)tx_basic->combat_defend_sensor / 100.0));

				// add effect description

				if (*tx_battle_compute_defender_effect_count < 4)
				{
					strcpy((*tx_battle_compute_defender_effect_labels)[*tx_battle_compute_defender_effect_count], *(*tx_labels + LABEL_OFFSET_SENSOR));
					(*tx_battle_compute_defender_effect_values)[*tx_battle_compute_defender_effect_count] = tx_basic->combat_defend_sensor;

					(*tx_battle_compute_defender_effect_count)++;

				}

			}

		}

	}

    // TODO - remove. This code doesn't work well with Hasty and Gas modifiers.
//    // adjust summary lines to the bottom
//
//    while (*tx_battle_compute_attacker_effect_count < 4)
//    {
//        (*tx_battle_compute_attacker_effect_labels)[*tx_battle_compute_attacker_effect_count][0] = '\x0';
//        (*tx_battle_compute_attacker_effect_values)[*tx_battle_compute_attacker_effect_count] = 0;
//
//        (*tx_battle_compute_attacker_effect_count)++;
//
//    }
//
//    while (*tx_battle_compute_defender_effect_count < 4)
//    {
//        (*tx_battle_compute_defender_effect_labels)[*tx_battle_compute_defender_effect_count][0] = '\x0';
//        (*tx_battle_compute_defender_effect_values)[*tx_battle_compute_defender_effect_count] = 0;
//
//        (*tx_battle_compute_defender_effect_count)++;
//
//    }
//
}

/*
Composes combat effect value percentage.
Overwrites zero with empty string.
*/
HOOK_API void battle_compute_compose_value_percentage(int output_string_pointer, int input_string_pointer)
{
    debug
    (
        "battle_compute_compose_value_percentage:input(output_string=%s, input_string=%s)\n",
        (char *)output_string_pointer,
        (char *)input_string_pointer
    )
    ;

    // call original function

    tx_strcat(output_string_pointer, input_string_pointer);

    debug
    (
        "battle_compute_compose_value_percentage:output(output_string=%s, input_string=%s)\n",
        (char *)output_string_pointer,
        (char *)input_string_pointer
    )
    ;

    // replace zero with empty string

    if (strcmp((char *)output_string_pointer, " +0%") == 0)
    {
        ((char *)output_string_pointer)[0] = '\x0';

    }

    debug
    (
        "battle_compute_compose_value_percentage:corrected(output_string=%s, input_string=%s)\n",
        (char *)output_string_pointer,
        (char *)input_string_pointer
    )
    ;

}

/*
Prototype cost calculation.

1. Calculate module and weapon/armor reactor modified costs.
module reactor modified cost = item cost * (Fission reactor value / 100)
weapon/armor reactor modified cost = item cost * (reactor value / 100)

2. Select primary and secondary item.
primary item = the one with higher reactor modified cost
secondary item = the one with lower reactor modified cost

3. Calculate primary item cost and secondary item shifted cost
primary item cost = item cost * (corresponding reactor value / 100)
secondary item shifted cost = (item cost - 1) * (corresponding reactor value / 100)

4. Calculate unit base cost.
unit base cost = [primary item cost + secondary item shifted cost / 2] * chassis cost / 2

5. Round.

6. Multiply by ability cost factor.
ability bytes 0-3 is cost factor

7. Add ability flat cost
ability bytes 4-7 is flat cost

8. Round.

*/
HOOK_API int proto_cost(int chassis_id, int weapon_id, int armor_id, int abilities, int reactor_level)
{
    // calculate reactor cost factors

    double fission_reactor_cost_factor = (double)conf.reactor_cost_factors[0] / 100.0;
    double reactor_cost_factor = (double)conf.reactor_cost_factors[reactor_level - 1] / 100.0;

    // get component values

    int weapon_offence_value = tx_weapon[weapon_id].offense_value;
    int armor_defense_value = tx_defense[armor_id].defense_value;
    int chassis_speed = tx_chassis[chassis_id].speed;

    // determine whether we have module or weapon

    bool module = (tx_weapon[weapon_id].mode >= WMODE_TRANSPORT);

    // get component costs

    int weapon_cost = tx_weapon[weapon_id].cost;
    int armor_cost = tx_defense[armor_id].cost;
    int chassis_cost = tx_chassis[chassis_id].cost;

    // calculate items reactor modified costs

    double weapon_reactor_modified_cost = (double)weapon_cost * (module ? fission_reactor_cost_factor : reactor_cost_factor);
    double armor_reactor_modified_cost = (double)armor_cost * reactor_cost_factor;

    // select primary item and secondary item shifted costs

    double primary_item_cost;
    double secondary_item_shifted_cost;

    if (weapon_reactor_modified_cost >= armor_reactor_modified_cost)
	{
		primary_item_cost = weapon_reactor_modified_cost;
		secondary_item_shifted_cost = ((double)armor_cost - 1) * reactor_cost_factor;
	}
	else
	{
		primary_item_cost = armor_reactor_modified_cost;
		secondary_item_shifted_cost = ((double)weapon_cost - 1) * (module ? fission_reactor_cost_factor : reactor_cost_factor);
	}

    // set minimal cost to reactor level (this is checked in some other places so we should do this here to avoid any conflicts)

    int minimal_cost = reactor_level;

    // calculate base unit cost without abilities

    double base_cost = (primary_item_cost + secondary_item_shifted_cost / 2) * chassis_cost / 2;

    // get abilities cost modifications

    int abilities_cost_factor = 0;
    int abilities_cost_addition = 0;

    for (int ability_id = 0; ability_id < 32; ability_id++)
    {
        if (((abilities >> ability_id) & 0x1) == 0x1)
        {
            int ability_cost = tx_ability[ability_id].cost;

            switch (ability_cost)
            {
            	// increased with weapon/armor ratio
			case -1:
				abilities_cost_factor += min(2, max(0, weapon_offence_value / armor_defense_value));
				break;

            	// increased with weapon
			case -2:
				abilities_cost_factor += weapon_offence_value;
				break;

            	// increased with armor
			case -3:
				abilities_cost_factor += armor_defense_value;
				break;

            	// increased with speed
			case -4:
				abilities_cost_factor += chassis_speed;
				break;

            	// increased with weapon + armor
			case -5:
				abilities_cost_factor += weapon_offence_value + armor_defense_value;
				break;

            	// increased with weapon + speed
			case -6:
				abilities_cost_factor += weapon_offence_value + chassis_speed;
				break;

            	// increased with armor + speed
			case -7:
				abilities_cost_factor += armor_defense_value + chassis_speed;
				break;

				// positive values
			default:

				// take ability cost factor bits: 0-3

				int ability_proportional_value = (ability_cost & 0xF);

				if (ability_proportional_value > 0)
				{
					abilities_cost_factor += ability_proportional_value;

				}

				// take ability cost addition bits: 4-7

				int ability_flat_value = ((ability_cost >> 4) & 0xF);

				if (ability_flat_value > 0)
				{
					abilities_cost_addition += ability_flat_value;

				}

            }

            // special case: cost increased for land units

            if ((tx_ability[ability_id].flags & AFLAG_COST_INC_LAND_UNIT) && tx_chassis[chassis_id].triad == TRIAD_LAND)
			{
				abilities_cost_factor += 1;
			}

        }

    }

    // calculate final cost

    int cost =
        max
        (
            // minimal cost
            minimal_cost,
            (int)
            round
            (
				// base cost
				base_cost
                *
                // abilities cost factor
                (1.0 + 0.25 * (double)abilities_cost_factor)
            )
            +
            // abilities flat cost
            abilities_cost_addition
        )
    ;

    return cost;

}

HOOK_API int upgrade_cost(int faction_id, int new_unit_id, int old_unit_id)
{
    // get old unit cost

    int old_unit_cost = tx_units[old_unit_id].cost;

    // get new unit cost

    int new_unit_cost = tx_units[new_unit_id].cost;

    // double new unit cost if not prototyped

    if ((tx_units[new_unit_id].unit_flags & 0x4) == 0)
    {
        new_unit_cost *= 2;
    }

    // calculate cost difference

    int cost_difference = new_unit_cost - old_unit_cost;

    // calculate base upgrade cost
    // x4 if positive
    // x2 if negative

    int upgrade_cost = cost_difference * (cost_difference >= 0 ? 4 : 2);

    // halve upgrade cost if positive and faction possesses The Nano Factory

    if (upgrade_cost > 0)
    {
        // get The Nano Factory base id

        int nano_factory_base_id = tx_secret_projects[0x16];

        // get The Nano Factory faction id

        if (nano_factory_base_id >= 0)
        {
            if (tx_bases[nano_factory_base_id].faction_id == faction_id)
            {
                upgrade_cost /= 2;

            }

        }

    }

    return upgrade_cost;

}

/*
Multipurpose combat roll function.
Determines which side wins this round. Returns 1 for attacker, 0 for defender.
*/
HOOK_API int combat_roll
(
    int attacker_strength,
    int defender_strength,
    int attacker_vehicle_offset,
    int defender_vehicle_offset,
    int attacker_initial_power,
    int defender_initial_power,
    int *attacker_won_last_round
)
{
    debug("combat_roll(attacker_strength=%d, defender_strength=%d, attacker_vehicle_offset=%d, defender_vehicle_offset=%d, attacker_initial_power=%d, defender_initial_power=%d, *attacker_won_last_round=%d)\n",
        attacker_strength,
        defender_strength,
        attacker_vehicle_offset,
        defender_vehicle_offset,
        attacker_initial_power,
        defender_initial_power,
        *attacker_won_last_round
    )
    ;

    // normalize strength values

    if (attacker_strength < 1) attacker_strength = 1;
    if (defender_strength < 1) defender_strength = 1;

    // calculate outcome

    int attacker_wins;

    if (!conf.alternative_combat_mechanics)
    {
        attacker_wins =
            standard_combat_mechanics_combat_roll
            (
                attacker_strength,
                defender_strength
            )
        ;

    }
    else
    {
        attacker_wins =
            alternative_combat_mechanics_combat_roll
            (
                attacker_strength,
                defender_strength,
                attacker_vehicle_offset,
                defender_vehicle_offset,
                attacker_initial_power,
                defender_initial_power,
                attacker_won_last_round
            )
        ;

    }

    // return outcome

    return attacker_wins;

}

/*
Standard combat mechanics.
Determines which side wins this round. Returns 1 for attacker, 0 for defender.
*/
int standard_combat_mechanics_combat_roll
(
    int attacker_strength,
    int defender_strength
)
{
    debug("standard_combat_mechanics_combat_roll(attacker_strength=%d, defender_strength=%d)\n",
        attacker_strength,
        defender_strength
    )
    ;

    // generate random roll

    int random_roll = random(attacker_strength + defender_strength);
    debug("\trandom_roll=%d\n", random_roll);

    // calculate outcome

    int attacker_wins = (random_roll < attacker_strength ? 1 : 0);
    debug("\tattacker_wins=%d\n", attacker_wins);

    return attacker_wins;

}

/*
Alternative combat mechanics.
Determines which side wins this round. Returns 1 for attacker, 0 for defender.
*/
int alternative_combat_mechanics_combat_roll
(
    int attacker_strength,
    int defender_strength,
    int attacker_vehicle_offset,
    int defender_vehicle_offset,
    int attacker_initial_power,
    int defender_initial_power,
    int *attacker_won_last_round
)
{
    debug("alternative_combat_mechanics_combat_roll(attacker_strength=%d, defender_strength=%d, attacker_initial_power=%d, defender_initial_power=%d, *attacker_won_last_round=%d)\n",
        attacker_strength,
        defender_strength,
        attacker_initial_power,
        defender_initial_power,
        *attacker_won_last_round
    )
    ;

    // calculate probabilities

    double p, q, pA, qA, pD, qD;

    alternative_combat_mechanics_probabilities
    (
        attacker_strength,
        defender_strength,
        &p, &q, &pA, &qA, &pD, &qD
    )
    ;

    // determine whether we are on first round

    VEH attacker_vehicle = tx_vehicles[attacker_vehicle_offset / 0x34];
    VEH defender_vehicle = tx_vehicles[defender_vehicle_offset / 0x34];

    UNIT attacker_unit = tx_units[attacker_vehicle.proto_id];
    UNIT defender_unit = tx_units[defender_vehicle.proto_id];

    int attacker_power = attacker_unit.reactor_type * 0xA - attacker_vehicle.damage_taken;
    int defender_power = defender_unit.reactor_type * 0xA - defender_vehicle.damage_taken;

    bool first_round = (attacker_power == attacker_initial_power) && (defender_power == defender_initial_power);
    debug("\tfirst_round=%d\n", (first_round ? 1 : 0));

    // determine effective p

    double effective_p = (first_round ? p : (*attacker_won_last_round == 1 ? pA : pD));
    debug("\teffective_p=%f\n", effective_p);

    // generate random roll

    double random_roll = (double)rand() / (double)RAND_MAX;
    debug("\trandom_roll=%f\n", random_roll);

    // calculate outcome

    int attacker_wins = (random_roll < effective_p ? 1 : 0);
    debug("\tattacker_wins=%d\n", attacker_wins);

    // set round winner

    *attacker_won_last_round = attacker_wins;

    return attacker_wins;

}

/*
Alternative combat mechanics.
Calculates probabilities.
*/
void alternative_combat_mechanics_probabilities
(
    int attacker_strength,
    int defender_strength,
    double *p, double *q, double *pA, double *qA, double *pD, double *qD
)
{
    // determine first round probability

    *p = (double)attacker_strength / ((double)attacker_strength + (double)defender_strength);
    *q = 1 - *p;

    // determine following rounds probabilities

    *qA = *q / conf.alternative_combat_mechanics_loss_divider;
    *pA = 1 - *qA;
    *pD = *p / conf.alternative_combat_mechanics_loss_divider;
    *qD = 1 - *pD;

}

/*
Calculates odds.
*/
HOOK_API void calculate_odds
(
    int attacker_vehicle_offset,
    int defender_vehicle_offset,
    int attacker_strength,
    int defender_strength,
    int *attacker_odd,
    int *defender_odd
)
{
    debug("calculate_odds(attacker_vehicle_offset=%d, defender_vehicle_offset=%d, attacker_strength=%d, defender_strength=%d)\n",
        attacker_vehicle_offset,
        defender_vehicle_offset,
        attacker_strength,
        defender_strength
    )
    ;

    // get attacker and defender vehicles

    VEH attacker_vehicle = tx_vehicles[attacker_vehicle_offset / sizeof(VEH)];
    VEH defender_vehicle = tx_vehicles[defender_vehicle_offset / sizeof(VEH)];

    // get attacker and defender units

    UNIT attacker_unit = tx_units[attacker_vehicle.proto_id];
    UNIT defender_unit = tx_units[defender_vehicle.proto_id];

    // calculate attacker and defender power
    // artifact gets 1 HP regardless of reactor

    int attacker_power = (attacker_unit.unit_plan == PLAN_ALIEN_ARTIFACT ? 1 : attacker_unit.reactor_type * 10 - attacker_vehicle.damage_taken);
    int defender_power = (defender_unit.unit_plan == PLAN_ALIEN_ARTIFACT ? 1 : defender_unit.reactor_type * 10 - defender_vehicle.damage_taken);

    // determine if we are ignoring reactor power

    bool ignore_reactor_power =
        conf.ignore_reactor_power_in_combat
        ||
        tx_weapon[attacker_unit.weapon_type].offense_value < 0
        ||
        tx_defense[defender_unit.armor_type].defense_value < 0
    ;

    // calculate firepower

    int attacker_fp =
        (ignore_reactor_power ? defender_unit.reactor_type : 1)
    ;
    int defender_fp =
        (ignore_reactor_power ? attacker_unit.reactor_type : 1)
    ;

    // calculate HP

    int attacker_hp = (int)ceil((double)attacker_power / defender_fp);
    int defender_hp = (int)ceil((double)defender_power / attacker_fp);

    // calculate attacker winning probability

    double attacker_winning_probability;

    if (defender_hp <= 0)
    {
        attacker_winning_probability = 1.0;

    }
    else if (attacker_hp <= 0)
    {
        attacker_winning_probability = 0.0;

    }
    else
    {
        if (!conf.alternative_combat_mechanics)
        {
            attacker_winning_probability =
                standard_combat_mechanics_calculate_attacker_winning_probability
                (
                    attacker_strength,
                    defender_strength,
                    attacker_hp,
                    defender_hp
                )
            ;

        }
        else
        {
            attacker_winning_probability =
                alternative_combat_mechanics_calculate_attacker_winning_probability
                (
                    attacker_strength,
                    defender_strength,
                    attacker_hp,
                    defender_hp
                )
            ;

        }

    }

    /*
    // calculate odds

    double odds = 1.0 / (1.0 / attacker_winning_probability - 1.0);

    bool attacker_advantage = (odds >= 1.0);
    double normalized_odds = (attacker_advantage ? odds : 1.0 / odds);

    // find good odds ratio

    int best_numerator = 0;
    int best_denominator = 0;
    double best_difference = -1.0;

    for (int denominator = 0x1; denominator <= 0xA; denominator++)
    {
        int numerator = (int)round(normalized_odds * denominator);
        double odds_ratio = (double)numerator / (double)denominator;
        double difference = abs(odds_ratio - normalized_odds);

        if (best_difference == -1 || difference < best_difference)
        {
            best_difference = difference;
            best_numerator = numerator;
            best_denominator = denominator;

        }

    }

    // set odds

    if (attacker_advantage)
    {
        *attacker_odd = best_numerator;
        *defender_odd = best_denominator;

    }
    else
    {
        *attacker_odd = best_denominator;
        *defender_odd = best_numerator;

    }
    */

    // set attacker odd to attacker winning probability percentage
    // set defender odd to 1 to disable fraction simplification

    *attacker_odd = (int)round(attacker_winning_probability * 100.0);
    *defender_odd = 1;

}

/*
Standard combat mechanics.
Calculates attacker winning probability.
*/
double standard_combat_mechanics_calculate_attacker_winning_probability
(
    int attacker_strength,
    int defender_strength,
    int attacker_hp,
    int defender_hp
)
{
    debug("standard_combat_mechanics_calculate_attacker_winning_probability(attacker_strength=%d, defender_strength=%d, attacker_hp=%d, defender_hp=%d)\n",
        attacker_strength,
        defender_strength,
        attacker_hp,
        defender_hp
    )
    ;

    // determine round probability

    double p = (double)attacker_strength / ((double)attacker_strength + (double)defender_strength);
    double q = 1 - p;

    // calculate attacker winning probability

    double attacker_winning_probability = 0.0;
    double p1 = pow(p, defender_hp);

    for (int alp = 0; alp < attacker_hp; alp++)
    {
        double q1 = pow(q, alp);

        double c = binomial_koefficient(defender_hp - 1 + alp, alp);

        attacker_winning_probability += p1 * q1 * c;

    }

    debug("p(round)=%f, p(battle)=%f\n", p, attacker_winning_probability);

    return attacker_winning_probability;

}

double binomial_koefficient(int n, int k)
{
    double c = 1.0;

    for (int i = 0; i < k; i++)
    {
        c *= (double)(n - i) / (double)(k - i);

    }

    return c;

}

/*
Alternative combat mechanics.
Calculates attacker winning probability.
*/
double alternative_combat_mechanics_calculate_attacker_winning_probability
(
    int attacker_strength,
    int defender_strength,
    int attacker_hp,
    int defender_hp
)
{
    debug("alternative_combat_mechanics_calculate_attacker_winning_probability(attacker_strength=%d, defender_strength=%d, attacker_hp=%d, defender_hp=%d)\n",
        attacker_strength,
        defender_strength,
        attacker_hp,
        defender_hp
    )
    ;

    double attacker_winning_probability;

    // cannot calculate too long tree for HP > 10
    // return 0.5 instead
    if (attacker_hp > 10 || defender_hp > 10)
    {
        attacker_winning_probability = 0.5;

    }
    else if (attacker_hp <= 0 && defender_hp <= 0)
    {
        attacker_winning_probability = 0.5;

    }
    else if (defender_hp <= 0)
    {
        attacker_winning_probability = 1.0;

    }
    else if (attacker_hp <= 0)
    {
        attacker_winning_probability = 0.0;

    }
    else
    {
        // determine first round probability

        double p = (double)attacker_strength / ((double)attacker_strength + (double)defender_strength);
        double q = 1 - p;

        // determine following rounds probabilities

        double qA = q / conf.alternative_combat_mechanics_loss_divider;
        double pA = 1 - qA;
        double pD = p / conf.alternative_combat_mechanics_loss_divider;
        double qD = 1 - pD;

        debug("p=%f, q=%f, pA=%f, qA=%f, pD=%f, qD=%f\n", p, q, pA, qA, pD, qD);

        // calculate attacker winning probability

        attacker_winning_probability =
            // attacker wins first round
            p
            *
            alternative_combat_mechanics_calculate_attacker_winning_probability_following_rounds
            (
                true,
                pA, qA, pD, qD,
                attacker_hp,
                defender_hp - 1
            )
            +
            // defender wins first round
            q
            *
            alternative_combat_mechanics_calculate_attacker_winning_probability_following_rounds
            (
                false,
                pA, qA, pD, qD,
                attacker_hp - 1,
                defender_hp
            )
        ;

    }

    debug("attacker_winning_probability=%f\n", attacker_winning_probability);

    return attacker_winning_probability;

}

double alternative_combat_mechanics_calculate_attacker_winning_probability_following_rounds
(
    bool attacker_won,
    double pA, double qA, double pD, double qD,
    int attacker_hp,
    int defender_hp
)
{
    double attacker_winning_probability;

    if (attacker_hp <= 0 && defender_hp <= 0)
    {
        attacker_winning_probability = 0.5;

    }
    else if (defender_hp <= 0)
    {
        attacker_winning_probability = 1.0;

    }
    else if (attacker_hp <= 0)
    {
        attacker_winning_probability = 0.0;

    }
    else
    {
        attacker_winning_probability =
            // attacker wins round
            (attacker_won ? pA : pD)
            *
            alternative_combat_mechanics_calculate_attacker_winning_probability_following_rounds
            (
                true,
                pA, qA, pD, qD,
                attacker_hp,
                defender_hp - 1
            )
            +
            // defender wins round
            (attacker_won ? qA : qD)
            *
            alternative_combat_mechanics_calculate_attacker_winning_probability_following_rounds
            (
                false,
                pA, qA, pD, qD,
                attacker_hp - 1,
                defender_hp
            )
        ;

    }

    return attacker_winning_probability;

}

/*
For sea squares pretend they are on the same continent as closest reachable coastal base
if such base is also closer than any other sea base in the same sea.
*/
HOOK_API int base_find3(int x, int y, int unknown_1, int region, int unknown_2, int unknown_3)
{
    debug
    (
        "base_find3(x=%d, y=%d, region=%d)\n",
        x,
        y,
        region
    )
    ;

    bool closest_reachable_base_found = false;
    bool closest_reachable_base_is_coastal = false;
    int closest_reachable_base_distance = 9999;
    BASE *closest_reachable_base = NULL;
    MAP *closest_reachable_base_square = NULL;

    int saved_coastal_base_region = -1;

    // check if given square is at sea

    if (region >= 0x41)
    {
        // loop through bases

        for (int base_id = 0; base_id < *total_num_bases; base_id++)
        {
            // get base

            BASE *base = &tx_bases[base_id];

            // get base map square

            MAP *base_map_square = mapsq(base->x, base->y);

            // get base map square body id

            int base_map_square_region = base_map_square->region;

            // check if it is the same body
            if (base_map_square_region == region)
            {
                // calculate base distance

                int base_distance = map_distance(x, y, base->x, base->y);

                // improve distance if smaller

                if (!closest_reachable_base_found || base_distance < closest_reachable_base_distance)
                {
                    closest_reachable_base_found = true;
                    closest_reachable_base_is_coastal = false;
                    closest_reachable_base_distance = base_distance;
                    closest_reachable_base = base;
                    closest_reachable_base_square = base_map_square;

                }

            }
            // otherwise, check it it is a land base
            else if (base_map_square_region < 0x41)
            {
                // iterate over nearby base squares

                for (int dx = -2; dx <= 2; dx++)
                {
                    for (int dy = -(2 - abs(dx)); dy <= (2 - abs(dx)); dy += 2)
                    {
                        int near_x = base->x + dx;
                        int near_y = base->y + dy;

                        // exclude squares beyond the map

                        if (near_y < 0 || near_y >= *map_axis_y)
                            continue;

                        if (*map_toggle_flat && (near_x < 0 || near_x >= *map_axis_x))
                            continue;

                        // wrap x on cylinder map

                        if (!*map_toggle_flat && near_x < 0)
                        {
                            near_x += *map_axis_x;

                        }

                        if (!*map_toggle_flat && near_x >= *map_axis_x)
                        {
                            near_x -= *map_axis_x;

                        }

                        // get near map square

                        MAP *near_map_square = mapsq(near_x, near_y);

                        // get near map square body id

                        int near_map_square_region = near_map_square->region;

                        // check if it is a matching body id

                        if (near_map_square_region == region)
                        {
                            // calculate base distance

                            int base_distance = map_distance(x, y, base->x, base->y);

                            // improve distance if smaller

                            if (!closest_reachable_base_found || base_distance < closest_reachable_base_distance)
                            {
                                closest_reachable_base_found = true;
                                closest_reachable_base_is_coastal = true;
                                closest_reachable_base_distance = base_distance;
                                closest_reachable_base = base;
                                closest_reachable_base_square = base_map_square;

                            }

                        }

                    }

                }

            }

        }

        debug
        (
            "closest_reachable_base_found=%d, closest_reachable_base_is_coastal=%d, closest_reachable_base_distance=%d\n",
            closest_reachable_base_found,
            closest_reachable_base_is_coastal,
            closest_reachable_base_distance
        )
        ;

        // if closest coastal base is found - pretend it is placed on same body

        if (closest_reachable_base_found && closest_reachable_base_is_coastal)
        {
            saved_coastal_base_region = closest_reachable_base_square->region;

            debug
            (
                "Before emulation: closest_reachable_base->x=%d, closest_reachable_base->y=%d, closest_reachable_base_square->region=%d\n",
                closest_reachable_base->x,
                closest_reachable_base->y,
                closest_reachable_base_square->region
            )
            ;

            closest_reachable_base_square->region = region;

            debug
            (
                "After emulation: closest_reachable_base_square->region=%d\n",
                closest_reachable_base_square->region
            )
            ;

        }

    }

    // run original function

    int nearest_base_id = tx_base_find3(x, y, unknown_1, region, unknown_2, unknown_3);

    // revert coastal base map square body to original

    if (closest_reachable_base_found && closest_reachable_base_is_coastal)
    {
        debug
        (
            "Before restoration: closest_reachable_base_square->region=%d\n",
            closest_reachable_base_square->region
        )
        ;

        closest_reachable_base_square->region = saved_coastal_base_region;

        debug
        (
            "After restoration: closest_reachable_base_square->region=%d\n",
            closest_reachable_base_square->region
        )
        ;

    }

    // return nearest base faction id

    return nearest_base_id;

}

/*
Calculates map distance as in vanilla.
*/
int map_distance(int x1, int y1, int x2, int y2)
{
    int xd = abs(x1-x2);
    int yd = abs(y1-y2);
    if (!*map_toggle_flat && xd > *map_axis_x/2)
        xd = *map_axis_x - xd;

    return ((xd + yd) + abs(xd - yd) / 2) / 2;

}

/*
Verifies two locations are within the base radius of each other.
*/
bool isWithinBaseRadius(int x1, int y1, int x2, int y2)
{
	return map_distance(x1, y1, x2, y2) <= 2;

}

/*
Rolls artillery damage.
*/
HOOK_API int roll_artillery_damage(int attacker_strength, int defender_strength, int attacker_firepower)
{
    debug
    (
        "roll_artillery_damage(attacker_strength=%d, defender_strength=%d, attacker_firepower=%d)\n",
        attacker_strength,
        defender_strength,
        attacker_firepower
    )
    ;

    int artillery_damage = 0;

    // compare attacker and defender strength

    if (attacker_strength >= defender_strength)
    {
        // replicate vanilla algorithm

        int max_damage = attacker_strength / defender_strength;

        artillery_damage = random(max_damage + 1);

    }
    else
    {
        int random_roll = random(attacker_strength + defender_strength);

        artillery_damage = (random_roll < attacker_strength ? 1 : 0);

    }

    // multiply attacker strength by firepower

    artillery_damage *= attacker_firepower;

    debug
    (
        "artillery_damage=%d)\n",
        artillery_damage
    )
    ;

    return artillery_damage;

}

/*
nutrient yield calculation
*/
HOOK_API int mod_nutrient_yield(int factionId, int baseId, int x, int y, int tf)
{
	// call original function

    int value = crop_yield(factionId, baseId, x, y, tf);

    // get map tile

    MAP* tile = getMapTile(x, y);

    // bad tile - should not happen, though

    if (!tile)
		return value;

	// apply condenser and enricher fix if configured

	if (conf.condenser_and_enricher_do_not_multiply_nutrients)
	{
		// condenser does not multiply nutrients

		if (tile->items & TERRA_CONDENSER)
		{
			value = (value * 2 + 2) / 3;
		}

		// enricher does not multiply nutrients

		if (tile->items & TERRA_SOIL_ENR)
		{
			value = (value * 2 + 2) / 3;
		}

		// enricher adds 1 nutrient

		if (tile->items & TERRA_SOIL_ENR)
		{
			value++;
		}

	}

    return value;

}

/*
mineral yield calculation
*/
HOOK_API int mod_mineral_yield(int factionId, int baseId, int x, int y, int tf)
{
	// call original function

    int value = mineral_yield(factionId, baseId, x, y, tf);

    return value;

}

/*
energy yield calculation
*/
HOOK_API int mod_energy_yield(int factionId, int baseId, int x, int y, int tf)
{
	// call original function

    int value = energy_yield(factionId, baseId, x, y, tf);

    return value;

}

/*
SE accumulated resource adjustment
*/
HOOK_API int se_accumulated_resource_adjustment(int a1, int a2, int faction_id, int a4, int a5)
{
    // get faction

    Faction* faction = &tx_factions[faction_id];

    // get old ratings

    int SE_growth_pending_old = faction->SE_growth_pending;
    int SE_industry_pending_old = faction->SE_industry_pending;

    // execute original code

    // use modified function instead of original code
//	int returnValue = tx_set_se_on_dialog_close(a1, a2, faction_id, a4, a5);
    int returnValue = modifiedSocialCalc(a1, a2, faction_id, a4, a5);

    // get new ratings

    int SE_growth_pending_new = faction->SE_growth_pending;
    int SE_industry_pending_new = faction->SE_industry_pending;

    debug
    (
        "se_accumulated_resource_adjustment: faction_id=%d, SE_growth_pending: %+d -> %+d, SE_industry_pending: %+d -> %+d\n",
        faction_id,
        SE_growth_pending_old,
        SE_growth_pending_new,
        SE_industry_pending_old,
        SE_industry_pending_new
    )
    ;

    // adjust nutrients

    if (SE_growth_pending_new != SE_growth_pending_old)
    {
        int row_size_old = 10 - SE_growth_pending_old;
        int row_size_new = 10 - SE_growth_pending_new;

        double conversion_ratio = (double)row_size_new / (double)row_size_old;

        for (int i = 0; i < *total_num_bases; i++)
        {
            BASE* base = &tx_bases[i];

            if (base->faction_id == faction_id)
            {
                base->nutrients_accumulated = (int)round((double)base->nutrients_accumulated * conversion_ratio);
            }

        }

    }

    // adjust minerals

    if (SE_industry_pending_new != SE_industry_pending_old)
    {
        int row_size_old = tx_basic->nutrient_cost_multi - SE_industry_pending_old;
        int row_size_new = tx_basic->nutrient_cost_multi - SE_industry_pending_new;

        double conversion_ratio = (double)row_size_new / (double)row_size_old;

        for (int i = 0; i < *total_num_bases; i++)
        {
            BASE* base = &tx_bases[i];

            if (base->faction_id == faction_id)
            {
                base->minerals_accumulated = (int)round((double)base->minerals_accumulated * conversion_ratio);
                base->minerals_accumulated_2 = (int)round((double)base->minerals_accumulated_2 * conversion_ratio);
            }

        }

    }

    fflush(debug_log);

    return returnValue;

}

/*
hex cost
*/
HOOK_API int hex_cost(int unit_id, int faction_id, int from_x, int from_y, int to_x, int to_y, int a7)
{
    // execute original code

    int value = tx_hex_cost(unit_id, faction_id, from_x, from_y, to_x, to_y, a7);

    // correct road/tube movement rate

    if (conf.tube_movement_rate_multiplier > 0)
    {
        MAP* square_from = mapsq(from_x, from_y);
        MAP* square_to = mapsq(to_x, to_y);

        if (square_from && square_to)
        {
            // set movement cost along tube to 1
            // base implies tube in its tile
            if
            (
                (square_from->items & (TERRA_BASE_IN_TILE | TERRA_MAGTUBE))
                &&
                (square_to->items & (TERRA_BASE_IN_TILE | TERRA_MAGTUBE))
            )
            {
                value = 1;

            }
            // set road movement cost
            else if (value == 1)
            {
                value = conf.road_movement_cost;

            }

        }

    }

    return value;

}

/*
Calculates tech level recursively.
*/
int wtp_tech_level(int id) {
    if (id < 0 || id > TECH_TranT)
    {
        return 0;
    }
    else
    {
        int v1 = wtp_tech_level(tx_techs[id].preq_tech1);
        int v2 = wtp_tech_level(tx_techs[id].preq_tech2);

        return max(v1, v2) + 1;
    }
}

/*
Calculates tech cost.
cost grows cubic from the beginning then linear.
S  = 10                                     // constant (first tech cost)
C  = 0.04                                   // cubic coefficient
B  = 160 * (<map area> / <normal map area>) // linear slope
x0 = SQRT(B / (3 * C))                      // break point
A  = C * x0 ^ 3 - B * x0                    // linear intercept
x  = (<level> - 1) * 7

cost = [S + (x < x0 ? C * x ^ 3 : A + B * x)] * scale
*/
int wtp_tech_cost(int fac, int tech) {
    assert(fac >= 0 && fac < 8);
    MetaFaction* m = &tx_metafactions[fac];
    int level = 1;

    if (tech >= 0) {
        level = wtp_tech_level(tech);
    }

    double S = 10.0;
    double C = 0.04;
    double B = 160 * (*map_area_tiles / 3200.0);
    double x0 = sqrt(B / (3 * C));
    double A = C * x0 * x0 * x0 - B * x0;
    double x = (level - 1) * 7;

    double base = S + (x < x0 ? C * x * x * x : A + B * x);

    double cost =
        base
        * (double)m->rule_techcost / 100.0
        * (*game_rules & RULES_TECH_STAGNATION ? 1.5 : 1.0)
        * (double)tx_basic->rules_tech_discovery_rate / 100.0
    ;

    double dw;

    if (is_human(fac)) {
        dw = 1.0 + 0.1 * (10.0 - tx_cost_ratios[*diff_level]);
    }
    else {
        dw = 1.0;
    }

    cost *= dw;

    debug("tech_cost %d %d | %8.4f %8.4f %8.4f %d %d %s\n", *current_turn, fac,
        base, dw, cost, level, tech, (tech >= 0 ? tx_techs[tech].name : NULL));

	// scale

	cost *= conf.tech_cost_scale;

    return max(2, (int)cost);

}

HOOK_API int sayBase(char *buffer, int id)
{
	// execute original code

	int returnValue = tx_say_base(buffer, id);

	// get base

	BASE *base = &(tx_bases[id]);

	// get base population limit

	int populationLimit = getBasePopulationLimit(base);

	// generate symbol

	char symbol[10] = "   <P>";

	if (populationLimit != -1 && base->pop_size >= populationLimit)
	{
		strcat(buffer, symbol);
	}

	// return original value

	return returnValue;

}

bool isBaseFacilityBuilt(BASE *base, int facilityId)
{
	return (base->facilities_built[facilityId / 8]) & (1 << (facilityId % 8));
}

int getBasePopulationLimit(BASE *base)
{
    int pop_rule = tx_metafactions[base->faction_id].rule_population;
    int hab_complex_limit = tx_basic->pop_limit_wo_hab_complex - pop_rule;
    int hab_dome_limit = tx_basic->pop_limit_wo_hab_dome - pop_rule;

    bool habitationComplexBuilt = isBaseFacilityBuilt(base, FAC_HAB_COMPLEX);
    bool habitationDomeBuilt = isBaseFacilityBuilt(base, FAC_HABITATION_DOME);

    if (habitationDomeBuilt)
	{
		return -1;
	}
	else if (habitationComplexBuilt)
	{
		return hab_dome_limit;
	}
	else
	{
		return hab_complex_limit;
	}

}

/*
Return next unbuilt growth facility or -1 if there is no next available facility.
*/
int getnextAvailableGrowthFacility(BASE *base)
{
	if (!isBaseFacilityBuilt(base, FAC_HAB_COMPLEX))
	{
		return (has_tech(base->faction_id, tx_facility[FAC_HAB_COMPLEX].preq_tech) ? FAC_HAB_COMPLEX : -1);
	}
	else if (!isBaseFacilityBuilt(base, FAC_HABITATION_DOME))
	{
		return (has_tech(base->faction_id, tx_facility[FAC_HABITATION_DOME].preq_tech) ? FAC_HABITATION_DOME : -1);
	}
	else
	{
		return -1;
	}

}

/*
Modifies base init computation.
*/
HOOK_API int baseInit(int factionId, int x, int y)
{
	// execute original code

	int id = tx_base_init(factionId, x, y);

	// return immediatelly if base wasn't created

	if (id < 0)
		return id;

	// get base

	BASE *base = &(tx_bases[id]);

	// The Planetary Transit System fix
	// new base size is limited by average faction base size

	if (has_project(factionId, FAC_PLANETARY_TRANS_SYS) && base->pop_size == 3)
	{
		// calculate average faction base size

		int totalFactionBaseCount = 0;
		int totalFactionPopulation = 0;

		for (int otherBaseId = 0; otherBaseId < *total_num_bases; otherBaseId++)
		{
			BASE *otherBase = &(tx_bases[otherBaseId]);

			// skip this base

			if (otherBaseId == id)
				continue;

			// only own bases

			if (otherBase->faction_id != base->faction_id)
				continue;

			totalFactionBaseCount++;
			totalFactionPopulation += otherBase->pop_size;

		}

		double averageFactionBaseSize = (double)totalFactionPopulation / (double)totalFactionBaseCount;

		// calculate new base size

		int newBaseSize = max(1, min(3, (int)floor(averageFactionBaseSize) - conf.pts_new_base_size_less_average));

		// set base size

		base->pop_size = newBaseSize;

	}

	// return base id

	return id;

}

/*
Wraps display help ability functionality.
This expands single number packed ability value (proportional + flat) into human readable text.
*/
HOOK_API char *getAbilityCostText(int cost, char *destination, int radix)
{
	// execute original code

	char *output = tx_itoa(cost, destination, radix);

	// clear default output

	output[0] = '\x0';

	// append help text

	if (cost == 0)
	{
		strcat(output, "Free");
	}
	else
	{
		// cost factor

		int costFactor = (cost & 0xF);
		int costFactorPercentage = 25 * costFactor;

		if (costFactor > 0)
		{
			sprintf(output + strlen(output), "Increases unit cost by %d %%. ", costFactorPercentage);
		}

		// flat cost

		int flatCost = (cost >> 4);

		if (flatCost > 0)
		{
			sprintf(output + strlen(output), "Increases unit cost by %d mineral rows. ", flatCost);
		}

	}

	// return original value

	return output;

}

/*
Wraps social_calc.
This is initially for CV GROWTH effect.
*/
HOOK_API int modifiedSocialCalc(int seSelectionsPointer, int seRatingsPointer, int factionId, int ignored4, int seChoiceEffectOnly)
{
	// execute original code

	int value = tx_social_calc(seSelectionsPointer, seRatingsPointer, factionId, ignored4, seChoiceEffectOnly);

	// ignore native faction

	if (factionId == 0)
		return value;

	// CV changes GROWTH rate if applicable

	if (conf.cloning_vats_se_growth != 0)
	{
		if (has_project(factionId, FAC_CLONING_VATS))
		{
			// calculate GROWTH rating output offset and create pointer to it

			int *seGrowthRating = (int *)seRatingsPointer + (SE_GROWTH - SE_ECONOMY);

			// modify GROWTH rating

			*seGrowthRating += conf.cloning_vats_se_growth;

		}

	}

	// return original value

	return value;

}

HOOK_API void correctGrowthTurnsForPopulationBoom(int destinationStringPointer, int sourceStringPointer)
{
    // call original function

    tx_strcat(destinationStringPointer, sourceStringPointer);

	// get current base

	int id = *current_base_id;
	BASE *base = *current_base_ptr;

	// get current base faction

	Faction *faction = &(tx_factions[base->faction_id]);

    // calculate base GROWTH rating

    int growthRating = faction->SE_growth_pending;

    if (has_facility(id, FAC_CHILDREN_CRECHE))
	{
		growthRating += 2;
	}

	if (base->status_flags & BASE_GOLDEN_AGE_ACTIVE)
	{
		growthRating += 2;
	}

    // modify output for population boom

    if (growthRating > conf.se_growth_rating_cap)
	{
		// set growth turns text offset

		char *growthTurnsText = (char *)destinationStringPointer + 8;

		// rewrtie growth turns text

		strcpy(growthTurnsText, "POP BOOM !");

	}

}

HOOK_API int modifiedRecyclingTanksMinerals(int facilityId, int baseId, int queueSlotId)
{
	// get current base

	int id = *current_base_id;
	BASE *base = *current_base_ptr;

	// Recycling Tanks increases minerals by 50%.
	// Pressure Dome now do not automatically implies Recycling Tanks so it does not deliver any RT benefits.

	if (has_facility(id, FAC_RECYCLING_TANKS) && base->mineral_intake >= 1)
	{
		// find mineral multiplier

		int multiplier = (2 * base->mineral_intake_multiplied + (base->mineral_intake - 1)) / base->mineral_intake;

		// increment multiplier

		multiplier++;

		// calculate updated value and addition

		int updatedMineralIntakeMultiplied = (multiplier * base->mineral_intake) / 2;
		int additionalMinerals = updatedMineralIntakeMultiplied - base->mineral_intake_multiplied;

		// update values

		base->mineral_intake_multiplied += additionalMinerals;
		base->mineral_surplus += additionalMinerals;

	}

	// execute original function

	int value = tx_has_fac(facilityId, baseId, queueSlotId);

	// return original value

	return value;

}

/*
Computes modified inefficiency based on given energy intake.

inefficiency = energy intake - efficiency; a complementary metric to inefficiency

efficiency = SE EFFICIENCY effect + HQ effect; capped at energy intake

SE EFFICIENCY effect = 1/8 * SE EFFICIENCY rating

HQ effect = (1 - <distance to HQ> / <1.5 * 1/4 of map width>) * energy intake; non negative
no effect if there is no HQ

*/
HOOK_API int modifiedInefficiency(int energyIntake)
{
	// execute original code

	int inefficiency = tx_black_market(energyIntake);

	// modify formula

	if (conf.alternative_inefficiency)
	{
		// accumulate efficiency from all effects

		double efficiencyRate = 0.0;

		// get current base

		int id = *current_base_id;
		BASE *base = *current_base_ptr;

		// get current base faction

		int factionId = base->faction_id;
		Faction *faction = &(tx_factions[factionId]);

		// get SE EFFICIENCY rating (with Children Creche effect)

		int seEfficiencyRating = max(-4, min(+4, faction->SE_effic_pending + (has_facility(id, FAC_CHILDREN_CRECHE) ? 1 : 0)));

		// add SE EFFICIENCY effect

		efficiencyRate += (double)(4 + seEfficiencyRating) / 8.0;

		// get HQ base

		int hqBaseId = find_hq(factionId);

		if (hqBaseId != -1)
		{
			BASE *hqBase = &(tx_bases[hqBaseId]);

			// calculate distance to HQ

			int hqDistance = map_distance(base->x, base->y, hqBase->x, hqBase->y);

			// calculate HQ effect radius

			double hqEffectRadius = 1.5 * (double)*map_half_x / 4.0;

			// calculate HQ effect

			if (hqDistance < hqEffectRadius)
			{
				efficiencyRate += 1.0 - (double)hqDistance / hqEffectRadius;

			}

		}

		// calculate efficiency

		int efficiency = max(0, min(energyIntake, (int)ceil(efficiencyRate * (double)energyIntake)));

		// update inefficiency

		inefficiency = energyIntake - efficiency;

	}

	return inefficiency;

}

/*
Wraps setup_player adding custom functionality.
*/
HOOK_API void modifiedSetupPlayer(int factionId, int a2, int a3)
{
	debug("modifiedSetupPlayer: %s\n", tx_metafactions[factionId].noun_faction);

	// execute original code

	tx_setup_player(factionId, a2, a3);

	// recreate initial extra colonies

	recreateInitialColonies(factionId);

	// give free former and colony at player start

	createFreeVehicles(factionId);

}

void createFreeVehicles(int factionId)
{
	// find first faction vehicle location

	VEH *factionVehicle = NULL;

	for (int id = 0; id < *total_num_vehicles; id++)
	{
		VEH* vehicle = &(tx_vehicles[id]);

		if (vehicle->faction_id != factionId)
			continue;

		factionVehicle = vehicle;

		break;

	}

	// faction vehicle is not found

	if (factionVehicle == NULL)
		return;

	// get spawn location

	MAP *tile = getMapTile(factionVehicle->x, factionVehicle->y);

	// create free vehicles

	int former = (is_ocean(tile) ? BSC_SEA_FORMERS : BSC_FORMERS);
	int colony = (is_ocean(tile) ? BSC_SEA_ESCAPE_POD : BSC_COLONY_POD);
	for (int j = 0; j < conf.free_colony_pods; j++)
	{
		spawn_veh(colony, factionId, factionVehicle->x, factionVehicle->y, -1);
	}
	for (int j = 0; j < conf.free_formers; j++)
	{
		spawn_veh(former, factionId, factionVehicle->x, factionVehicle->y, -1);
	}

}

/*
Wraps balance to correctly generate number of colonies at start.
*/
HOOK_API void modifiedBalance()
{
	// execute original code

	tx_balance();

	// calculate initial colony count

	for (int id = 0; id < *total_num_vehicles; id++)
	{
		VEH *vehicle = &(tx_vehicles[id]);
		int factionId = vehicle->faction_id;

		// real factions only

		if (factionId == 0)
			continue;

		// check vehicle is a colony

		if (isVehicleColony(id))
		{
			initialColonyCount[factionId]++;
		}

	}

}

void recreateInitialColonies(int factionId)
{
	// clear colony count on turn 0 and exit

	if (*current_turn == 0)
	{
		initialColonyCount[factionId] = 0;
		return;
	}

	// count current colonies

	int currentColonyCount = 0;
	VEH *factionVehicle = NULL;

	for (int id = 0; id < *total_num_vehicles; id++)
	{
		VEH *vehicle = &(tx_vehicles[id]);

		if (vehicle->faction_id == factionId && factionVehicle == NULL)
		{
			factionVehicle = vehicle;
		}

		if (vehicle->faction_id == factionId && isVehicleColony(id))
		{
			currentColonyCount++;
		}

	}

	// no faction vehicle found

	if (factionVehicle == NULL)
		return;

	// get faction tile

	MAP *tile = getMapTile(factionVehicle->x, factionVehicle->y);

	// create more colonies as needed

	if (currentColonyCount < initialColonyCount[factionId])
	{
		int colony = (is_ocean(tile) ? BSC_SEA_ESCAPE_POD : BSC_COLONY_POD);
		for (int j = 0; j < initialColonyCount[factionId] - currentColonyCount; j++)
		{
			spawn_veh(colony, factionId, factionVehicle->x, factionVehicle->y, -1);
		}

	}

}

double getVehicleSpeedWithoutRoads(int id)
{
	return (double)veh_speed(id) / (double)tx_basic->mov_rate_along_roads;
}

double getLandVehicleSpeedOnRoads(int id)
{
	double landVehicleSpeedOnRoads;

	if (conf.tube_movement_rate_multiplier > 0)
	{
		landVehicleSpeedOnRoads = (double)veh_speed(id) / (double)conf.road_movement_cost;
	}
	else
	{
		landVehicleSpeedOnRoads = (double)veh_speed(id);
	}

	return landVehicleSpeedOnRoads;

}

double getLandVehicleSpeedOnTubes(int id)
{
	double landVehicleSpeedOnTubes;

	if (conf.tube_movement_rate_multiplier > 0)
	{
		landVehicleSpeedOnTubes = (double)veh_speed(id);
	}
	else
	{
		landVehicleSpeedOnTubes = 1000;
	}

	return landVehicleSpeedOnTubes;

}

/*
Overrides world_build.
*/
HOOK_API void modifiedWorldBuild()
{
	// execute originial function

	tx_world_build();

	// modify ocean depth

	if (conf.ocean_depth_multiplier != 1.0)
	{
		debug("modify ocean depth %d\n", *map_area_tiles)

		for (int i = 0; i < *map_area_tiles; i++)
		{
			MAP *tile = &((*tx_map_ptr)[i]);

			// process only ocean

			if (!is_ocean(tile))
				continue;

			// calculate level and altitude

			int oldLevel = tile->level & 0xE0;
			int oldLevelIndex = oldLevel >> 5;
			int oldAltitude = tile->altitude;
			int oldAltitudeM = 50 * (tile->altitude - 60);
			int oldAltitudeLevelIndex = oldAltitude / 20;

			// modify altitude and level

			int newAltitudeM = (int)floor(conf.ocean_depth_multiplier * (double)oldAltitudeM);
			int newAltitude = newAltitudeM / 50 + 60;
			int newAltitudeLevelIndex = newAltitude / 20;
			int newLevelIndex = newAltitudeLevelIndex;
			int newLevel = newLevelIndex << 5;

			// update map

			tile->level = (tile->level & ~0xE0) | newLevel;
			tile->altitude = newAltitude;

			debug("[%4d] %d, %3d, %5d -> %d, %3d, %5d\n", i, oldLevelIndex, oldAltitude, oldAltitudeM, newLevelIndex, newAltitude, newAltitudeM);

			if (oldAltitudeLevelIndex != oldLevelIndex)
			{
				debug("\toldAltitudeLevelIndex and oldLevelIndex do not match.\n");
			}

		}

		debug("\n")

	}

}

/*
Overrides ZOC move to friendly unit tile check.
*/
HOOK_API int modifiedZocMoveToFriendlyUnitTileCheck(int x, int y)
{
	// execute original function

	int value = tx_veh_at(x, y);

	// process only land tiles

	if (!is_ocean(getMapTile(x, y)))
	{
		// modify behavior if instructed by configuration

		if (conf.zoc_regular_army_sneaking_disabled)
		{
			// search for any unit in target tile disabling ZoC rule
			// 1. own/pact unit except those ignoring ZoC by themselves (probe, cloaked, air)
			// 2. any non-friendly

			bool disableZOCRule = false;

			for (int id = 0; id < *total_num_vehicles; id++)
			{
				VEH *vehicle = &(tx_vehicles[id]);

				// only vehicles located in target tile

				if (!(vehicle->x == x && vehicle->y == y))
					continue;

				// own/pact
				if (vehicle->faction_id == *active_faction || tx_factions[*active_faction].diplo_status[vehicle->faction_id] & DIPLO_PACT)
				{
					// transport disables ZoC rule

					if (isVehicleTransport(vehicle))
					{
						disableZOCRule = true;
						break;
					}

					// probe does not disable ZoC rule

					else if (isVehicleProbe(vehicle))
						continue;

					// cloaked unit does not disable ZoC rule

					else if (vehicle_has_ability(vehicle, ABL_CLOAKED))
						continue;

					// air unit does not disable ZoC rule

					else if (veh_triad(id) == TRIAD_AIR)
						continue;

					// any other unit disables ZoC rule

					else
					{
						disableZOCRule = true;
						break;
					}

				}
				// non friendly unit disables ZoC rule
				else
				{
					disableZOCRule = true;
					break;
				}

			}

			// clear original function return value if there are no units disabling ZoC rule

			if (!disableZOCRule)
			{
				value = -1;
			}

		}

	}

	// return value

	return value;

}

/*
Calculates summary cost of all not prototyped components.
*/
int calculateNotPrototypedComponentsCost(int chassisId, int weaponId, int armorId, int chassisPrototyped, int weaponPrototyped, int armorPrototyped)
{
	debug("calculateNotPrototypedComponentsCost: chassis=%d, weapon=%d, armor=%d, chassisPrototyped=%d, weaponPrototyped=%d, armorPrototyped=%d\n", chassisId, weaponId, armorId, chassisPrototyped, weaponPrototyped, armorPrototyped);

	// summarize all non prototyped component costs

	int notPrototypedComponentsCost = 0;

	if (!chassisPrototyped)
	{
		notPrototypedComponentsCost += tx_chassis[chassisId].cost;
	}

	if (!weaponPrototyped)
	{
		notPrototypedComponentsCost += tx_weapon[weaponId].cost;
	}

	if (!armorPrototyped)
	{
		notPrototypedComponentsCost += tx_defense[armorId].cost;
	}

	debug("notPrototypedComponentsCost=%d\n", notPrototypedComponentsCost);

	return notPrototypedComponentsCost;

}

/*
Calculates not prototyped components cost.
*/
HOOK_API int calculateNotPrototypedComponentsCostForProduction(int unitId)
{
	UNIT *unit = &(tx_units[unitId]);

	// predefined units are all prototyped

	if (unitId < 64)
		return 0;

	// unit without name does not exist yet - impossible to get specs

	if (strlen(unit->name) == 0)
		return 0;

	// prototyped units do not have extra prototype cost

	if (unit->unit_flags & UNIT_PROTOTYPED)
		return 0;

	// get faction ID

	int factionId = unitId / 64;

	// check all prototyped components

	bool chassisPrototyped = false;
	bool weaponPrototyped = false;
	bool armorPrototyped = false;

	for (int factionPrototypedUnitId : getFactionPrototypes(factionId, false))
	{
		UNIT *factionPrototypedUnit = &(tx_units[factionPrototypedUnitId]);

		if (unit->chassis_type == factionPrototypedUnit->chassis_type || tx_chassis[unit->chassis_type].speed == tx_chassis[factionPrototypedUnit->chassis_type].speed)
		{
			chassisPrototyped = true;
		}

		if (unit->weapon_type == factionPrototypedUnit->weapon_type || tx_weapon[unit->weapon_type].offense_value == tx_weapon[factionPrototypedUnit->weapon_type].offense_value)
		{
			weaponPrototyped = true;
		}

		if (unit->armor_type == factionPrototypedUnit->armor_type || tx_defense[unit->armor_type].defense_value == tx_defense[factionPrototypedUnit->armor_type].defense_value)
		{
			armorPrototyped = true;
		}

	}

	debug("chassisPrototyped=%d, weaponPrototyped=%d, armorPrototyped=%d\n", chassisPrototyped, weaponPrototyped, armorPrototyped);

	// calculate not prototyped components cost

	return calculateNotPrototypedComponentsCost(unit->chassis_type, unit->weapon_type, unit->armor_type, chassisPrototyped, weaponPrototyped, armorPrototyped);

}

/*
Calculates extra prototype cost for design workshop INSTEAD of calculating base unit cost.
The prototype cost is calculated without regard to faction bonuses.
*/
HOOK_API int calculateNotPrototypedComponentsCostForDesign(int chassisId, int weaponId, int armorId, int chassisPrototyped, int weaponPrototyped, int armorPrototyped)
{
	// calculate modified extra prototype cost

	return calculateNotPrototypedComponentsCost(chassisId, weaponId, armorId, chassisPrototyped, weaponPrototyped, armorPrototyped);

}

HOOK_API int getActiveFactionMineralCostFactor()
{
	return tx_cost_factor(*active_faction, 1, -1);
}

HOOK_API void displayArtifactMineralContributionInformation(int input_string_pointer, int output_string_pointer)
{
	// execute original function

	tx_parse_string(input_string_pointer, output_string_pointer);

	// calculate artifact mineral contribution

	int artifactMineralContribution = tx_units[BSC_ALIEN_ARTIFACT].cost * tx_cost_factor(*active_faction, 1, -1) / 2;

	// get output string pointer

	char *output = (char *)output_string_pointer;

	// append information to string

	sprintf(output + strlen(output), "  %d minerals will be added to production", artifactMineralContribution);

}

/*
Calculates current base production mineral cost.
*/
HOOK_API int getCurrentBaseProductionMineralCost()
{
	int baseId = *current_base_id;
	BASE *base = *current_base_ptr;

	// get current base production mineral cost

	int baseProductionMineralCost = mineral_cost(baseId, base->queue_items[0]);

	// return value

	return baseProductionMineralCost;

}

HOOK_API void displayHurryCostScaledToBasicMineralCostMultiplierInformation(int input_string_pointer, int output_string_pointer)
{
	// execute original function

	tx_parse_string(input_string_pointer, output_string_pointer);

	// get output string pointer

	char *output = (char *)output_string_pointer;

	// append information to string

	sprintf(output + strlen(output), " (hurry cost is scaled to basic mineral cost multiplier: %d)", tx_basic->mineral_cost_multi);

}

/*
Scales value to basic mineral cost multiplier.
*/
int scaleValueToBasicMinieralCostMultiplier(int factionId, int value)
{
	// get mineral cost multiplier

	int mineralCostMultiplier = tx_basic->mineral_cost_multi;

	// get mineral cost factor

	int mineralCostFactor = tx_cost_factor(factionId, 1, -1);

	// scale value

	int scaledValue = (value * mineralCostMultiplier + mineralCostFactor / 2) / mineralCostFactor;

	// return value

	return scaledValue;

}

HOOK_API void displayPartialHurryCostToCompleteNextTurnInformation(int input_string_pointer, int output_string_pointer)
{
	// execute original function

	tx_parse_string(input_string_pointer, output_string_pointer);

	// apply additional information message only when flat hurry cost is enabled

	if (!conf.flat_hurry_cost)
		return;

	// get output string pointer

	char *output = (char *)output_string_pointer;

	// append information to string

	sprintf(output + strlen(output), " (minimal hurry cost to complete production next turn = %d)", getPartialHurryCostToCompleteNextTurn());

}

/*
Calculates partial hurry cost to complete next turn.
Applied to the current base.
*/
int getPartialHurryCostToCompleteNextTurn()
{
	// get current base

	int baseId = *current_base_id;
	BASE *base = *current_base_ptr;

	// get minerals neede to hurry for next turn completion

	int mineralsToHurry = mineral_cost(baseId, base->queue_items[0]) - base->minerals_accumulated - base->mineral_surplus;

	// calculate partial hurry cost

	return getPartialFlatHurryCost(baseId, mineralsToHurry);

}

/*
Adds pact condition to the spying check giving pact faction all benfits of the spying.
*/
HOOK_API int modifiedSpyingForPactBaseProductionDisplay(int factionId)
{
	// execute vanilla code

	int spying = tx_spying(factionId);

	// check if there is a pact with faction

	bool pact = isDiploStatus(*current_player_faction, factionId, DIPLO_PACT);

	// return value based on combined condition

	return ((spying != 0 || pact) ? 1 : 0);

}

void expireInfiltrations(int factionId)
{
	// check if feature is on

	if (!conf.infiltration_expire)
		return;

	debug("expireInfiltrations: %s\n", tx_metafactions[factionId].noun_faction);

	// try to discover other faction infiltration devices

	for (int infiltratingFactionId = 1; infiltratingFactionId <= 7; infiltratingFactionId++)
	{
		// skip self

		if (infiltratingFactionId == factionId)
			continue;

		// skip computer factions

		if (!is_human(infiltratingFactionId))
			continue;

		// skip faction not spying on us

		if (!isDiploStatus(infiltratingFactionId, factionId, DIPLO_HAVE_INFILTRATOR))
			continue;

		debug("\t%s\n", tx_metafactions[infiltratingFactionId].noun_faction);

		// get infiltration device count

		int infiltrationDeviceCount = getInfiltrationDeviceCount(infiltratingFactionId, factionId);

		debug("\t\tinfiltrationDeviceCount=%d\n", infiltrationDeviceCount);

		// retrieve lifetime formula parameters

		double lifetimeBase;
		double lifetimeProbeEffect;

		if (is_human(factionId))
		{
			lifetimeBase = conf.human_infiltration_device_lifetime_base;
			lifetimeProbeEffect = conf.human_infiltration_device_lifetime_probe_effect;
		}
		else
		{
			lifetimeBase = conf.computer_infiltration_device_lifetime_base;
			lifetimeProbeEffect = conf.computer_infiltration_device_lifetime_probe_effect;
		}

		// get our PROBE rating

		int probeRating = tx_factions[factionId].SE_probe_pending;
		debug("\t\tprobeRating=%d\n", probeRating);

		// calculate lifetime

		double lifetime = max(1.0, lifetimeBase + lifetimeProbeEffect * probeRating);
		debug("\t\tlifetime=%f\n", lifetime);

		// calculate probability

		double probability = min(1.0, 1.0 / lifetime);
		debug("\t\tprobability=%f\n", probability);

		// roll dice

		double probabilityRoll = random_double(1.0);
		debug("\t\tprobabilityRoll=%f\n", probabilityRoll);
		debug("\t\tdisabled=%d\n", (probabilityRoll < probability ? 1 : 0));

		if (probabilityRoll < probability)
		{
			// deactivate infiltration device

			infiltrationDeviceCount--;
			setInfiltrationDeviceCount(infiltratingFactionId, factionId, infiltrationDeviceCount);

			if (infiltrationDeviceCount == 0)
			{
				// deactivate infiltration if number of devices reaches zero

				setDiploStatus(infiltratingFactionId, factionId, DIPLO_HAVE_INFILTRATOR, false);

				// display info to humans

				if (is_human(factionId))
				{
					parse_says(0, tx_metafactions[infiltratingFactionId].noun_faction, -1, -1);
					popp(ScriptTxtID, "INFILTRATIONDEACTIVATEDENEMY", 0, "capture_sm.pcx", 0);
				}

				if (is_human(infiltratingFactionId))
				{
					parse_says(0, tx_metafactions[factionId].noun_faction, -1, -1);
					popp(ScriptTxtID, "INFILTRATIONDEACTIVATEDOURS", 0, "capture_sm.pcx", 0);
				}

			}
			else
			{
				// display info to humans

				if (is_human(factionId))
				{
					parse_says(0, tx_metafactions[infiltratingFactionId].noun_faction, -1, -1);
					parse_num(1, infiltrationDeviceCount);
					popp(ScriptTxtID, "INFILTRATIONDEVICEDISABLEDENEMY", 0, "capture_sm.pcx", 0);
				}

				if (is_human(infiltratingFactionId))
				{
					parse_says(0, tx_metafactions[factionId].noun_faction, -1, -1);
					parse_num(1, infiltrationDeviceCount);
					popp(ScriptTxtID, "INFILTRATIONDEVICEDISABLEDOURS", 0, "capture_sm.pcx", 0);
				}

			}

		}

	}

	debug("\n");

}

void setInfiltrationDeviceCount(int infiltratingFactionId, int infiltratedFactionId, int deviceCount)
{
	// device count is in range 0-0x7FFF

	deviceCount = min(0x7FFF, max(0x0, deviceCount));

	// clear infiltration devices count

	tx_factions[infiltratingFactionId].diplo_agenda[infiltratedFactionId] &= 0x0000FFFF;

	// set infiltration devices count

	tx_factions[infiltratingFactionId].diplo_agenda[infiltratedFactionId] |= (deviceCount << 16);

}

int getInfiltrationDeviceCount(int infiltratingFactionId, int infiltratedFactionId)
{
	return tx_factions[infiltratingFactionId].diplo_agenda[infiltratedFactionId] >> 16;

}

/*
Returns modified probe action risk.
*/
HOOK_API void modifiedProbeActionRisk(int action, int riskPointer)
{
	// convert risk pointer

	int *risk = (int *)riskPointer;

	switch (action)
	{
		// genetic plague
	case 7:
		*risk = 2;
		break;
	}

}

/*
Overrides Infiltrate Datalinks option text.
*/
HOOK_API int modifiedInfiltrateDatalinksOptionTextGet()
{
	// execute original code

	int textPointer = tx_text_get();

	// apply only when infiltration expiration is enabled

	if (conf.infiltration_expire)
	{
		// get number of devices

		int infiltrationDeviceCount = getInfiltrationDeviceCount(*current_player_faction, *g_PROBE_FACT_TARGET);

		// convert pointer to char *

		char *text = (char *)textPointer;

		// modify text

		sprintf(text + strlen(text), " (%d/%d devices planted)", infiltrationDeviceCount, conf.infiltration_devices);

	}

	return textPointer;

}

/*
Wraps set_treaty call for infiltration flag to also set number of devices.
*/
HOOK_API void modifiedSetTreatyForInfiltrationExpiration(int initiatingFactionId, int targetFactionId, int diploState, int set_clear)
{
	// execute original code

	tx_set_treaty(initiatingFactionId, targetFactionId, diploState, set_clear);

	// plant devices

	setInfiltrationDeviceCount(initiatingFactionId, targetFactionId, conf.infiltration_devices);

}

/*
Substitutes vanilla hurry cost with flat hurry cost.
*/
HOOK_API int modifiedHurryCost()
{
	// get current base

	int baseId = *current_base_id;

	// calculate flat hurry cost

	return getFlatHurryCost(baseId);

}

/*
Calculates flat hurry cost.
Applies only when flat hurry cost is enabled.
*/
int getFlatHurryCost(int baseId)
{
	// apply only when flat hurry cost is enabled

	if (!conf.flat_hurry_cost)
		return 0;

	// get base

	BASE *base = &(tx_bases[baseId]);

	// get hurry cost multiplier

	int hurryCostMultiplier;

	if (isBaseBuildingUnit(baseId))
	{
		hurryCostMultiplier = conf.flat_hurry_cost_multiplier_unit;
	}
	else if (isBaseBuildingFacility(baseId))
	{
		hurryCostMultiplier = conf.flat_hurry_cost_multiplier_facility;
	}
	else if (isBaseBuildingProject(baseId))
	{
		hurryCostMultiplier = conf.flat_hurry_cost_multiplier_project;
	}
	else
	{
		// should not happen
		hurryCostMultiplier = 1;
	}

	// get production remaining minerals

	int remainingMinerals = getRemainingMinerals(baseId);

	// calculate hurry cost

	int hurryCost = hurryCostMultiplier * remainingMinerals;

	// scale value to mineral cost multiplier if configured

	if (conf.fix_mineral_contribution)
	{
		hurryCost = scaleValueToBasicMinieralCostMultiplier(base->faction_id, hurryCost);
	}

	return hurryCost;

}

/*
Calculates partial flat hurry cost.
Applies only when flat hurry cost is enabled.
*/
int getPartialFlatHurryCost(int baseId, int minerals)
{
	// apply only when flat hurry cost is enabled

	if (!conf.flat_hurry_cost)
		return 0;

	// get remaining minerals

	int remainingMinerals = getRemainingMinerals(baseId);

	// nothing to hurry

	if (remainingMinerals == 0)
		return 0;

	// get hurry cost

	int hurryCost = getFlatHurryCost(baseId);

	// calculate partial flat hurry cost

	int partialHurryCost = (hurryCost * minerals + remainingMinerals / 2) / remainingMinerals;

	return partialHurryCost;

}

/*
Searches base for returned probe.
Specifically makes sure sea probe is returned to same ocean region.
*/
HOOK_API int modifiedFindReturnedProbeBase(int vehicleId)
{
	VEH *vehicle = &(tx_vehicles[vehicleId]);
	int vehicleTriad = veh_triad(vehicleId);
	MAP *vehicleTile = getVehicleMapTile(vehicleId);

	// search nearest base

	int nearestBaseId = -1;
	int nearestBaseRange = -1;

	for (int baseId = 0; baseId < *total_num_bases; baseId++)
	{
		BASE *base = &(tx_bases[baseId]);

		// own bases only

		if (base->faction_id != vehicle->faction_id)
			continue;

		// only having access to same water region for sea probes

		if (vehicleTriad == TRIAD_SEA && !isBaseConnectedToRegion(baseId, vehicleTile->region))
			continue;

		// calculate range to base

		int range = map_range(vehicle->x, vehicle->y, base->x, base->y);

		// update nearest base

		if (nearestBaseId == -1 || range < nearestBaseRange)
		{
			nearestBaseId = baseId;
			nearestBaseRange = range;
		}

	}

	return nearestBaseId;

}

/*
Fixes bugs in best defender selection.
*/
HOOK_API int modifiedBestDefender(int defenderVehicleId, int attackerVehicleId, int bombardment)
{
	int defenderTriad = veh_triad(defenderVehicleId);
	int attackerTriad = veh_triad(attackerVehicleId);

	// best defender

	int bestDefenderVehicleId = defenderVehicleId;

	// sea unit directly attacking sea unit

	if (attackerTriad == TRIAD_SEA && defenderTriad == TRIAD_SEA && bombardment == 0)
	{
		// iterate the stack

		double bestDefenderEffectiveness = 0.0;

		for (int stackedVehicleId : getStackedVehicleIds(defenderVehicleId))
		{
			VEH *stackedVehicle = &(tx_vehicles[stackedVehicleId]);
			UNIT *stackedVehicleUnit = &(tx_units[stackedVehicle->proto_id]);

			int attackerStrength;
			int defenderStrength;
			battle_compute(attackerVehicleId, stackedVehicleId, (int)&attackerStrength, (int)&defenderStrength, 0);

			double defenderEffectiveness = ((double)defenderStrength / (double)attackerStrength) * ((stackedVehicleUnit->reactor_type * 10 - stackedVehicle->damage_taken) / (stackedVehicleUnit->reactor_type * 10));

			if (bestDefenderVehicleId == -1 || defenderEffectiveness > bestDefenderEffectiveness)
			{
				bestDefenderVehicleId = stackedVehicleId;
				bestDefenderEffectiveness = defenderEffectiveness;
			}

		}

	}
	else
	{
		bestDefenderVehicleId = tx_best_defender(defenderVehicleId, attackerVehicleId, bombardment);

	}

	return bestDefenderVehicleId;

}

/*
Fixes empty square bombardment and prevents subsequent artillery duel.
*/
HOOK_API void modifiedVehSkipForActionDestroy(int vehicleId)
{
	// execute original code

	tx_veh_skip(vehicleId);

	// clear global combat variable

	*g_UNK_ATTACK_FLAGS = 0x0;

}

HOOK_API void appendAbilityCostTextInWorkshop(int output_string_pointer, int input_string_pointer)
{
	const char *COST_PREFIX = "Cost: ";

    debug
    (
        "appendAbilityCostTextInWorkshop:input(output_string=%s, input_string=%s)\n",
        (char *)output_string_pointer,
        (char *)input_string_pointer
    )
    ;

    // call original function

    tx_strcat(output_string_pointer, input_string_pointer);

    // get output string

    char *outputString = (char *)output_string_pointer;

    // find cost entry

    char *costEntry = strstr(outputString, COST_PREFIX);

    // not found

    if (costEntry == NULL)
		return;

	// extract number

	int cost = atoi(costEntry + strlen(COST_PREFIX));

	// generate explanaroty text

	int proportionalCost = (cost & 0xF);
	int flatCost = (cost >> 4);

    // add explanatory text

    sprintf(outputString + strlen(outputString), " | +%02d%%, +%2d rows", proportionalCost * 25, flatCost);
    debug("appendAbilityCostTextInWorkshop: %s\n", outputString);

}

/*
Request for break treaty before combat actions.
*/
HOOK_API void modifiedBattleFight2(int attackerVehicleId, int angle, int tx, int ty, int do_arty, int flag1, int flag2)
{
	VEH *attackerVehicle = &(tx_vehicles[attackerVehicleId]);

	if (attackerVehicle->faction_id == *current_player_faction)
	{
		int defenderVehicleId = tx_veh_at(tx, ty);

		if (defenderVehicleId >= 0)
		{
			VEH *defenderVehicle = &(tx_vehicles[defenderVehicleId]);

			int treatyNotBroken = tx_break_treaty(attackerVehicle->faction_id, defenderVehicle->faction_id, 0xB);

			if (treatyNotBroken)
				return;

			// break treaty really

			tx_act_of_aggression(attackerVehicle->faction_id, defenderVehicle->faction_id);

		}

	}

	// execute original code

	tx_battle_fight_2(attackerVehicleId, angle, tx, ty, do_arty, flag1, flag2);

}

/*
Calculates sprite offset in SocialWin__draw_social to display TALENT icon properly.
*/
HOOK_API int modifiedSocialWinDrawSocialCalculateSpriteOffset(int spriteBaseIndex, int effectValue)
{
	// calculate sprite color index for negative/positive value

	int colorIndex = (effectValue >= 1 ? 2 : 1);

	// calculate sprite index

	int spriteIndex = spriteBaseIndex + colorIndex;

	// adjust sprite index based on effect

	switch (spriteBaseIndex / 9)
	{
		// before TALENT
	case 0:
	case 1:
	case 2:
		// no change
		break;
		// TALENT
	case 3:
		// 6 sprites left
		spriteIndex -= 6;
		break;
		// after TALENT
	case 4:
	case 5:
	case 6:
	case 7:
	case 8:
	case 9:
	case 10:
	case 11:
		// previous row
		spriteIndex -= 9;
		break;
	default:
		// should not happen
		return 0;
	}

	// calculate sprite offset

	int spriteOffset = spriteIndex * 0x2C;

	// calculate sprite address

	byte *sprite = g_SOCIALWIN + 0x2F20 + spriteOffset;

	// convert and return value

	return (int)sprite;

}

HOOK_API void modifiedTechResearch(int factionId, int labs)
{
	Faction *faction = &(tx_factions[factionId]);

	// modify labs contribution from base

	if (conf.se_research_bonus_percentage != 10 && faction->SE_research_pending != 0)
	{
		int currentMultiplierPercentage = 100 + 10 * faction->SE_research_pending;
		int modifiedMultiplierPercentage = 100 + conf.se_research_bonus_percentage * faction->SE_research_pending;

		labs = (labs * modifiedMultiplierPercentage + currentMultiplierPercentage / 2) / currentMultiplierPercentage;

	}

	// pass labs to original function

	tx_tech_research(factionId, labs);

}

