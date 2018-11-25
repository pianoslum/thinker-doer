
#include "move.h"

typedef int PMTable[MAPSZ*2][MAPSZ];

PMTable pm_former;
PMTable pm_safety;
PMTable pm_enemy;

void adjust_value(int x, int y, int range, int value, PMTable tbl) {
    for (int i=-range*2; i<=range*2; i++) {
        for (int j=-range*2 + abs(i); j<=range*2 - abs(i); j+=2) {
            int x2 = wrap(x + i);
            int y2 = y + j;
            if (mapsq(x2, y2)) {
                tbl[x2][y2] += value;
            }
        }
    }
}

bool other_in_tile(int fac, MAP* sq) {
    int u = unit_in_tile(sq);
    return (u > 0 && u != fac);
}

void move_upkeep(int fac) {
    int enemymask = 1;
    convoys.clear();
    boreholes.clear();
    needferry.clear();
    memset(pm_former, 0, sizeof(pm_former));
    memset(pm_safety, 0, sizeof(pm_safety));
    memset(pm_enemy, 0, sizeof(pm_enemy));

    for (int i=1; i<8; i++) {
        enemymask |= (at_war(fac, i) ? 1 : 0) << i;
    }
    for (int i=0; i<*tx_total_num_vehicles; i++) {
        VEH* veh = &tx_vehicles[i];
        UNIT* u = &tx_units[veh->proto_id];
        auto xy = mp(veh->x, veh->y);
        if (veh->move_status == FORMER_THERMAL_BORE+4) {
            boreholes.insert(xy);
        }
        if ((1 << veh->faction_id) & enemymask) {
            int val = (u->weapon_type <= WPN_PSI_ATTACK && veh->proto_id != BSC_FUNGAL_TOWER ? -100 : -10);
            adjust_value(veh->x, veh->y, 1, val, pm_safety);
            if (val == -100) {
                int w = (u->chassis_type == CHS_INFANTRY ? 2 : 1);
                adjust_value(veh->x, veh->y, 2, val/(w*3), pm_safety);
                adjust_value(veh->x, veh->y, 3, val/(w*6), pm_safety);
            }
            adjust_value(veh->x, veh->y, 3, 1, pm_enemy);
            pm_enemy[veh->x][veh->y] += 256;
        } else if (veh->faction_id == fac) {
            if (u->weapon_type <= WPN_PSI_ATTACK) {
                adjust_value(veh->x, veh->y, 1, 40, pm_safety);
                pm_safety[veh->x][veh->y] += 60;
            }
            if ((u->weapon_type == WPN_COLONY_MODULE || u->weapon_type == WPN_TERRAFORMING_UNIT)
            && veh_triad(i) == TRIAD_LAND) {
                MAP* sq = mapsq(veh->x, veh->y);
                if (sq && is_ocean(sq) && sq->items & TERRA_BASE_IN_TILE
                && coast_tiles(veh->x, veh->y) < 8) {
                    needferry.insert(xy);
                }
            }
        }
    }
    for (int i=0; i<*tx_total_num_bases; i++) {
        BASE* base = &tx_bases[i];
        if (base->faction_id == fac) {
            adjust_value(base->x, base->y, 2, base->pop_size, pm_former);
            pm_safety[base->x][base->y] += 10000;
        }
    }
}

bool has_transport(int x, int y, int fac) {
    for (int i=0; i<*tx_total_num_vehicles; i++) {
        VEH* veh = &tx_vehicles[i];
        UNIT* u = &tx_units[veh->proto_id];
        if (veh->faction_id == fac && veh->x == x && veh->y == y
        && u->weapon_mode == WMODE_TRANSPORT) {
            return true;
        }
    }
    return false;
}

bool non_combat_move(int x, int y, int fac, int triad) {
    MAP* sq = mapsq(x, y);
    int other = unit_in_tile(sq);
    if (x < 0 || y < 0 || !sq || (triad != TRIAD_AIR && is_ocean(sq) != (triad == TRIAD_SEA))) {
        return false;
    }
    if (sq->owner > 0 && sq->owner != fac) {
        return false;
    }
    if (other < 0 || other == fac) {
        return pm_safety[x][y] >= PM_NEAR_SAFE;
    }
    return false;
}

void count_bases(int x, int y, int fac, int type, int* area, int* bases) {
    int triad = (type == LAND_ONLY ? TRIAD_LAND : TRIAD_SEA);
    MAP* sq;
    *area = 0;
    *bases = 0;
    int i = 0;
    TileSearch ts;
    ts.init(x, y, type, 2);
    while (i++ < 160 && (sq = ts.get_next()) != NULL) {
        if (sq->items & TERRA_BASE_IN_TILE) {
            (*bases)++;
        } else if (can_build_base(ts.rx, ts.ry, fac, triad)) {
            (*area)++;
        }
    }
    debuglog("count_bases %d %d %d %d %d\n", x, y, type, *area, *bases);
}

bool has_base_sites(int x, int y, int fac, int type) {
    int area;
    int bases;
    count_bases(x, y, fac, type, &area, &bases);
    return area > bases+4;
}

bool need_heals(VEH* veh) {
    return veh->damage_taken > 2 && (veh->proto_id < BSC_BATTLE_OGRE_MK1 || veh->proto_id > BSC_BATTLE_OGRE_MK3);
}

bool defend_tile(VEH* veh, MAP* sq) {
    UNIT* u = &tx_units[veh->proto_id];
    if (!sq) {
        return true;
    } else if (need_heals(veh) && sq->items & (TERRA_BASE_IN_TILE | TERRA_MONOLITH)) {
        return true;
    } else if (~sq->items & TERRA_BASE_IN_TILE || u->weapon_type <= WPN_PSI_ATTACK) {
        return false;
    }
    int n = 0;
    for (const int* t : offset) {
        int x2 = wrap(veh->x + t[0]);
        int y2 = veh->y + t[1];
        if (mapsq(x2, y2) && pm_safety[x2][y2] < PM_SAFE) {
            n++;
        }
    }
    return n > 3;
}

int escape_score(int x, int y, int range, VEH* veh, MAP* sq) {
    return pm_safety[x][y] - range*120
        + (sq->items & TERRA_MONOLITH && need_heals(veh) ? 1000 : 0)
        + (sq->items & TERRA_BUNKER ? 500 : 0)
        + (sq->items & TERRA_FOREST ? 100 : 0)
        + (sq->rocks & TILE_ROCKY ? 100 : 0);
}

int escape_move(int id) {
    VEH* veh = &tx_vehicles[id];
    MAP* sq = mapsq(veh->x, veh->y);
    if (defend_tile(veh, sq))
        return veh_skip(id);
    int fac = veh->faction_id;
    int tscore = pm_safety[veh->x][veh->y];
    int i = 0;
    int tx = -1;
    int ty = -1;
    TileSearch ts;
    ts.init(veh->x, veh->y, (veh_triad(id) == TRIAD_SEA ? WATER_ONLY : LAND_ONLY));
    while (i++ < (tscore < 500 ? 80 : 24) && (sq = ts.get_next()) != NULL) {
        int score = escape_score(ts.rx, ts.ry, ts.dist-1, veh, sq);
        if (score > tscore && !other_in_tile(fac, sq) && !ts.has_zoc(fac)) {
            tx = ts.rx;
            ty = ts.ry;
            tscore = score;
            debuglog("escape_score %d %d -> %d %d | %d %d %d\n",
                veh->x, veh->y, ts.rx, ts.ry, i, ts.dist, score);
        }
    }
    if (tx >= 0) {
        debuglog("escape_move %d %d -> %d %d | %d %d\n",
            veh->x, veh->y, tx, ty, pm_safety[veh->x][veh->y], tscore);
        return set_move_to(id, tx, ty);
    }
    if (tx_units[veh->proto_id].weapon_type <= WPN_PSI_ATTACK)
        return tx_enemy_move(id);
    return veh_skip(id);
}

int trans_move(int id) {
    VEH* veh = &tx_vehicles[id];
    int fac = veh->faction_id;
    int triad = veh_triad(id);
    auto xy = mp(veh->x, veh->y);
    if (needferry.count(xy)) {
        needferry.erase(xy);
        return veh_skip(id);
    }
    if (needferry.size() > 0 && triad == TRIAD_SEA && at_target(veh)) {
        MAP* sq;
        int i = 0;
        TileSearch ts;
        ts.init(veh->x, veh->y, WATER_ONLY);
        while (i++ < 120 && (sq = ts.get_next()) != NULL) {
            xy = mp(ts.rx, ts.ry);
            if (needferry.count(xy) && non_combat_move(ts.rx, ts.ry, fac, triad)) {
                needferry.erase(xy);
                debuglog("trans_move %d %d -> %d %d\n", veh->x, veh->y, ts.rx, ts.ry);
                return set_move_to(id, ts.rx, ts.ry);
            }
        }
    }
    return tx_enemy_move(id);
}

int want_convoy(int fac, int x, int y, MAP* sq) {
    if (sq && sq->owner == fac && !convoys.count(mp(x, y))
    && !(sq->items & BASE_DISALLOWED)) {
        int bonus = tx_bonus_at(x, y);
        if (bonus == RES_ENERGY)
            return RES_NONE;
        else if (sq->items & TERRA_CONDENSER)
            return RES_NUTRIENT;
        else if (bonus == RES_NUTRIENT)
            return RES_NONE;
        else if (sq->items & TERRA_MINE && sq->rocks & TILE_ROCKY)
            return RES_MINERAL;
        else if (sq->items & TERRA_FOREST && ~sq->items & TERRA_RIVER)
            return RES_MINERAL;
    }
    return RES_NONE;
}

int crawler_move(int id) {
    VEH* veh = &tx_vehicles[id];
    MAP* sq = mapsq(veh->x, veh->y);
    if (!sq || veh->home_base_id < 0)
        return veh_skip(id);
    if (!at_target(veh))
        return SYNC;
    BASE* base = &tx_bases[veh->home_base_id];
    int res = want_convoy(veh->faction_id, veh->x, veh->y, sq);
    bool prefer_min = base->nutrient_surplus > 5;
    int v1 = 0;
    if (res) {
        v1 = (sq->items & TERRA_FOREST ? 1 : 2);
    }
    if (res && v1 > 1) {
        return set_convoy(id, res);
    }
    int i = 0;
    int cx = -1;
    int cy = -1;
    TileSearch ts;
    ts.init(veh->x, veh->y, LAND_ONLY);

    while (i++ < 40 && (sq = ts.get_next()) != NULL) {
        int other = unit_in_tile(sq);
        if (other > 0 && other != veh->faction_id) {
            debuglog("convoy_skip %d %d %d %d %d %d\n", veh->x, veh->y,
                ts.rx, ts.ry, veh->faction_id, other);
            continue;
        }
        res = want_convoy(veh->faction_id, ts.rx, ts.ry, sq);
        if (res && pm_safety[ts.rx][ts.ry] >= PM_SAFE) {
            int v2 = (sq->items & TERRA_FOREST ? 1 : 2);
            if (prefer_min && res == RES_MINERAL && v2 > 1) {
                return set_move_to(id, ts.rx, ts.ry);
            } else if (!prefer_min && res == RES_NUTRIENT) {
                return set_move_to(id, ts.rx, ts.ry);
            }
            if (res == RES_MINERAL && v2 > v1) {
                cx = ts.rx;
                cy = ts.ry;
                v1 = v2;
            }
        }
    }
    if (cx >= 0)
        return set_move_to(id, cx, cy);
    if (v1 > 0)
        return set_convoy(id, RES_MINERAL);
    return veh_skip(id);
}

bool want_base(int x, int y, int fac, int triad) {
    MAP* sq = mapsq(x, y);
    if (!sq || y < 3 || y >= *tx_map_axis_y-3)
        return false;
    if (pm_former[x][y] > 0 && ~sq->landmarks & LM_JUNGLE)
        return false;
    if (sq->rocks & TILE_ROCKY || (sq->items & (BASE_DISALLOWED | TERRA_CONDENSER)))
        return false;
    if (sq->owner > 0 && fac != sq->owner && !at_war(fac, sq->owner))
        return false;
    if (sq->landmarks & LM_JUNGLE) {
        if (bases_in_range(x, y, 1) > 0 || bases_in_range(x, y, 2) > 1)
            return false;
    } else {
        if (bases_in_range(x, y, 2) > 0)
            return false;
    }
    if (triad != TRIAD_SEA && !is_ocean(sq)) {
        return true;
    } else if (triad == TRIAD_SEA && is_ocean_shelf(sq)) {
        return true;
    }
    return false;
}

bool can_build_base(int x, int y, int fac, int triad) {
    return want_base(x, y, fac, triad) && non_combat_move(x, y, fac, triad);
}

int base_tile_score(int x1, int y1, int range, int triad) {
    const int priority[][2] = {
        {TERRA_RIVER, 2},
        {TERRA_RIVER_SRC, 2},
        {TERRA_FUNGUS, -2},
        {TERRA_FARM, 2},
        {TERRA_FOREST, 2},
        {TERRA_MONOLITH, 4},
    };
    MAP* sq = mapsq(x1, y1);
    int score = (!is_ocean(sq) && sq->items & TERRA_RIVER ? 4 : 0);
    int land = 0;
    range *= (triad == TRIAD_LAND ? 3 : 1);

    for (int i=0; i<20; i++) {
        int x2 = wrap(x1 + offset_tbl[i][0]);
        int y2 = y1 + offset_tbl[i][1];
        sq = mapsq(x2, y2);
        if (sq) {
            int items = sq->items;
            score += (tx_bonus_at(x2, y2) ? 6 : 0);
            if (sq->landmarks && !(sq->landmarks & (LM_DUNES | LM_SARGASSO | LM_UNITY)))
                score += (sq->landmarks & LM_JUNGLE ? 3 : 2);
            if (i < 8) {
                if (triad == TRIAD_SEA && !is_ocean(sq)
                && nearby_tiles(x2, y2, LAND_ONLY, 20) >= 20 && ++land < 4)
                    score += (sq->owner < 1 ? 20 : 5);
                if (is_ocean_shelf(sq))
                    score += 3;
            }
            if (!is_ocean(sq)) {
                if (sq->level & TILE_RAINY)
                    score++;
                if (sq->rocks & TILE_ROLLING)
                    score++;
            }
            for (const int* p : priority) if (items & p[0]) score += p[1];
        }
    }
    return score - range + random(6) + min(0, pm_safety[x1][y1]);
}

int colony_move(int id) {
    VEH* veh = &tx_vehicles[id];
    MAP* sq = mapsq(veh->x, veh->y);
    int fac = veh->faction_id;
    int triad = veh_triad(id);
    if (defend_tile(veh, sq)) {
        return veh_skip(id);
    }
    if (pm_safety[veh->x][veh->y] < PM_SAFE) {
        return escape_move(id);
    }
    if ((at_target(veh) || triad == TRIAD_LAND) && want_base(veh->x, veh->y, fac, triad)) {
        tx_action_build(id, 0);
        return SYNC;
    }
    if (is_ocean(sq) && triad == TRIAD_LAND && has_transport(veh->x, veh->y, fac)) {
        for (const int* t : offset) {
            int x2 = wrap(veh->x + t[0]);
            int y2 = veh->y + t[1];
            if (non_combat_move(x2, y2, fac, triad) && has_base_sites(x2, y2, fac, LAND_ONLY)) {
                debuglog("colony_trans %d %d -> %d %d\n", veh->x, veh->y, x2, y2);
                return set_move_to(id, x2, y2);
            }
        }
    }
    if (at_target(veh) || !can_build_base(veh->waypoint_1_x, veh->waypoint_1_y, fac, triad)) {
        int i = 0;
        int k = 0;
        int tscore = INT_MIN;
        int tx = -1;
        int ty = -1;
        TileSearch ts;
        ts.init(veh->x, veh->y, (triad == TRIAD_SEA ? WATER_ONLY : LAND_ONLY), 2);
        while (i++ < 200 && k < 20 && (sq = ts.get_next()) != NULL) {
            if (!can_build_base(ts.rx, ts.ry, fac, triad))
                continue;
            int score = base_tile_score(ts.rx, ts.ry, i/8, triad);
            k++;
            if (score > tscore) {
                tx = ts.rx;
                ty = ts.ry;
                tscore = score;
            }
        }
        if (tx >= 0) {
            debuglog("colony_move %d %d -> %d %d | %d %d | %d\n", veh->x, veh->y, tx, ty, fac, id, tscore);
            adjust_value(tx, ty, 1, 1, pm_former);
            return set_move_to(id, tx, ty);
        }
    }
    if (!at_target(veh))
        return SYNC;
    if (triad == TRIAD_LAND)
        return tx_enemy_move(id);
    return veh_skip(id);
}

bool can_bridge(int x, int y) {
    if (coast_tiles(x, y) < 3)
        return false;
    const int range = 4;
    MAP* sq;
    TileSearch ts;
    ts.init(x, y, LAND_ONLY);
    int k = 0;
    while (k++ < 120 && ts.get_next() != NULL);

    int n = 0;
    for (int i=-range*2; i<=range*2; i++) {
        for (int j=-range*2 + abs(i); j<=range*2 - abs(i); j+=2) {
            int x2 = wrap(x + i);
            int y2 = y + j;
            sq = mapsq(x2, y2);
            if (sq && y2 > 0 && y2 < *tx_map_axis_y-1 && !is_ocean(sq)
            && !ts.oldtiles.count(mp(x2, y2))) {
                n++;
            }
        }
    }
    debuglog("bridge %d %d %d %d\n", x, y, k, n);
    return n > 4;
}

bool can_borehole(int x, int y, int bonus) {
    MAP* sq = mapsq(x, y);
    if (!sq || sq->items & (BASE_DISALLOWED | IMP_ADVANCED) || bonus == RES_NUTRIENT)
        return false;
    if (bonus == RES_NONE && sq->rocks & TILE_ROLLING)
        return false;
    if (pm_former[x][y] < 4 || !workable_tile(x, y, sq->owner))
        return false;
    int level = sq->level >> 5;
    for (const int* t : offset) {
        int x2 = wrap(x + t[0]);
        int y2 = y + t[1];
        sq = mapsq(x2, y2);
        if (!sq || sq->items & TERRA_THERMAL_BORE || boreholes.count(mp(x2, y2)))
            return false;
        int level2 = sq->level >> 5;
        if (level2 < level && level2 > LEVEL_OCEAN_SHELF)
            return false;
    }
    return true;
}

bool can_farm(int x, int y, int bonus, bool has_eco, MAP* sq) {
    if (bonus == RES_NUTRIENT)
        return true;
    if (bonus == RES_ENERGY || bonus == RES_MINERAL)
        return false;
    if (sq->landmarks & LM_JUNGLE && !has_eco)
        return false;
    if (nearby_items(x, y, 1, TERRA_FOREST) < (sq->items & TERRA_FOREST ? 4 : 1))
        return false;
    if (sq->level & TILE_RAINY && sq->rocks & TILE_ROLLING && ~sq->landmarks & LM_JUNGLE)
        return true;
    return (has_eco && !(x % 2) && !(y % 2) && !(abs(x-y) % 4));
}

bool can_fungus(int x, int y, int bonus, MAP* sq) {
    if (sq->items & (TERRA_BASE_IN_TILE | TERRA_MONOLITH | IMP_ADVANCED))
        return false;
    return !can_borehole(x, y, bonus) && nearby_items(x, y, 1, TERRA_FUNGUS) < 5;
}

bool can_sensor(int x, int y, int fac, MAP* sq) {
    return ~sq->items & TERRA_SENSOR && has_terra(fac, FORMER_SENSOR)
        && !nearby_items(x, y, 2, TERRA_SENSOR);
}

int select_item(int x, int y, int fac, MAP* sq) {
    int items = sq->items;
    int bonus = tx_bonus_at(x, y);
    bool rocky_sq = sq->rocks & TILE_ROCKY;
    bool has_eco = has_terra(fac, FORMER_CONDENSER);

    if (items & TERRA_BASE_IN_TILE)
        return -1;
    if (items & TERRA_FUNGUS) {
        if (plans[fac].keep_fungus && can_fungus(x, y, bonus, sq)) {
            if (~items & TERRA_ROAD && knows_tech(fac, tx_basic->tech_preq_build_road_fungus))
                return FORMER_ROAD;
            else if (can_sensor(x, y, fac, sq) && knows_tech(fac, tx_basic->tech_preq_improv_fungus))
                return FORMER_SENSOR;
            return -1;
        }
        return FORMER_REMOVE_FUNGUS;
    }
    if (~items & TERRA_ROAD)
        return FORMER_ROAD;
    if (has_terra(fac, FORMER_RAISE_LAND) && can_bridge(x, y)) {
        int cost = tx_terraform_cost(x, y, fac);
        if (cost < tx_factions[fac].energy_credits/10) {
            return FORMER_RAISE_LAND;
        }
    }
    if (workable_tile(x, y, fac)) {
        if (plans[fac].plant_fungus && can_fungus(x, y, bonus, sq))
            return FORMER_PLANT_FUNGUS;
        else if (items & BASE_DISALLOWED)
            return -1;
    } else {
        return -1;
    }
    if (has_terra(fac, FORMER_THERMAL_BORE) && can_borehole(x, y, bonus))
        return FORMER_THERMAL_BORE;
    if (rocky_sq && (bonus == RES_NUTRIENT || sq->landmarks & LM_JUNGLE) && has_terra(fac, FORMER_LEVEL_TERRAIN))
        return FORMER_LEVEL_TERRAIN;
    if (rocky_sq && ~items & TERRA_MINE && (has_eco || bonus == RES_MINERAL) && has_terra(fac, FORMER_MINE))
        return FORMER_MINE;
    if (!rocky_sq && can_farm(x, y, bonus, has_eco, sq)) {
        if (~items & TERRA_FARM && (has_eco || bonus == RES_NUTRIENT) && has_terra(fac, FORMER_FARM))
            return FORMER_FARM;
        else if (has_eco && ~items & TERRA_CONDENSER)
            return FORMER_CONDENSER;
        else if (has_terra(fac, FORMER_SOIL_ENR) && ~items & TERRA_SOIL_ENR)
            return FORMER_SOIL_ENR;
        else if (!has_eco && !(items & (TERRA_CONDENSER | TERRA_SOLAR)) && has_terra(fac, FORMER_SOLAR))
            return FORMER_SOLAR;
    } else if (!rocky_sq && !(items & (TERRA_FARM | TERRA_CONDENSER))) {
        if (can_sensor(x, y, fac, sq))
            return FORMER_SENSOR;
        else if (~items & TERRA_FOREST && has_terra(fac, FORMER_FOREST))
            return FORMER_FOREST;
    }
    return -1;
}

int tile_score(int x1, int y1, int x2, int y2, int fac, MAP* sq) {
    const int priority[][2] = {
        {TERRA_RIVER, 2},
        {TERRA_RIVER_SRC, 2},
        {TERRA_ROAD, -5},
        {TERRA_FARM, -2},
        {TERRA_FOREST, -4},
        {TERRA_MINE, -4},
        {TERRA_CONDENSER, -4},
        {TERRA_SOIL_ENR, -4},
        {TERRA_THERMAL_BORE, -8},
    };
    int bonus = tx_bonus_at(x2, y2);
    int range = map_range(x1, y1, x2, y2);
    int items = sq->items;
    int score = (sq->landmarks ? 3 : 0);

    if (bonus && !(items & IMP_ADVANCED)) {
        bool bh = (bonus == RES_MINERAL || bonus == RES_ENERGY)
            && has_terra(*tx_active_faction, FORMER_THERMAL_BORE)
            && can_borehole(x2, y2, bonus);
        int w = (bh || !(items & IMP_SIMPLE) ? 5 : 1);
        score += w * (bonus == RES_NUTRIENT ? 3 : 2);
    }
    for (const int* p : priority) {
        if (items & p[0]) {
            score += p[1];
        }
    }
    if (items & TERRA_FUNGUS) {
        score += (items & IMP_ADVANCED ? 20 : 0);
        score += (plans[fac].keep_fungus ? -10 : -2);
        score += (plans[fac].plant_fungus && (items & TERRA_ROAD) ? -8 : 0);
    }
    return score - range/2 + min(8, pm_former[x2][y2]) + min(0, pm_safety[x2][y2]);
}

int former_move(int id) {
    VEH* veh = &tx_vehicles[id];
    int fac = veh->faction_id;
    int x = veh->x;
    int y = veh->y;
    MAP* sq = mapsq(x, y);
    bool safe = pm_safety[x][y] >= PM_SAFE;
    if (!sq || sq->owner != fac) {
        return tx_enemy_move(id);
    }
    if (defend_tile(veh, sq)) {
        return veh_skip(id);
    }
    if (safe || (veh->move_status >= 16 && veh->move_status < 22)) {
        if (veh->move_status >= 4 && veh->move_status < 24) {
            return SYNC;
        }
        int item = select_item(x, y, fac, sq);
        if (item >= 0) {
            pm_former[x][y] -= 2;
            debuglog("former_action %d %d %d %d %d\n", x, y, fac, id, item);
            if (item == FORMER_RAISE_LAND) {
                int cost = tx_terraform_cost(x, y, fac);
                tx_factions[fac].energy_credits -= cost;
                debuglog("bridge_cost %d %d %d %d\n", x, y, fac, cost);
            }
            return set_action(id, item+4, *tx_terraform[item].shortcuts);
        }
    } else if (!safe) {
        return escape_move(id);
    }
    int i = 0;
    int tscore = INT_MIN;
    int tx = -1;
    int ty = -1;
    int bx = x;
    int by = y;
    if (veh->home_base_id >= 0) {
        bx = tx_bases[veh->home_base_id].x;
        by = tx_bases[veh->home_base_id].y;
    }
    TileSearch ts;
    ts.init(x, y, LAND_ONLY);

    while (i++ < 40 && (sq = ts.get_next()) != NULL) {
        if (sq->owner != fac || sq->items & TERRA_BASE_IN_TILE
        || pm_safety[ts.rx][ts.ry] < PM_SAFE
        || pm_former[ts.rx][ts.ry] < 1
        || other_in_tile(fac, sq))
            continue;
        int score = tile_score(bx, by, ts.rx, ts.ry, fac, sq);
        if (score > tscore && select_item(ts.rx, ts.ry, fac, sq) != -1) {
            tx = ts.rx;
            ty = ts.ry;
            tscore = score;
        }
    }
    if (tx >= 0) {
        pm_former[tx][ty] -= 2;
        debuglog("former_move %d %d -> %d %d | %d %d | %d\n", x, y, tx, ty, fac, id, tscore);
        return set_road_to(id, tx, ty);
    } else if (!random(4)) {
        return set_move_to(id, bx, by);
    }
    return veh_skip(id);
}

int best_defender(int x, int y, int fac, int att_id) {
    int id = tx_veh_at(x, y);
    if (id < 0 || ~tx_vehicles[id].visibility & (1 << fac))
        return -1;
    return tx_best_defender(id, att_id, 0);
}

int combat_value(int id, int power, int moves, int mov_rate) {
    VEH* veh = &tx_vehicles[id];
    return max(1, power * (10 - veh->damage_taken) * min(moves, mov_rate) / mov_rate);
}

double battle_eval(int id1, int id2, int moves, int mov_rate) {
    int s1;
    int s2;
    tx_battle_compute(id1, id2, (int)&s1, (int)&s2, 0);
    int v1 = combat_value(id1, s1, moves, mov_rate);
    int v2 = combat_value(id2, s2, mov_rate, mov_rate);
    return 1.0 * v1/v2;
}

int combat_move(int id) {
    VEH* veh = &tx_vehicles[id];
    MAP* sq = mapsq(veh->x, veh->y);
    UNIT* u = &tx_units[veh->proto_id];
    int fac = veh->faction_id;
    if (is_ocean(sq) || u->ability_flags & ABL_ARTILLERY)
        return tx_enemy_move(id);
    if (pm_enemy[veh->x][veh->y] < 1) {
        if (need_heals(veh))
            return escape_move(id);
        return tx_enemy_move(id);
    }
    bool at_base = sq->items & TERRA_BASE_IN_TILE;
    int mov_rate = tx_basic->mov_rate_along_roads;
    int moves = veh_speed(id) - veh->road_moves_spent;
    int max_dist = moves;
    int defenders = 0;

    if (at_base) {
        for (int i=0; i<*tx_total_num_vehicles; i++) {
            VEH* v = &tx_vehicles[i];
            if (veh->x == v->x && veh->y == v->y && tx_units[v->proto_id].weapon_type <= WPN_PSI_ATTACK) {
                defenders++;
            }
        }
        max_dist = (defenders > 1 ? moves : 1);
    }
    int i = 0;
    int tx = -1;
    int ty = -1;
    int id2 = -1;
    double odds = (at_base ? 1.4 - 0.05 * min(5, defenders) : 1.1);
    TileSearch ts;
    ts.init(veh->x, veh->y, NEAR_ROADS);

    while (i++ < 50 && (sq = ts.get_next()) != NULL && ts.dist <= max_dist) {
        bool to_base = sq->items & TERRA_BASE_IN_TILE;
        int fac2 = unit_in_tile(sq);
        if (at_war(fac, fac2) && (id2 = best_defender(ts.rx, ts.ry, fac, id)) != -1) {
            VEH* veh2 = &tx_vehicles[id2];
            UNIT* u2 = &tx_units[veh2->proto_id];
            max_dist = min(ts.dist, max_dist);
            if (ts.dist > 1 && ~veh2->visibility & (1 << fac))
                continue;
            if (!to_base && veh_triad(id2) == TRIAD_AIR && ~u->ability_flags & ABL_AIR_SUPERIORITY)
                continue;
            MAP* prev_sq = ts.prev_tile();
            bool from_base = prev_sq && prev_sq->items & (TERRA_BASE_IN_TILE | TERRA_BUNKER);
            double v1 = battle_eval(id, id2, moves - ts.dist + 1, mov_rate);
            double v2 = (sq->owner == fac ? 0.1 : 0.0)
                + (to_base ? 0.0 : 0.05 * (pm_enemy[ts.rx][ts.ry]/256))
                + (from_base ? 0.15 : 0.0002 * (pm_safety[ts.rx][ts.ry]))
                + min(12, abs(offense_value(u2)))*(u2->chassis_type == CHS_INFANTRY ? 0.008 : 0.02);
            double v3 = min(1.6, v1) + min(0.5, max(-0.5, v2));

            debuglog("attack_odds %2d %2d -> %2d %2d | %d %d %d %d | %d %d %.4f %.4f %.4f %.4f | %d %d %s | %d %d %s\n",
                veh->x, veh->y, ts.rx, ts.ry, ts.dist, max_dist, from_base, to_base,
                pm_enemy[ts.rx][ts.ry], pm_safety[ts.rx][ts.ry], v1, v2, v3, odds,
                id, fac, u->name, id2, fac2, u2->name);
            if (v3 > odds) {
                tx = ts.rx;
                ty = ts.ry;
                odds = v3;
            }
        } else if (to_base && at_war(fac, sq->owner) && fac2 < 0) {
            tx = ts.rx;
            ty = ts.ry;
            break;
        }
    }
    if (tx >= 0) {
        debuglog("attack_set  %2d %2d -> %2d %2d\n", veh->x, veh->y, tx, ty);
        return set_move_to(id, tx, ty);
    }
    if (need_heals(veh)) {
        return escape_move(id);
    }
    return tx_enemy_move(id);
}



