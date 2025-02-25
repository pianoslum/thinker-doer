
#include "patch.h"
#include "game.h"
#include "move.h"
#include "tech.h"
#include "engine.h"
#include "wtp.h"
#include "ai.h"
#include "aiProduction.h"

const char* ac_alpha = "ac_mod\\alphax";
const char* ac_help = "ac_mod\\helpx";
const char* ac_tutor = "ac_mod\\tutor";
const char* ac_concepts = "ac_mod\\conceptsx";
const char* ac_opening = "opening";
const char* ac_movlist = "movlist";
const char* ac_movlist_txt = "movlist.txt";
const char* ac_movlistx_txt = "movlistx.txt";

const char* lm_params[] = {
    "crater", "volcano", "jungle", "uranium",
    "sargasso", "ruins", "dunes", "fresh",
    "mesa", "canyon", "geothermal", "ridge",
    "borehole", "nexus", "unity", "fossil"
};

int prev_rnd = -1;
Points natives;
Points goodtiles;

bool FileExists(const char* path) {
    return GetFileAttributes(path) != INVALID_FILE_ATTRIBUTES;
}

HOOK_API int mod_crop_yield(int faction, int base, int x, int y, int tf) {
// [WtP] delegate to generic function instead
//    int value = crop_yield(faction, base, x, y, tf);
    int value = mod_nutrient_yield(faction, base, x, y, tf);
    MAP* sq = mapsq(x, y);
    if (sq && sq->items & TERRA_THERMAL_BORE) {
        value++;
    }
    return value;
}

HOOK_API int mod_base_draw(int ptr, int base_id, int x, int y, int zoom, int v1) {
    BASE* b = &tx_bases[base_id];
    base_draw(ptr, base_id, x, y, zoom, v1);

    if (zoom >= -8 && has_facility(base_id, FAC_HEADQUARTERS)) {
        // Game engine uses this way to determine the population label width
        const char* s1 = "8";
        const char* s2 = "88";
        int w = font_width(*(int*)0x93FC24, (int)(b->pop_size >= 10 ? s2 : s1)) + 5;
        int h = *(int*)((*(int*)0x93FC24)+16) + 4;

        for (int i=1; i<3; i++) {
            RECT rr = {x-i, y-i, x+w+i, y+h+i};
            buffer_box(ptr, (int)(&rr), 255, 255);
        }
    }
    return 0;
}

void check_relocate_hq(int faction) {
    if (find_hq(faction) < 0) {
        double best_score = INT_MIN;
        int best_id = -1;
        Points bases;
        for (int i=0; i<*total_num_bases; i++) {
            BASE* b = &tx_bases[i];
            if (b->faction_id == faction) {
                bases.insert({b->x, b->y});
            }
        }
        for (int i=0; i<*total_num_bases; i++) {
            BASE* b = &tx_bases[i];
            if (b->faction_id == faction) {
                double score = b->pop_size*0.2 - mean_range(bases, b->x, b->y);
                debug("relocate_hq %.4f %s\n", score, b->name);
                if (score > best_score) {
                    best_id = i;
                    best_score = score;
                }
            }
        }
        if (best_id >= 0) {
            BASE* b = &tx_bases[best_id];
            b->facilities_built[FAC_HEADQUARTERS/8] |= (1 << (FAC_HEADQUARTERS % 8));
            draw_tile(b->x, b->y, 0);
        }
    }
}

HOOK_API int mod_capture_base(int base_id, int faction, int probe) {

    int old_faction = tx_bases[base_id].faction_id;
    assert(faction > 0 && faction < 8 && faction != old_faction);

    // set current base to fix free facilities creation bug

    tx_set_base(base_id);

    // execute original code

    capture_base(base_id, faction, probe);

    // relocate HQ if needed

    check_relocate_hq(old_faction);

    return 0;

}

HOOK_API int mod_base_kill(int base_id) {
    int old_faction = tx_bases[base_id].faction_id;
    assert(base_id >= 0 && base_id < *total_num_bases);
    base_kill(base_id);
    check_relocate_hq(old_faction);
    return 0;
}

HOOK_API int content_pop() {
    int faction = (*current_base_ptr)->faction_id;
    assert(faction > 0 && faction < 8);
    if (is_human(faction)) {
        return conf.content_pop_player[*diff_level];
    }
    return conf.content_pop_computer[*diff_level];
}

void process_map(int k) {
    TileSearch ts;
    Points visited;
    Points current;
    natives.clear();
    goodtiles.clear();
    // Map area square root values: Standard = 56, Huge = 90
    int limit = *map_area_sq_root * (*map_area_sq_root < 70 ? 1 : 2);

    for (int y = 0; y < *map_axis_y; y++) {
        for (int x = y&1; x < *map_axis_x; x+=2) {
            MAP* sq = mapsq(x, y);
            if (unit_in_tile(sq) == 0)
                natives.insert({x, y});
            if (y < 4 || y >= *map_axis_x - 4)
                continue;
            if (sq && !is_ocean(sq) && !visited.count({x, y})) {
                int n = 0;
                ts.init(x, y, TRIAD_LAND, k);
                while ((sq = ts.get_next()) != NULL) {
                    auto p = mp(ts.rx, ts.ry);
                    visited.insert(p);
                    if (~sq->items & TERRA_FUNGUS && !(sq->landmarks & ~LM_FRESH)) {
                        current.insert(p);
                    }
                    n++;
                }
                if (n >= limit) {
                    for (auto& p : current) {
                        goodtiles.insert(p);
                    }
                }
                current.clear();
            }
        }
    }
    if ((int)goodtiles.size() < *map_area_sq_root * 8) {
        goodtiles.clear();
    }
    debug("process_map %d %d %d %d %d\n", *map_axis_x, *map_axis_y,
        *map_area_sq_root, visited.size(), goodtiles.size());
}

bool valid_start (int faction, int iter, int x, int y, bool aquatic) {
    Points pods;
    MAP* sq = mapsq(x, y);
    if (!sq || sq->items & BASE_DISALLOWED || (sq->rocks & TILE_ROCKY && !is_ocean(sq)))
        return false;
    if (sq->landmarks & ~LM_FRESH)
        return false;
    if (aquatic != is_ocean(sq) || tx_bonus_at(x, y) != RES_NONE)
        return false;
    if (point_range(natives, x, y) < 4)
        return false;
    int sc = 0;
    int nut = 0;
    for (int i=0; i<44; i++) {
        int x2 = wrap(x + offset_tbl[i][0]);
        int y2 = y + offset_tbl[i][1];
        sq = mapsq(x2, y2);
        if (sq) {
            int bonus = tx_bonus_at(x2, y2);
            if (is_ocean(sq)) {
                if (!is_ocean_shelf(sq))
                    sc--;
                else if (aquatic)
                    sc += 4;
            } else {
                sc += (sq->level & (TILE_RAINY | TILE_MOIST) ? 3 : 1);
                if (sq->items & TERRA_RIVER)
                    sc += (i < 20 ? 4 : 2);
                if (sq->rocks & TILE_ROLLING)
                    sc += 1;
            }
            if (bonus > 0) {
                if (i < 20 && bonus == RES_NUTRIENT && !is_ocean(sq))
                    nut++;
                sc += (i < 20 ? 10 : 5);
            }
            if (tx_goody_at(x2, y2) > 0) {
                sc += (i < 20 ? 25 : 8);
                if (!is_ocean(sq) && (i < 20 || pods.size() < 2))
                    pods.insert({x2, y2});
            }
            if (sq->items & TERRA_FUNGUS) {
                sc -= (i < 20 ? 4 : 1);
            }
        }
    }
    int min_sc = 160 - iter/2;
    bool need_bonus = (!aquatic && conf.nutrient_bonus > is_human(faction));
    debug("find_score %2d | %3d %3d | %d %d %d %d\n", iter, x, y, pods.size(), nut, min_sc, sc);
    if (sc >= min_sc && need_bonus && (int)pods.size() > 1 - iter/50) {
        int n = 0;
        while (!aquatic && pods.size() > 0 && nut + n < 2) {
            Points::const_iterator it(pods.begin());
            std::advance(it, random(pods.size()));
            sq = mapsq(it->first, it->second);
            pods.erase(it);
            sq->items &= ~(TERRA_FUNGUS | TERRA_MINERAL_RES | TERRA_ENERGY_RES);
            sq->items |= (TERRA_SUPPLY_REMOVE | TERRA_BONUS_RES | TERRA_NUTRIENT_RES);
            n++;
        }
        return true;
    }
    return sc >= min_sc && (!need_bonus || nut > 1);
}

HOOK_API void find_start(int faction, int* tx, int* ty) {
    Points spawns;
    bool aquatic = tx_metafactions[faction].rule_flags & FACT_AQUATIC;
    int k = (*map_axis_y < 80 ? 8 : 16);
    if (*map_random_seed != prev_rnd) {
        process_map(k/2);
        prev_rnd = *map_random_seed;
    }
    for (int i=0; i<*total_num_vehicles; i++) {
        VEH* v = &tx_vehicles[i];
        if (v->faction_id != 0 && v->faction_id != faction) {
            spawns.insert({v->x, v->y});
        }
    }
    int z = 0;
    int x = 0;
    int y = 0;
    int i = 0;
    while (++i < 120) {
        if (!aquatic && goodtiles.size() > 0) {
            Points::const_iterator it(goodtiles.begin());
            std::advance(it, random(goodtiles.size()));
            x = it->first;
            y = it->second;
        } else {
            y = (random(*map_axis_y - k) + k/2);
            x = (random(*map_axis_x) &~1) + (y&1);
        }
        int min_range = max(8, *map_area_sq_root/3 - i/5);
        z = point_range(spawns, x, y);
        debug("find_iter %d %d | %3d %3d | %2d %2d\n", faction, i, x, y, min_range, z);
        if (z >= min_range && valid_start(faction, i, x, y, aquatic)) {
            *tx = x;
            *ty = y;
            break;
        }
    }
    tx_site_set(*tx, *ty, tx_world_site(*tx, *ty, 0));
    debug("find_start %d %d %d %d %d\n", faction, i, *tx, *ty, point_range(spawns, *tx, *ty));
    fflush(debug_log);
}

void exit_fail() {
    MessageBoxA(0, "Error while patching game binary. Game will now exit.",
        MOD_VERSION, MB_OK | MB_ICONSTOP);
    exit(EXIT_FAILURE);
}

/*
Replaces old byte sequence with a new byte sequence at address.
Verifies that address contains old values before replacing.
If new_bytes is a null pointer, replace old_bytes with NOP code instead.
*/
void write_bytes(int addr, const byte* old_bytes, const byte* new_bytes, int len) {
    if ((int*)addr < (int*)AC_IMAGE_BASE || (int*)addr + len > (int*)AC_IMAGE_BASE + AC_IMAGE_LEN) {
        exit_fail();
    }
    for (int i=0; i<len; i++) {
        if (*((byte*)addr + i) != old_bytes[i]) {
            debug("write_bytes: old byte doesn't match at address: %08x, value: %02x, expected: %02x",
                addr + i, *((byte*)addr + i), old_bytes[i]);
            exit_fail();
        }
        if (new_bytes != NULL) {
            *((byte*)addr + i) = new_bytes[i];
        } else {
            *((byte*)addr + i) = 0x90;
        }
    }
}

/*
Verify that the location contains standard function prologue before patching.

55          push    ebp
8B EC       mov     ebp, esp
*/
void write_jump(int addr, int func) {
    if ((*(int*)addr & 0xFFFFFF) != 0xEC8B55) {
        exit_fail();
    }
    *(byte*)addr = 0xE9;
    *(int*)(addr + 1) = func - addr - 5;
}

void write_call(int addr, int func) {
    if (*(byte*)addr != 0xE8 || abs(*(int*)(addr+1)) > (int)AC_IMAGE_LEN) {
        exit_fail();
    }
    *(int*)(addr+1) = func - addr - 5;
}

void write_offset(int addr, const void* ofs) {
    if (*(byte*)addr != 0x68 || *(int*)(addr+1) < (int)AC_IMAGE_BASE) {
        exit_fail();
    }
    *(int*)(addr+1) = (int)ofs;
}

void remove_call(int addr) {
    if (*(byte*)addr != 0xE8 || *(int*)(addr+1) + addr < (int)AC_IMAGE_BASE) {
        exit_fail();
    }
    memset((void*)addr, 0x90, 5);
}

/*
Replaces old byte sequence to new byte sequence at address.
Verifies that byte contains old values before replacing.
*/
void write_bytes(int address, int length, byte old_bytes[], byte new_bytes[]) {

    // check address is not out of range
    if ((byte*)address < (byte*)AC_IMAGE_BASE || (byte*)address + length > (byte*)AC_IMAGE_BASE + AC_IMAGE_LEN) {
        throw std::runtime_error("write_bytes: address is out of code range.");
    }

    // write new bytes at address
    for (int i = 0; i < length; i++)
    {
        // verify old byte match value at address
        if (*((byte*)address + i) != old_bytes[i]) {
            debug("write_bytes: old byte doesn't match at address: %p, byte at address: %x, byte expected: %x.", (byte*)address + i, *((byte*)address + i), old_bytes[i])
            throw std::runtime_error("write_bytes: old byte doesn't match.");
        }

        // write new byte
        *((byte*)address + i) = new_bytes[i];

    }

}

// ========================================
// The Will to Power patch procedures
// ========================================

/*
Patch battle compute wrapper.
*/
void patch_battle_compute()
{
    // wrap battle computation into custom function

    write_call(0x0050474C, (int)battle_compute);
    write_call(0x00506EA6, (int)battle_compute);
    write_call(0x005085E0, (int)battle_compute);

    // wrap string concatenation into custom function for battle computation output only

    write_call(0x00422915, (int)battle_compute_compose_value_percentage);

}

/*
Patch read basic rules.
*/
void patch_read_basic_rules()
{
    write_call(0x00587424, (int)read_basic_rules);

}

/*
Ignores reactor power multiplier in combat processing.
*/
void patch_ignore_reactor_power_in_combat_processing()
{
    // Ignore reactor power in combat processing
    // set up both units firepower as if it were psi combat

    // ignore defender reactor power

    int ignore_defender_reactor_power_combat_bytes_length = 1;

    /* jl */
    byte old_ignore_defender_reactor_power_combat_bytes[] = { 0x7C };

    /* jmp */
    byte new_ignore_defender_reactor_power_combat_bytes[] = { 0xEB };

    write_bytes(0x00506F90, ignore_defender_reactor_power_combat_bytes_length, old_ignore_defender_reactor_power_combat_bytes, new_ignore_defender_reactor_power_combat_bytes);

    // ignore attacker reactor power

    int ignore_attacker_reactor_power_combat_bytes_length = 1;

    /* jl */
    byte old_ignore_attacker_reactor_power_combat_bytes[] = { 0x7C };

    /* jmp */
    byte new_ignore_attacker_reactor_power_combat_bytes[] = { 0xEB };

    write_bytes(0x00506FF6, ignore_attacker_reactor_power_combat_bytes_length, old_ignore_attacker_reactor_power_combat_bytes, new_ignore_attacker_reactor_power_combat_bytes);

}

/*
Fixes combat roll formula to match odds.
*/
void patch_combat_roll()
{
    // replace code

    int combat_roll_bytes_length = 0x30;

    /*
    0:  8b 75 ec                mov    esi,DWORD PTR [ebp-0x14]
    3:  8d 46 ff                lea    eax,[esi-0x1]
    6:  85 c0                   test   eax,eax
    8:  7f 04                   jg     0xe
    a:  33 ff                   xor    edi,edi
    c:  eb 0a                   jmp    0x18
    e:  E8 65 CE 13 00          call   <randomizer>
    13: 99                      cdq
    14: f7 fe                   idiv   esi
    16: 8b fa                   mov    edi,edx
    18: 8b 75 e4                mov    esi,DWORD PTR [ebp-0x1c]
    1b: 8d 4e ff                lea    ecx,[esi-0x1]
    1e: 85 c9                   test   ecx,ecx
    20: 7f 04                   jg     0x26
    22: 33 d2                   xor    edx,edx
    24: eb 08                   jmp    0x2e
    26: E8 4D CE 13 00          call   <randomizer>
    2b: 99                      cdq
    2c: f7 fe                   idiv   esi
    2e: 3b fa                   cmp    edi,edx
    */
    byte combat_roll_old_bytes[] =
        { 0x8B, 0x75, 0xEC, 0x8D, 0x46, 0xFF, 0x85, 0xC0, 0x7F, 0x04, 0x33, 0xFF, 0xEB, 0x0A, 0xE8, 0x65, 0xCE, 0x13, 0x00, 0x99, 0xF7, 0xFE, 0x8B, 0xFA, 0x8B, 0x75, 0xE4, 0x8D, 0x4E, 0xFF, 0x85, 0xC9, 0x7F, 0x04, 0x33, 0xD2, 0xEB, 0x08, 0xE8, 0x4D, 0xCE, 0x13, 0x00, 0x99, 0xF7, 0xFE, 0x3B, 0xFA, 0x0F, 0x8E, 0x4E, 0x04, 0x00, 0x00 }
    ;

    /*
    0:  8d bd 10 ff ff ff       lea    edi,[ebp-0xf0]
    6:  57                      push   edi
    7:  8b 7d a4                mov    edi,DWORD PTR [ebp-0x5c]
    a:  57                      push   edi
    b:  8b 7d a0                mov    edi,DWORD PTR [ebp-0x60]
    e:  57                      push   edi
    f:  8b 7d f8                mov    edi,DWORD PTR [ebp-0x8]
    12: 57                      push   edi
    13: 8b 7d fc                mov    edi,DWORD PTR [ebp-0x4]
    16: 57                      push   edi
    17: 8b 7d e4                mov    edi,DWORD PTR [ebp-0x1c]
    1a: 57                      push   edi
    1b: 8b 7d ec                mov    edi,DWORD PTR [ebp-0x14]
    1e: 57                      push   edi
    1f: e8 fc ff ff ff          call   20 <_main+0x20>
    24: 83 c4 1c                add    esp,0x1c
    27: 85 c0                   test   eax,eax
    ...
    */
    byte combat_roll_new_bytes[] =
        { 0x8D, 0xBD, 0x10, 0xFF, 0xFF, 0xFF, 0x57, 0x8B, 0x7D, 0xA4, 0x57, 0x8B, 0x7D, 0xA0, 0x57, 0x8B, 0x7D, 0xF8, 0x57, 0x8B, 0x7D, 0xFC, 0x57, 0x8B, 0x7D, 0xE4, 0x57, 0x8B, 0x7D, 0xEC, 0x57, 0xE8, 0xFC, 0xFF, 0xFF, 0xFF, 0x83, 0xC4, 0x1C, 0x85, 0xC0, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 }
    ;

    write_bytes
    (
        0x005091A5,
        combat_roll_bytes_length,
        combat_roll_old_bytes,
        combat_roll_new_bytes
    )
    ;

    // set call pointer to custom combat_roll function

    write_call(0x005091A5 + 0x1f, (int)combat_roll);

}

/*
Calculates odds.
*/
void patch_calculate_odds()
{
    // replace calculate odds code

    int calculate_odds_bytes_length = 0xC3;

    /*
    0:  8b 5d fc                mov    ebx,DWORD PTR [ebp-0x4]
    3:  83 c4 18                add    esp,0x18
    6:  0f bf 83 32 28 95 00    movsx  eax,WORD PTR [ebx+0x952832]
    d:  8d 0c 40                lea    ecx,[eax+eax*2]
    10: 8d 04 88                lea    eax,[eax+ecx*4]
    13: c1 e0 02                shl    eax,0x2
    16: 80 b8 92 b8 9a 00 0c    cmp    BYTE PTR [eax+0x9ab892],0xc
    1d: 75 07                   jne    0x26
    1f: b8 01 00 00 00          mov    eax,0x1
    24: eb 1c                   jmp    0x42
    26: 33 d2                   xor    edx,edx
    28: 6a 64                   push   0x64
    2a: 8a 90 8f b8 9a 00       mov    dl,BYTE PTR [eax+0x9ab88f]
    30: 6a 01                   push   0x1
    32: 8b c2                   mov    eax,edx
    34: 50                      push   eax
    35: e8 92 ae f1 ff          call   0xfff1aecc
    3a: 8d 04 80                lea    eax,[eax+eax*4]
    3d: 83 c4 0c                add    esp,0xc
    40: d1 e0                   shl    eax,1
    42: 33 c9                   xor    ecx,ecx
    44: 8a 8b 38 28 95 00       mov    cl,BYTE PTR [ebx+0x952838]
    4a: 2b c1                   sub    eax,ecx
    4c: 78 0e                   js     0x5c
    4e: 3d 0f 27 00 00          cmp    eax,0x270f
    53: be 0f 27 00 00          mov    esi,0x270f
    58: 7f 02                   jg     0x5c
    5a: 8b f0                   mov    esi,eax
    5c: 8b 7d f8                mov    edi,DWORD PTR [ebp-0x8]
    5f: 0f af 75 ec             imul   esi,DWORD PTR [ebp-0x14]
    63: 0f bf 87 32 28 95 00    movsx  eax,WORD PTR [edi+0x952832]
    6a: 8d 14 40                lea    edx,[eax+eax*2]
    6d: 8d 04 90                lea    eax,[eax+edx*4]
    70: c1 e0 02                shl    eax,0x2
    73: 80 b8 92 b8 9a 00 0c    cmp    BYTE PTR [eax+0x9ab892],0xc
    7a: 75 07                   jne    0x83
    7c: b8 01 00 00 00          mov    eax,0x1
    81: eb 1c                   jmp    0x9f
    83: 33 c9                   xor    ecx,ecx
    85: 6a 64                   push   0x64
    87: 8a 88 8f b8 9a 00       mov    cl,BYTE PTR [eax+0x9ab88f]
    8d: 6a 01                   push   0x1
    8f: 8b c1                   mov    eax,ecx
    91: 50                      push   eax
    92: e8 35 ae f1 ff          call   0xfff1aecc
    97: 8d 04 80                lea    eax,[eax+eax*4]
    9a: 83 c4 0c                add    esp,0xc
    9d: d1 e0                   shl    eax,1
    9f: 33 d2                   xor    edx,edx
    a1: 8a 97 38 28 95 00       mov    dl,BYTE PTR [edi+0x952838]
    a7: 2b c2                   sub    eax,edx
    a9: 78 12                   js     0xbd
    ab: 3d 0f 27 00 00          cmp    eax,0x270f
    b0: 7e 07                   jle    0xb9
    b2: bf 0f 27 00 00          mov    edi,0x270f
    b7: eb 06                   jmp    0xbf
    b9: 8b f8                   mov    edi,eax
    bb: eb 02                   jmp    0xbf
    bd: 33 ff                   xor    edi,edi
    bf: 0f af 7d e4             imul   edi,DWORD PTR [ebp-0x1c]
    */
    byte calculate_odds_old_bytes[] =
        { 0x8B, 0x5D, 0xFC, 0x83, 0xC4, 0x18, 0x0F, 0xBF, 0x83, 0x32, 0x28, 0x95, 0x00, 0x8D, 0x0C, 0x40, 0x8D, 0x04, 0x88, 0xC1, 0xE0, 0x02, 0x80, 0xB8, 0x92, 0xB8, 0x9A, 0x00, 0x0C, 0x75, 0x07, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xEB, 0x1C, 0x33, 0xD2, 0x6A, 0x64, 0x8A, 0x90, 0x8F, 0xB8, 0x9A, 0x00, 0x6A, 0x01, 0x8B, 0xC2, 0x50, 0xE8, 0x92, 0xAE, 0xF1, 0xFF, 0x8D, 0x04, 0x80, 0x83, 0xC4, 0x0C, 0xD1, 0xE0, 0x33, 0xC9, 0x8A, 0x8B, 0x38, 0x28, 0x95, 0x00, 0x2B, 0xC1, 0x78, 0x0E, 0x3D, 0x0F, 0x27, 0x00, 0x00, 0xBE, 0x0F, 0x27, 0x00, 0x00, 0x7F, 0x02, 0x8B, 0xF0, 0x8B, 0x7D, 0xF8, 0x0F, 0xAF, 0x75, 0xEC, 0x0F, 0xBF, 0x87, 0x32, 0x28, 0x95, 0x00, 0x8D, 0x14, 0x40, 0x8D, 0x04, 0x90, 0xC1, 0xE0, 0x02, 0x80, 0xB8, 0x92, 0xB8, 0x9A, 0x00, 0x0C, 0x75, 0x07, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xEB, 0x1C, 0x33, 0xC9, 0x6A, 0x64, 0x8A, 0x88, 0x8F, 0xB8, 0x9A, 0x00, 0x6A, 0x01, 0x8B, 0xC1, 0x50, 0xE8, 0x35, 0xAE, 0xF1, 0xFF, 0x8D, 0x04, 0x80, 0x83, 0xC4, 0x0C, 0xD1, 0xE0, 0x33, 0xD2, 0x8A, 0x97, 0x38, 0x28, 0x95, 0x00, 0x2B, 0xC2, 0x78, 0x12, 0x3D, 0x0F, 0x27, 0x00, 0x00, 0x7E, 0x07, 0xBF, 0x0F, 0x27, 0x00, 0x00, 0xEB, 0x06, 0x8B, 0xF8, 0xEB, 0x02, 0x33, 0xFF, 0x0F, 0xAF, 0x7D, 0xE4 }
    ;

    /*
    0:  83 c4 18                add    esp,0x18
    3:  89 e0                   mov    eax,esp
    5:  83 e8 04                sub    eax,0x4
    8:  50                      push   eax
    9:  89 e0                   mov    eax,esp
    b:  83 e8 04                sub    eax,0x4
    e:  50                      push   eax
    f:  8b 7d e4                mov    edi,DWORD PTR [ebp-0x1c]
    12: 57                      push   edi
    13: 8b 75 ec                mov    esi,DWORD PTR [ebp-0x14]
    16: 56                      push   esi
    17: 8b 7d f8                mov    edi,DWORD PTR [ebp-0x8]
    1a: 57                      push   edi
    1b: 8b 5d fc                mov    ebx,DWORD PTR [ebp-0x4]
    1e: 53                      push   ebx
    1f: e8 fc ff ff ff          call   20 <_main+0x20>
    24: 83 c4 10                add    esp,0x10
    27: 5e                      pop    esi
    28: 5f                      pop    edi
    ...
    */
    byte calculate_odds_new_bytes[] =
        { 0x83, 0xC4, 0x18, 0x89, 0xE0, 0x83, 0xE8, 0x04, 0x50, 0x89, 0xE0, 0x83, 0xE8, 0x04, 0x50, 0x8B, 0x7D, 0xE4, 0x57, 0x8B, 0x75, 0xEC, 0x56, 0x8B, 0x7D, 0xF8, 0x57, 0x8B, 0x5D, 0xFC, 0x53, 0xE8, 0xFC, 0xFF, 0xFF, 0xFF, 0x83, 0xC4, 0x10, 0x5E, 0x5F, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 }
    ;

    write_bytes
    (
        0x00508034,
        calculate_odds_bytes_length,
        calculate_odds_old_bytes,
        calculate_odds_new_bytes
    )
    ;

    // set call pointer to custom calculate_odds function

    write_call(0x00508034 + 0x1f, (int)calculate_odds);

}

/*
Disables alien guaranteed technologies assignment.
*/
void patch_alien_guaranteed_technologies()
{
    // replace jz by !ALIEN condition with jmp unconditional

    int bytes_length = 1;

    /* jz */
    byte old_bytes[] = { 0x74 };

    /* jmp */
    byte new_bytes[] = { 0xEB };

    write_bytes(0x005B29F8, bytes_length, old_bytes, new_bytes);

}

/*
Assigns alternative base values for weapon icon selection algorithm.
*/
void patch_weapon_icon_selection_algorithm()
{
    // load weapon ID instead of weapon value for icon selection

    int load_weapon_value_bytes_length = 0xC;

    /*
    shl    eax,0x4
    cmp    esi,ebx
    movsx  ecx,BYTE PTR [eax+0x94ae68]
    */
    byte old_load_weapon_value_bytes[] =
        { 0xC1, 0xE0, 0x04, 0x3B, 0xF3, 0x0F, 0xBE, 0x88, 0x68, 0xAE, 0x94, 0x00 }
    ;

    /*
    mov    ecx,eax
    cmp    esi,ebx
    shl    eax,0x4
    nop
    nop
    nop
    nop
    nop
    */
    byte new_load_weapon_value_bytes[] =
        { 0x89, 0xC1, 0x39, 0xDE, 0xC1, 0xE0, 0x04, 0x90, 0x90, 0x90, 0x90, 0x90 }
    ;

    write_bytes
    (
        0x004C2E15,
        load_weapon_value_bytes_length,
        old_load_weapon_value_bytes,
        new_load_weapon_value_bytes
    )
    ;

    // rearrange switch table to match weapon ID instead of weapon value

    int weapon_icon_selection_table_bytes_length = 0x8D;

    /*
    or      eax, 0xFFFFFFFF
    push    ebx
    mov     DWORD PTR [ebp-0x68], eax
    mov     DWORD PTR [ebp-0x64], eax
    mov     eax, 2
    push    esi
    mov     DWORD PTR [ebp-0x50], eax
    mov     DWORD PTR [ebp-0x4C], eax
    mov     eax, 4
    push    edi
    mov     DWORD PTR [ebp-0x48], eax
    mov     DWORD PTR [ebp-0x44], eax
    mov     eax, 9
    mov     ecx, 8
    mov     DWORD PTR [ebp-0x18], eax
    mov     DWORD PTR [ebp-0x14], eax
    mov     DWORD PTR [ebp-0x10], eax
    mov     DWORD PTR [ebp-0xC], eax
    mov     eax, [ebp+0xC]
    xor     ebx, ebx
    mov     edx, 5
    mov     edi, 7
    cmp     eax, ecx
    mov     DWORD PTR [ebp-0x60], ebx
    mov     DWORD PTR [ebp-0x5C], ebx
    mov     DWORD PTR [ebp-0x58], 1
    mov     DWORD PTR [ebp-0x54], 3
    mov     DWORD PTR [ebp-0x40], edx
    mov     DWORD PTR [ebp-0x3C], edx
    mov     DWORD PTR [ebp-0x38], 6
    mov     DWORD PTR [ebp-0x34], edi
    mov     DWORD PTR [ebp-0x30], edi
    mov     DWORD PTR [ebp-0x2C], edi
    mov     DWORD PTR [ebp-0x28], ecx
    mov     DWORD PTR [ebp-0x24], ecx
    mov     DWORD PTR [ebp-0x20], ecx
    mov     DWORD PTR [ebp-0x1C], ecx
    mov     DWORD PTR [ebp-0x8], 0xA
    mov     DWORD PTR [ebp-0x4], 0xD
    */
    byte old_weapon_icon_selection_table_bytes[] =
        { 0x83, 0xC8, 0xFF, 0x53, 0x89, 0x45, 0x98, 0x89, 0x45, 0x9C, 0xB8, 0x02, 0x00, 0x00, 0x00, 0x56, 0x89, 0x45, 0xB0, 0x89, 0x45, 0xB4, 0xB8, 0x04, 0x00, 0x00, 0x00, 0x57, 0x89, 0x45, 0xB8, 0x89, 0x45, 0xBC, 0xB8, 0x09, 0x00, 0x00, 0x00, 0xB9, 0x08, 0x00, 0x00, 0x00, 0x89, 0x45, 0xE8, 0x89, 0x45, 0xEC, 0x89, 0x45, 0xF0, 0x89, 0x45, 0xF4, 0x8B, 0x45, 0x0C, 0x33, 0xDB, 0xBA, 0x05, 0x00, 0x00, 0x00, 0xBF, 0x07, 0x00, 0x00, 0x00, 0x3B, 0xC1, 0x89, 0x5D, 0xA0, 0x89, 0x5D, 0xA4, 0xC7, 0x45, 0xA8, 0x01, 0x00, 0x00, 0x00, 0xC7, 0x45, 0xAC, 0x03, 0x00, 0x00, 0x00, 0x89, 0x55, 0xC0, 0x89, 0x55, 0xC4, 0xC7, 0x45, 0xC8, 0x06, 0x00, 0x00, 0x00, 0x89, 0x7D, 0xCC, 0x89, 0x7D, 0xD0, 0x89, 0x7D, 0xD4, 0x89, 0x4D, 0xD8, 0x89, 0x4D, 0xDC, 0x89, 0x4D, 0xE0, 0x89, 0x4D, 0xE4, 0xC7, 0x45, 0xF8, 0x0A, 0x00, 0x00, 0x00, 0xC7, 0x45, 0xFC, 0x0D, 0x00, 0x00, 0x00 }
    ;

    /*
    or     eax,0xffffffff
    mov    DWORD PTR [ebp-0x38],eax
    mov    DWORD PTR [ebp-0x34],eax
    mov    DWORD PTR [ebp-0x2c],eax
    mov    DWORD PTR [ebp-0x28],eax
    mov    DWORD PTR [ebp-0x24],eax
    mov    DWORD PTR [ebp-0x20],eax
    mov    DWORD PTR [ebp-0x1c],eax
    mov    DWORD PTR [ebp-0x18],eax
    mov    DWORD PTR [ebp-0x14],eax
    mov    DWORD PTR [ebp-0x10],eax
    mov    DWORD PTR [ebp-0xc],eax
    mov    DWORD PTR [ebp-0x8],eax
    mov    DWORD PTR [ebp-0x4],eax
    mov    DWORD PTR [ebp-0x68],eax
    inc    eax
    mov    DWORD PTR [ebp-0x64],eax
    inc    eax
    mov    DWORD PTR [ebp-0x60],eax
    inc    eax
    mov    DWORD PTR [ebp-0x58],eax
    inc    eax
    mov    DWORD PTR [ebp-0x5c],eax
    inc    eax
    mov    DWORD PTR [ebp-0x54],eax
    inc    eax
    mov    DWORD PTR [ebp-0x50],eax
    inc    eax
    mov    DWORD PTR [ebp-0x4c],eax
    inc    eax
    mov    DWORD PTR [ebp-0x48],eax
    inc    eax
    mov    DWORD PTR [ebp-0x44],eax
    inc    eax
    mov    DWORD PTR [ebp-0x40],eax
    inc    eax
    mov    DWORD PTR [ebp-0x3c],eax
    mov    DWORD PTR [ebp-0x30],0xd
    push   ebx
    push   esi
    push   edi
    xor    ebx,ebx
    mov    edx,0x5
    mov    edi,0x7
    mov    eax,DWORD PTR [ebp+0xc]
    mov    ecx,0x8
    cmp    eax,ecx
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    */
    byte new_weapon_icon_selection_table_bytes[] =
        { 0x83, 0xC8, 0xFF, 0x89, 0x45, 0xC8, 0x89, 0x45, 0xCC, 0x89, 0x45, 0xD4, 0x89, 0x45, 0xD8, 0x89, 0x45, 0xDC, 0x89, 0x45, 0xE0, 0x89, 0x45, 0xE4, 0x89, 0x45, 0xE8, 0x89, 0x45, 0xEC, 0x89, 0x45, 0xF0, 0x89, 0x45, 0xF4, 0x89, 0x45, 0xF8, 0x89, 0x45, 0xFC, 0x89, 0x45, 0x98, 0x40, 0x89, 0x45, 0x9C, 0x40, 0x89, 0x45, 0xA0, 0x40, 0x89, 0x45, 0xA8, 0x40, 0x89, 0x45, 0xA4, 0x40, 0x89, 0x45, 0xAC, 0x40, 0x89, 0x45, 0xB0, 0x40, 0x89, 0x45, 0xB4, 0x40, 0x89, 0x45, 0xB8, 0x40, 0x89, 0x45, 0xBC, 0x40, 0x89, 0x45, 0xC0, 0x40, 0x89, 0x45, 0xC4, 0xC7, 0x45, 0xD0, 0x0D, 0x00, 0x00, 0x00, 0x53, 0x56, 0x57, 0x31, 0xDB, 0xBA, 0x05, 0x00, 0x00, 0x00, 0xBF, 0x07, 0x00, 0x00, 0x00, 0x8B, 0x45, 0x0C, 0xB9, 0x08, 0x00, 0x00, 0x00, 0x39, 0xC8, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 }
    ;

    write_bytes
    (
        0x004C2CF6,
        weapon_icon_selection_table_bytes_length,
        old_weapon_icon_selection_table_bytes,
        new_weapon_icon_selection_table_bytes
    )
    ;

}

/*
Enables alternative prototype cost formula.
*/
void patch_alternative_prototype_cost_formula()
{
    write_call(0x00436ADD, (int)proto_cost);
    // this call special purposed to calculate prototype cost, not unit cost
//	write_call(0x0043704C, (int)proto_cost);
	write_call(0x005817C9, (int)proto_cost);
    write_call(0x00581833, (int)proto_cost);
    write_call(0x00581BB3, (int)proto_cost);
    write_call(0x00581BCB, (int)proto_cost);
    write_call(0x00582339, (int)proto_cost);
    write_call(0x00582359, (int)proto_cost);
    write_call(0x00582378, (int)proto_cost);
    write_call(0x00582398, (int)proto_cost);
    write_call(0x005823B0, (int)proto_cost);
    write_call(0x00582482, (int)proto_cost);
    write_call(0x0058249A, (int)proto_cost);
    write_call(0x0058254A, (int)proto_cost);
    write_call(0x005827E4, (int)proto_cost);
    write_call(0x00582EC5, (int)proto_cost);
    write_call(0x00582FEC, (int)proto_cost);
    write_call(0x005A5D35, (int)proto_cost);
    write_call(0x005A5F15, (int)proto_cost);

}

/*
Applies flat hurry cost mechanics.
*/
void patch_flat_hurry_cost()
{
	// BaseWin__hurry

    int flat_hurry_cost_bytes_length = 0x59;

/*
0:  2b ce                   sub    ecx,esi
2:  3b d7                   cmp    edx,edi
4:  7c 1b                   jl     0x21
6:  8b d1                   mov    edx,ecx
8:  b8 67 66 66 66          mov    eax,0x66666667
d:  0f af d1                imul   edx,ecx
10: f7 ea                   imul   edx
12: c1 fa 03                sar    edx,0x3
15: 8b c2                   mov    eax,edx
17: c1 e8 1f                shr    eax,0x1f
1a: 03 d0                   add    edx,eax
1c: 8d 3c 4a                lea    edi,[edx+ecx*2]
1f: eb 03                   jmp    0x24
21: 8d 3c 09                lea    edi,[ecx+ecx*1]
24: 83 7d e4 46             cmp    DWORD PTR [ebp-0x1c],0x46
28: 7c 25                   jl     0x4f
2a: a1 30 ea 90 00          mov    eax,ds:0x90ea30
2f: 33 c9                   xor    ecx,ecx
31: 6a ff                   push   0xffffffff
33: 6a 01                   push   0x1
35: 8a 48 04                mov    cl,BYTE PTR [eax+0x4]
38: 8b 70 40                mov    esi,DWORD PTR [eax+0x40]
3b: 51                      push   ecx
3c: 03 ff                   add    edi,edi
3e: e8 35 b4 0c 00          call   0xcb478
43: c1 e0 02                shl    eax,0x2
46: 83 c4 0c                add    esp,0xc
49: 3b f0                   cmp    esi,eax
4b: 7d 02                   jge    0x4f
4d: 03 ff                   add    edi,edi
4f: 3b 35 30 98 94 00       cmp    esi,DWORD PTR ds:0x949830
55: 7d 02                   jge    0x59
57: 03 ff                   add    edi,edi
*/
    byte flat_hurry_cost_bytes_old[] =
        { 0x2B, 0xCE, 0x3B, 0xD7, 0x7C, 0x1B, 0x8B, 0xD1, 0xB8, 0x67, 0x66, 0x66, 0x66, 0x0F, 0xAF, 0xD1, 0xF7, 0xEA, 0xC1, 0xFA, 0x03, 0x8B, 0xC2, 0xC1, 0xE8, 0x1F, 0x03, 0xD0, 0x8D, 0x3C, 0x4A, 0xEB, 0x03, 0x8D, 0x3C, 0x09, 0x83, 0x7D, 0xE4, 0x46, 0x7C, 0x25, 0xA1, 0x30, 0xEA, 0x90, 0x00, 0x33, 0xC9, 0x6A, 0xFF, 0x6A, 0x01, 0x8A, 0x48, 0x04, 0x8B, 0x70, 0x40, 0x51, 0x03, 0xFF, 0xE8, 0x35, 0xB4, 0x0C, 0x00, 0xC1, 0xE0, 0x02, 0x83, 0xC4, 0x0C, 0x3B, 0xF0, 0x7D, 0x02, 0x03, 0xFF, 0x3B, 0x35, 0x30, 0x98, 0x94, 0x00, 0x7D, 0x02, 0x03, 0xFF }
    ;

/*
0:  e8 fd ff ff ff          call   2 <_main+0x2>
5:  89 c7                   mov    edi,eax
...
*/
    byte flat_hurry_cost_bytes_new[] =
        { 0xE8, 0xFD, 0xFF, 0xFF, 0xFF, 0x89, 0xC7, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 }
    ;

    write_bytes
    (
        0x00418FB8,
        flat_hurry_cost_bytes_length,
        flat_hurry_cost_bytes_old,
        flat_hurry_cost_bytes_new
    )
    ;

    write_call(0x00418FB8, (int)modifiedHurryCost);

	// display additional hurry information

	write_call(0x004192A5, (int)displayHurryCostScaledToBasicMineralCostMultiplierInformation);
	write_call(0x004192FF, (int)displayPartialHurryCostToCompleteNextTurnInformation);

}

/*
Enables alternative upgrade cost formula.
*/
void patch_alternative_upgrade_cost_formula()
{
    write_call(0x004D0ECF, (int)upgrade_cost);
    write_call(0x004D16D9, (int)upgrade_cost);
    write_call(0x004EFB76, (int)upgrade_cost);
    write_call(0x004EFEB9, (int)upgrade_cost);

}

/*
Sets Perimeter Defense and Tachyon Field defense bonus.
*/
void patch_defensive_structures_bonus(int perimeter_defense_multiplier, int tachyon_field_bonus)
{
    int perimeter_defense_multiplier_bytes_length = 0x5;

    /*
    mov     esi, 4
    */
    byte old_perimeter_defense_multiplier_bytes[] =
        { 0xBE, 0x04, 0x00, 0x00, 0x00 }
    ;

    /*
    mov     esi, ?
    */
    byte new_perimeter_defense_multiplier_bytes[] =
        { 0xBE, (byte)perimeter_defense_multiplier, 0x00, 0x00, 0x00 }
    ;

    write_bytes
    (
        0x005034B9,
        perimeter_defense_multiplier_bytes_length,
        old_perimeter_defense_multiplier_bytes,
        new_perimeter_defense_multiplier_bytes
    )
    ;

    int tachyon_field_bonus_bytes_length = 0x3;

    /*
    add     esi, 2
    */
    byte old_tachyon_field_bonus_bytes[] =
        { 0x83, 0xC6, 0x02 }
    ;

    /*
    add     esi, ?
    */
    byte new_tachyon_field_bonus_bytes[] =
        { 0x83, 0xC6, (byte)tachyon_field_bonus }
    ;

    write_bytes
    (
        0x00503506,
        tachyon_field_bonus_bytes_length,
        old_tachyon_field_bonus_bytes,
        new_tachyon_field_bonus_bytes
    )
    ;

}

/*
Uses defender reactor power for collateral damage computation.
*/
void patch_collateral_damage_defender_reactor()
{
    int collateral_damage_defender_reactor_bytes_length = 0x2;

    /*
    jge     short loc_50AA9F
    */
    byte old_collateral_damage_defender_reactor_bytes[] =
        { 0x7D, 0x16 }
    ;

    /*
    nop
    nop
    */
    byte new_collateral_damage_defender_reactor_bytes[] =
        { 0x90, 0x90 }
    ;

    write_bytes
    (
        0x0050AA87,
        collateral_damage_defender_reactor_bytes_length,
        old_collateral_damage_defender_reactor_bytes,
        new_collateral_damage_defender_reactor_bytes
    )
    ;

}

/*
Sets collateral damage value.
*/
void patch_collateral_damage_value(int collateral_damage_value)
{
    int collateral_damage_value_bytes_length = 0x2;

    /*
    mov     dl, 3
    */
    byte old_collateral_damage_value_bytes[] =
        { 0xB2, 0x03 }
    ;

    /*
    mov     dl, ?
    */
    byte new_collateral_damage_value_bytes[] =
        { 0xB2, (byte)collateral_damage_value }
    ;

    write_bytes
    (
        0x0050AAA5,
        collateral_damage_value_bytes_length,
        old_collateral_damage_value_bytes,
        new_collateral_damage_value_bytes
    )
    ;

}

/*
Disables AQUATIC bonus minerals.
*/
void patch_disable_aquatic_bonus_minerals()
{
    int disable_aquatic_bonus_minerals_bytes_length = 0x1;

    /*
    inc     esi
    */
    byte old_disable_aquatic_bonus_minerals_bytes[] =
        { 0x46 }
    ;

    /*
    nop
    */
    byte new_disable_aquatic_bonus_minerals_bytes[] =
        { 0x90 }
    ;

    write_bytes
    (
        0x004E7604,
        disable_aquatic_bonus_minerals_bytes_length,
        old_disable_aquatic_bonus_minerals_bytes,
        new_disable_aquatic_bonus_minerals_bytes
    )
    ;

}

/*
Following methods modify repair rates.
*/
void patch_repair_minimal(int repair_minimal)
{
    int repair_minimal_bytes_length = 0x7;

    /*
    0:  c7 45 fc 01 00 00 00    mov    DWORD PTR [ebp-0x4],0x1
    */
    byte repair_minimal_bytes_old[] =
        { 0xC7, 0x45, 0xFC, 0x01, 0x00, 0x00, 0x00 }
    ;

    /*
    0:  c7 45 fc 01 00 00 00    mov    DWORD PTR [ebp-0x4],<repair_minimal>
    */
    byte repair_minimal_bytes_new[] =
        { 0xC7, 0x45, 0xFC, (byte)repair_minimal, 0x00, 0x00, 0x00 }
    ;

    write_bytes
    (
        0x00526200,
        repair_minimal_bytes_length,
        repair_minimal_bytes_old,
        repair_minimal_bytes_new
    )
    ;

}
void patch_repair_fungus(int repair_fungus)
{
    int repair_fungus_bytes_length = 0x7;

    /*
    0:  c7 45 fc 02 00 00 00    mov    DWORD PTR [ebp-0x4],0x2
    */
    byte repair_fungus_bytes_old[] =
        { 0xC7, 0x45, 0xFC, 0x02, 0x00, 0x00, 0x00 }
    ;

    /*
    0:  c7 45 fc 02 00 00 00    mov    DWORD PTR [ebp-0x4],<repair_fungus>
    */
    byte repair_fungus_bytes_new[] =
        { 0xC7, 0x45, 0xFC, (byte)repair_fungus, 0x00, 0x00, 0x00 }
    ;

    write_bytes
    (
        0x00526261,
        repair_fungus_bytes_length,
        repair_fungus_bytes_old,
        repair_fungus_bytes_new
    )
    ;

}
void patch_repair_friendly()
{
    int repair_friendly_bytes_length = 0x3;

    /*
    0:  ff 45 fc                inc    DWORD PTR [ebp-0x4]
    */
    byte repair_friendly_bytes_old[] =
        { 0xFF, 0x45, 0xFC }
    ;

    /*
    ...
    */
    byte repair_friendly_bytes_new[] =
        { 0x90, 0x90, 0x90 }
    ;

    write_bytes
    (
        0x00526284,
        repair_friendly_bytes_length,
        repair_friendly_bytes_old,
        repair_friendly_bytes_new
    )
    ;

}
void patch_repair_airbase()
{
    int repair_airbase_bytes_length = 0x3;

    /*
    0:  ff 45 fc                inc    DWORD PTR [ebp-0x4]
    */
    byte repair_airbase_bytes_old[] =
        { 0xFF, 0x45, 0xFC }
    ;

    /*
    ...
    */
    byte repair_airbase_bytes_new[] =
        { 0x90, 0x90, 0x90 }
    ;

    write_bytes
    (
        0x005262D4,
        repair_airbase_bytes_length,
        repair_airbase_bytes_old,
        repair_airbase_bytes_new
    )
    ;

}
void patch_repair_bunker()
{
    int repair_bunker_bytes_length = 0x3;

    /*
    0:  ff 45 fc                inc    DWORD PTR [ebp-0x4]
    */
    byte repair_bunker_bytes_old[] =
        { 0xFF, 0x45, 0xFC }
    ;

    /*
    ...
    */
    byte repair_bunker_bytes_new[] =
        { 0x90, 0x90, 0x90 }
    ;

    write_bytes
    (
        0x005262F5,
        repair_bunker_bytes_length,
        repair_bunker_bytes_old,
        repair_bunker_bytes_new
    )
    ;

}
void patch_repair_base(int repair_base)
{
    int repair_base_bytes_length = 0x10;

    /*
    0:  8b 45 fc                mov    eax,DWORD PTR [ebp-0x4]
    3:  66 8b 8e 32 28 95 00    mov    cx,WORD PTR [esi+0x952832]
    a:  40                      inc    eax
    b:  33 d2                   xor    edx,edx
    d:  89 45 fc                mov    DWORD PTR [ebp-0x4],eax
    */
    byte repair_base_bytes_old[] =
        { 0x8B, 0x45, 0xFC, 0x66, 0x8B, 0x8E, 0x32, 0x28, 0x95, 0x00, 0x40, 0x33, 0xD2, 0x89, 0x45, 0xFC }
    ;

    /*
    0:  66 8b 8e 32 28 95 00    mov    cx,WORD PTR [esi+0x952832]
    7:  31 d2                   xor    edx,edx
    9:  83 45 fc 01             add    DWORD PTR [ebp-0x4],0x1
    ...
    */
    byte repair_base_bytes_new[] =
        { 0x66, 0x8B, 0x8E, 0x32, 0x28, 0x95, 0x00, 0x31, 0xD2, 0x83, 0x45, 0xFC, (byte)repair_base, 0x90, 0x90, 0x90 }
    ;

    write_bytes
    (
        0x0052632E,
        repair_base_bytes_length,
        repair_base_bytes_old,
        repair_base_bytes_new
    )
    ;

}
void patch_repair_base_native(int repair_base_native)
{
    int repair_base_native_bytes_length = 0xB;

    /*
    0:  33 c9                   xor    ecx,ecx
    2:  8a 8e 38 28 95 00       mov    cl,BYTE PTR [esi+0x952838]
    8:  89 4d fc                mov    DWORD PTR [ebp-0x4],ecx
    */
    byte repair_base_native_bytes_old[] =
        { 0x33, 0xC9, 0x8A, 0x8E, 0x38, 0x28, 0x95, 0x00, 0x89, 0x4D, 0xFC }
    ;

    /*
    0:  83 45 fc 01             add    DWORD PTR [ebp-0x4],<repair_base_native>
    ...
    */
    byte repair_base_native_bytes_new[] =
        { 0x83, 0x45, 0xFC, (byte)repair_base_native, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 }
    ;

    write_bytes
    (
        0x00526376,
        repair_base_native_bytes_length,
        repair_base_native_bytes_old,
        repair_base_native_bytes_new
    )
    ;

}
void patch_repair_base_facility(int repair_base_facility)
{
    int repair_base_facility_bytes_length = 0xB;

    /*
    0:  33 d2                   xor    edx,edx
    2:  8a 96 38 28 95 00       mov    dl,BYTE PTR [esi+0x952838]
    8:  89 55 fc                mov    DWORD PTR [ebp-0x4],edx
    */
    byte repair_base_facility_bytes_old[] =
        { 0x33, 0xD2, 0x8A, 0x96, 0x38, 0x28, 0x95, 0x00, 0x89, 0x55, 0xFC }
    ;

    /*
    0:  83 45 fc 01             add    DWORD PTR [ebp-0x4],<repair_base_facility>
    */
    byte repair_base_facility_bytes_new[] =
        { 0x83, 0x45, 0xFC, (byte)repair_base_facility, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 }
    ;

    write_bytes
    (
        0x00526422,
        repair_base_facility_bytes_length,
        repair_base_facility_bytes_old,
        repair_base_facility_bytes_new
    )
    ;

}
void patch_repair_nano_factory(int repair_nano_factory)
{
    int repair_nano_factory_bytes_length = 0xB;

    /*
    0:  33 c9                   xor    ecx,ecx
    2:  8a 8e 38 28 95 00       mov    cl,BYTE PTR [esi+0x952838]
    8:  89 4d fc                mov    DWORD PTR [ebp-0x4],ecx
    */
    byte repair_nano_factory_bytes_old[] =
        { 0x33, 0xC9, 0x8A, 0x8E, 0x38, 0x28, 0x95, 0x00, 0x89, 0x4D, 0xFC }
    ;

    /*
    0:  83 45 fc 01             add    DWORD PTR [ebp-0x4],<repair_nano_factory>
    */
    byte repair_nano_factory_bytes_new[] =
        { 0x83, 0x45, 0xFC, (byte)repair_nano_factory, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 }
    ;

    write_bytes
    (
        0x00526540,
        repair_nano_factory_bytes_length,
        repair_nano_factory_bytes_old,
        repair_nano_factory_bytes_new
    )
    ;

}

/*
Disables planetpearls collection and corresponding message.
*/
void patch_disable_planetpearls()
{
    // remove planetpearls calculation code (effectively sets its value to 0)

    int disable_planetpearls_bytes_length = 0x1C;

    /*
    0:  8b 45 08                mov    eax,DWORD PTR [ebp+0x8]
    3:  6a 00                   push   0x0
    5:  6a 01                   push   0x1
    7:  50                      push   eax
    8:  e8 46 ad 0b 00          call   0xbad53
    d:  83 c4 0c                add    esp,0xc
    10: 40                      inc    eax
    11: 8d 0c 80                lea    ecx,[eax+eax*4]
    14: 8b 06                   mov    eax,DWORD PTR [esi]
    16: d1 e1                   shl    ecx,1
    18: 03 c1                   add    eax,ecx
    1a: 89 06                   mov    DWORD PTR [esi],eax
    */
    byte disable_planetpearls_bytes_old[] =
        { 0x8B, 0x45, 0x08, 0x6A, 0x00, 0x6A, 0x01, 0x50, 0xE8, 0x46, 0xAD, 0x0B, 0x00, 0x83, 0xC4, 0x0C, 0x40, 0x8D, 0x0C, 0x80, 0x8B, 0x06, 0xD1, 0xE1, 0x03, 0xC1, 0x89, 0x06 }
    ;

    /*
    ...
    */
    byte disable_planetpearls_bytes_new[] =
        { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 }
    ;

    write_bytes
    (
        0x005060ED,
        disable_planetpearls_bytes_length,
        disable_planetpearls_bytes_old,
        disable_planetpearls_bytes_new
    )
    ;

}

/*
Makes promotions probabilities uniform for all levels.
*/
void patch_uniform_promotions()
{
    // enable factor of 1/2 for Very Green, Green, Regular

    int halve_promotion_chances_for_all_levels_bytes_length = 0x2;

    /*
    0:  7e 17                   jle    0x19
    */
    byte halve_promotion_chances_for_all_levels_bytes_old[] =
        { 0x7E, 0x17 }
    ;

    /*
    ...
    */
    byte halve_promotion_chances_for_all_levels_bytes_new[] =
        { 0x90, 0x90 }
    ;

    write_bytes
    (
        0x00506342,
        halve_promotion_chances_for_all_levels_bytes_length,
        halve_promotion_chances_for_all_levels_bytes_old,
        halve_promotion_chances_for_all_levels_bytes_new
    )
    ;

    // disable attacker immediate promotion for Very Green, Green

    int disable_attacker_immediate_promotion_bytes_length = 0x2;

    /*
    0:  7e 21                   jle    0x23
    */
    byte disable_attacker_immediate_promotion_bytes_old[] =
        { 0x7E, 0x21 }
    ;

    /*
    ...
    */
    byte disable_attacker_immediate_promotion_bytes_new[] =
        { 0x90, 0x90 }
    ;

    write_bytes
    (
        0x0050A2B1,
        disable_attacker_immediate_promotion_bytes_length,
        disable_attacker_immediate_promotion_bytes_old,
        disable_attacker_immediate_promotion_bytes_new
    )
    ;

    // disable defender immediate promotion for Very Green, Green

    int disable_defender_immediate_promotion_bytes_length = 0x2;

    /*
    0:  7e 3a                   jle    0x3c
    */
    byte disable_defender_immediate_promotion_bytes_old[] =
        { 0x7E, 0x3A }
    ;

    /*
    ...
    */
    byte disable_defender_immediate_promotion_bytes_new[] =
        { 0x90, 0x90 }
    ;

    write_bytes
    (
        0x0050A3ED,
        disable_defender_immediate_promotion_bytes_length,
        disable_defender_immediate_promotion_bytes_old,
        disable_defender_immediate_promotion_bytes_new
    )
    ;

    // unify attacker_1 reactor power multiplier

    int unify_attacker_1_reactor_power_multiplier_bytes_length = 0x6;

    /*
    0:  8a 81 8f b8 9a 00       mov    al,BYTE PTR [ecx+0x9ab88f]
    */
    byte unify_attacker_1_reactor_power_multiplier_bytes_old[] =
        { 0x8A, 0x81, 0x8F, 0xB8, 0x9A, 0x00 }
    ;

    /*
    0:  b0 01                   mov    al,0x1
    ...
    */
    byte unify_attacker_1_reactor_power_multiplier_bytes_new[] =
        { 0xB0, 0x01, 0x90, 0x90, 0x90, 0x90 }
    ;

    write_bytes
    (
        0x0050A223,
        unify_attacker_1_reactor_power_multiplier_bytes_length,
        unify_attacker_1_reactor_power_multiplier_bytes_old,
        unify_attacker_1_reactor_power_multiplier_bytes_new
    )
    ;

    // unify attacker_2 reactor power multiplier

    int unify_attacker_2_reactor_power_multiplier_bytes_length = 0x6;

    /*
    0:  8a 90 8f b8 9a 00       mov    dl,BYTE PTR [eax+0x9ab88f]
    */
    byte unify_attacker_2_reactor_power_multiplier_bytes_old[] =
        { 0x8A, 0x90, 0x8F, 0xB8, 0x9A, 0x00 }
    ;

    /*
    0:  b2 01                   mov    dl,0x1
    ...
    */
    byte unify_attacker_2_reactor_power_multiplier_bytes_new[] =
        { 0xB2, 0x01, 0x90, 0x90, 0x90, 0x90 }
    ;

    write_bytes
    (
        0x0050A283,
        unify_attacker_2_reactor_power_multiplier_bytes_length,
        unify_attacker_2_reactor_power_multiplier_bytes_old,
        unify_attacker_2_reactor_power_multiplier_bytes_new
    )
    ;

    // unify defender_1 reactor power multiplier

    int unify_defender_1_reactor_power_multiplier_bytes_length = 0x6;

    /*
    0:  8a 90 8f b8 9a 00       mov    dl,BYTE PTR [eax+0x9ab88f]
    */
    byte unify_defender_1_reactor_power_multiplier_bytes_old[] =
        { 0x8A, 0x90, 0x8F, 0xB8, 0x9A, 0x00 }
    ;

    /*
    0:  b2 01                   mov    dl,0x1
    ...
    */
    byte unify_defender_1_reactor_power_multiplier_bytes_new[] =
        { 0xB2, 0x01, 0x90, 0x90, 0x90, 0x90 }
    ;

    write_bytes
    (
        0x0050A341,
        unify_defender_1_reactor_power_multiplier_bytes_length,
        unify_defender_1_reactor_power_multiplier_bytes_old,
        unify_defender_1_reactor_power_multiplier_bytes_new
    )
    ;

    // unify defender_2 reactor power multiplier

    int unify_defender_2_reactor_power_multiplier_bytes_length = 0x6;

    /*
    0:  8a 90 8f b8 9a 00       mov    dl,BYTE PTR [eax+0x9ab88f]
    */
    byte unify_defender_2_reactor_power_multiplier_bytes_old[] =
        { 0x8A, 0x90, 0x8F, 0xB8, 0x9A, 0x00 }
    ;

    /*
    0:  b2 01                   mov    dl,0x1
    ...
    */
    byte unify_defender_2_reactor_power_multiplier_bytes_new[] =
        { 0xB2, 0x01, 0x90, 0x90, 0x90, 0x90 }
    ;

    write_bytes
    (
        0x0050A3BF,
        unify_defender_2_reactor_power_multiplier_bytes_length,
        unify_defender_2_reactor_power_multiplier_bytes_old,
        unify_defender_2_reactor_power_multiplier_bytes_new
    )
    ;

}

/*
Removes Very Green on defense morale bonus.
*/
void patch_very_green_no_defense_bonus()
{
    // very_green_no_defense_bonus

    int very_green_no_defense_bonus_bytes_length = 0x2;

    /*
    0:  7c 0c                   jl     0xe
    */
    byte very_green_no_defense_bonus_bytes_old[] =
        { 0x7C, 0x0C }
    ;

    /*
    ...
    */
    byte very_green_no_defense_bonus_bytes_new[] =
        { 0x90, 0x90 }
    ;

    write_bytes
    (
        0x00501C06,
        very_green_no_defense_bonus_bytes_length,
        very_green_no_defense_bonus_bytes_old,
        very_green_no_defense_bonus_bytes_new
    )
    ;

    // very_green_no_defense_bonus_display

    int very_green_no_defense_bonus_display_bytes_length = 0x2;

    /*
    0:  75 10                   jne    0x12
    */
    byte very_green_no_defense_bonus_display_bytes_old[] =
        { 0x75, 0x10 }
    ;

    /*
    0:  75 10                   jmp    0x12
    */
    byte very_green_no_defense_bonus_display_bytes_new[] =
        { 0xEB, 0x10 }
    ;

    write_bytes
    (
        0x004B4327,
        very_green_no_defense_bonus_display_bytes_length,
        very_green_no_defense_bonus_display_bytes_old,
        very_green_no_defense_bonus_display_bytes_new
    )
    ;

}

/*
Makes sea base territory distance same as land one.
*/
void patch_sea_territory_distance_same_as_land()
{
    // sea_territory_distance_same_as_land

    int sea_territory_distance_same_as_land_bytes_length = 0x2;

    /*
    0:  d1 fa                   sar    edx,1
    */
    byte sea_territory_distance_same_as_land_bytes_old[] =
        { 0xD1, 0xFA }
    ;

    /*
    ...
    */
    byte sea_territory_distance_same_as_land_bytes_new[] =
        { 0x90, 0x90 }
    ;

    write_bytes
    (
        0x00523F01,
        sea_territory_distance_same_as_land_bytes_length,
        sea_territory_distance_same_as_land_bytes_old,
        sea_territory_distance_same_as_land_bytes_new
    )
    ;

}

/*
Makes coastal base territory distance same as sea one.
*/
void patch_coastal_territory_distance_same_as_sea()
{
    // wrap base_find3

    write_call(0x00523ED7, (int)base_find3);

}

/*
Alternative artillery damage.
*/
void patch_alternative_artillery_damage()
{
    // alternative_artillery_damage

    int alternative_artillery_damage_bytes_length = 0x35;

    /*
    0:  99                      cdq
    1:  f7 f9                   idiv   ecx
    3:  40                      inc    eax
    4:  83 f8 01                cmp    eax,0x1
    7:  7c 12                   jl     0x1b
    9:  3d e7 03 00 00          cmp    eax,0x3e7
    e:  7e 07                   jle    0x17
    10: be e7 03 00 00          mov    esi,0x3e7
    15: eb 09                   jmp    0x20
    17: 8b f0                   mov    esi,eax
    19: eb 05                   jmp    0x20
    1b: be 01 00 00 00          mov    esi,0x1
    20: 8d 4e ff                lea    ecx,[esi-0x1]
    23: 85 c9                   test   ecx,ecx
    25: 7f 04                   jg     0x2b
    27: 33 ff                   xor    edi,edi
    29: eb 0a                   jmp    0x35
    2b: e8 d7 d9 13 00          call   0x13da07
    30: 99                      cdq
    31: f7 fe                   idiv   esi
    33: 8b fa                   mov    edi,edx
    */
    byte alternative_artillery_damage_bytes_old[] =
        { 0x99, 0xF7, 0xF9, 0x40, 0x83, 0xF8, 0x01, 0x7C, 0x12, 0x3D, 0xE7, 0x03, 0x00, 0x00, 0x7E, 0x07, 0xBE, 0xE7, 0x03, 0x00, 0x00, 0xEB, 0x09, 0x8B, 0xF0, 0xEB, 0x05, 0xBE, 0x01, 0x00, 0x00, 0x00, 0x8D, 0x4E, 0xFF, 0x85, 0xC9, 0x7F, 0x04, 0x33, 0xFF, 0xEB, 0x0A, 0xE8, 0xD7, 0xD9, 0x13, 0x00, 0x99, 0xF7, 0xFE, 0x8B, 0xFA }
    ;

    /*
    0:  ff 75 90                push   DWORD PTR [ebp-0x70]
    3:  51                      push   ecx
    4:  50                      push   eax
    5:  e8 fc ff ff ff          call   6 <_main+0x6>
    a:  83 c4 0c                add    esp,0xc
    d:  89 c7                   mov    edi,eax
    ...
    */
    byte alternative_artillery_damage_bytes_new[] =
        { 0xFF, 0x75, 0x90, 0x51, 0x50, 0xE8, 0xFC, 0xFF, 0xFF, 0xFF, 0x83, 0xC4, 0x0C, 0x89, 0xC7, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 }
    ;

    write_bytes
    (
        0x00508616,
        alternative_artillery_damage_bytes_length,
        alternative_artillery_damage_bytes_old,
        alternative_artillery_damage_bytes_new
    )
    ;

    // custom artillery damage generator

    write_call(0x00508616 + 0x5, (int)roll_artillery_damage);

}

/*
Disables home base Children Creche morale bonus.
Disables home base Brood Pit morale bonus.
*/
void patch_disable_home_base_cc_morale_bonus()
{
    int disable_home_base_cc_morale_bonus_bytes_length = 0x2;
    byte disable_home_base_cc_morale_bonus_bytes_old[] = { 0xF7, 0xDA };
    byte disable_home_base_cc_morale_bonus_bytes_new[] = { 0x31, 0xD2 };

    write_bytes
    (
        0x005C1037,
        disable_home_base_cc_morale_bonus_bytes_length,
        disable_home_base_cc_morale_bonus_bytes_old,
        disable_home_base_cc_morale_bonus_bytes_new
    )
    ;

    int disable_home_base_bp_morale_bonus_bytes_length = 0x2;
    byte disable_home_base_bp_morale_bonus_bytes_old[] = { 0xF7, 0xD9 };
    byte disable_home_base_bp_morale_bonus_bytes_new[] = { 0x31, 0xC9 };

    write_bytes
    (
        0x005C1073,
        disable_home_base_bp_morale_bonus_bytes_length,
        disable_home_base_bp_morale_bonus_bytes_old,
        disable_home_base_bp_morale_bonus_bytes_new
    )
    ;

}

/*
Disables current base Children Creche morale bonus.
Disables current base Brood Pit morale bonus.
*/
void patch_disable_current_base_cc_morale_bonus()
{
    // disable_current_base_cc_morale_bonus display

    int disable_current_base_cc_morale_bonus_display_bytes_length = 0x2;
    byte disable_current_base_cc_morale_bonus_display_bytes_old[] = { 0xF7, 0xDA };
    byte disable_current_base_cc_morale_bonus_display_bytes_new[] = { 0x31, 0xD2 };

    write_bytes
    (
        0x004B4144,
        disable_current_base_cc_morale_bonus_display_bytes_length,
        disable_current_base_cc_morale_bonus_display_bytes_old,
        disable_current_base_cc_morale_bonus_display_bytes_new
    )
    ;

    // disable_current_base_bp_morale_bonus display

    int disable_current_base_bp_morale_bonus_display1_bytes_length = 0x2;
    byte disable_current_base_bp_morale_bonus_display1_bytes_old[] = { 0xF7, 0xDA };
    byte disable_current_base_bp_morale_bonus_display1_bytes_new[] = { 0x31, 0xD2 };

    write_bytes
    (
        0x004B41CA,
        disable_current_base_bp_morale_bonus_display1_bytes_length,
        disable_current_base_bp_morale_bonus_display1_bytes_old,
        disable_current_base_bp_morale_bonus_display1_bytes_new
    )
    ;

    int disable_current_base_bp_morale_bonus_display2_bytes_length = 0x2;
    byte disable_current_base_bp_morale_bonus_display2_bytes_old[] = { 0xF7, 0xD8 };
    byte disable_current_base_bp_morale_bonus_display2_bytes_new[] = { 0x31, 0xC0 };

    write_bytes
    (
        0x004B425D,
        disable_current_base_bp_morale_bonus_display2_bytes_length,
        disable_current_base_bp_morale_bonus_display2_bytes_old,
        disable_current_base_bp_morale_bonus_display2_bytes_new
    )
    ;

    // disable_current_base_cc_morale_bonus attack

    int disable_current_base_cc_morale_bonus_attack_bytes_length = 0x2;
    byte disable_current_base_cc_morale_bonus_attack_bytes_old[] = { 0xF7, 0xD9 };
    byte disable_current_base_cc_morale_bonus_attack_bytes_new[] = { 0x31, 0xC9 };

    write_bytes
    (
        0x00501669,
        disable_current_base_cc_morale_bonus_attack_bytes_length,
        disable_current_base_cc_morale_bonus_attack_bytes_old,
        disable_current_base_cc_morale_bonus_attack_bytes_new
    )
    ;

    // disable_current_base_bp_morale_bonus attack

    int disable_current_base_bp_morale_bonus_attack1_bytes_length = 0x2;
    byte disable_current_base_bp_morale_bonus_attack1_bytes_old[] = { 0xF7, 0xD8 };
    byte disable_current_base_bp_morale_bonus_attack1_bytes_new[] = { 0x31, 0xC0 };

    write_bytes
    (
        0x005016D4,
        disable_current_base_bp_morale_bonus_attack1_bytes_length,
        disable_current_base_bp_morale_bonus_attack1_bytes_old,
        disable_current_base_bp_morale_bonus_attack1_bytes_new
    )
    ;

    int disable_current_base_bp_morale_bonus_attack2_bytes_length = 0x2;
    byte disable_current_base_bp_morale_bonus_attack2_bytes_old[] = { 0xF7, 0xD8 };
    byte disable_current_base_bp_morale_bonus_attack2_bytes_new[] = { 0x31, 0xC0 };

    write_bytes
    (
        0x00501747,
        disable_current_base_bp_morale_bonus_attack2_bytes_length,
        disable_current_base_bp_morale_bonus_attack2_bytes_old,
        disable_current_base_bp_morale_bonus_attack2_bytes_new
    )
    ;

    // disable_current_base_cc_morale_bonus defense

    int disable_current_base_cc_morale_bonus_defense_bytes_length = 0x2;
    byte disable_current_base_cc_morale_bonus_defense_bytes_old[] = { 0xF7, 0xD9 };
    byte disable_current_base_cc_morale_bonus_defense_bytes_new[] = { 0x31, 0xC9 };

    write_bytes
    (
        0x005019F1,
        disable_current_base_cc_morale_bonus_defense_bytes_length,
        disable_current_base_cc_morale_bonus_defense_bytes_old,
        disable_current_base_cc_morale_bonus_defense_bytes_new
    )
    ;

    // disable_current_base_bp_morale_bonus defense

    int disable_current_base_bp_morale_bonus_defense1_bytes_length = 0x2;
    byte disable_current_base_bp_morale_bonus_defense1_bytes_old[] = { 0xF7, 0xD8 };
    byte disable_current_base_bp_morale_bonus_defense1_bytes_new[] = { 0x31, 0xC0 };

    write_bytes
    (
        0x00501A55,
        disable_current_base_bp_morale_bonus_defense1_bytes_length,
        disable_current_base_bp_morale_bonus_defense1_bytes_old,
        disable_current_base_bp_morale_bonus_defense1_bytes_new
    )
    ;

    int disable_current_base_bp_morale_bonus_defense2_bytes_length = 0x2;
    byte disable_current_base_bp_morale_bonus_defense2_bytes_old[] = { 0xF7, 0xD8 };
    byte disable_current_base_bp_morale_bonus_defense2_bytes_new[] = { 0x31, 0xC0 };

    write_bytes
    (
        0x00501AC8,
        disable_current_base_bp_morale_bonus_defense2_bytes_length,
        disable_current_base_bp_morale_bonus_defense2_bytes_old,
        disable_current_base_bp_morale_bonus_defense2_bytes_new
    )
    ;

}

/*
Sets default unit morale to Very Green.
*/
void patch_default_morale_very_green()
{
    // default_morale_very_green place 1

    int default_morale_very_green_1_bytes_length = 0x1;

    /*
    0:  46                      inc    esi
    */
    byte default_morale_very_green_1_bytes_old[] =
        { 0x46 }
    ;

    /*
    ...
    */
    byte default_morale_very_green_1_bytes_new[] =
        { 0x90 }
    ;

    write_bytes
    (
        0x004F13A6,
        default_morale_very_green_1_bytes_length,
        default_morale_very_green_1_bytes_old,
        default_morale_very_green_1_bytes_new
    )
    ;

    // default_morale_very_green place 2

    int default_morale_very_green_2_bytes_length = 0x2;

    /*
    0:  fe c2                   inc    dl
    */
    byte default_morale_very_green_2_bytes_old[] =
        { 0xFE, 0xC2 }
    ;

    /*
    ...
    */
    byte default_morale_very_green_2_bytes_new[] =
        { 0x90, 0x90 }
    ;

    write_bytes
    (
        0x005C039C,
        default_morale_very_green_2_bytes_length,
        default_morale_very_green_2_bytes_old,
        default_morale_very_green_2_bytes_new
    )
    ;

}

/*
Patch tile yield calculation.
*/
void patch_tile_yield()
{
	// nutrient yield
	write_call(0x00465DD6, (int)mod_nutrient_yield);
	write_call(0x004B6C44, (int)mod_nutrient_yield);
	write_call(0x004BCEEB, (int)mod_nutrient_yield);
	write_call(0x004E7DE4, (int)mod_nutrient_yield);
	write_call(0x004E7F04, (int)mod_nutrient_yield);
	write_call(0x004E8034, (int)mod_nutrient_yield);
// this call is already patched by Thinker
// Thinker code will delegate to mod_nutrient_yield instead of original function
//	write_call(0x004E888C, (int)mod_nutrient_yield);
	write_call(0x004E96F4, (int)mod_nutrient_yield);
	write_call(0x004ED7F1, (int)mod_nutrient_yield);
	write_call(0x00565878, (int)mod_nutrient_yield);

	// mineral yield
	write_call(0x004B6E4F, (int)mod_mineral_yield);
	write_call(0x004B6EF9, (int)mod_mineral_yield);
	write_call(0x004B6F84, (int)mod_mineral_yield);
	write_call(0x004E7E00, (int)mod_mineral_yield);
	write_call(0x004E7F14, (int)mod_mineral_yield);
	write_call(0x004E8044, (int)mod_mineral_yield);
	write_call(0x004E88AC, (int)mod_mineral_yield);
	write_call(0x004E970A, (int)mod_mineral_yield);

	// energy yield
	write_call(0x004B7028, (int)mod_energy_yield);
	write_call(0x004B7136, (int)mod_energy_yield);
	write_call(0x004D3E5C, (int)mod_energy_yield);
	write_call(0x004E7E1C, (int)mod_energy_yield);
	write_call(0x004E7F24, (int)mod_energy_yield);
	write_call(0x004E8054, (int)mod_energy_yield);
	write_call(0x004E88CA, (int)mod_energy_yield);
	write_call(0x004E971F, (int)mod_energy_yield);
	write_call(0x0056C856, (int)mod_energy_yield);

}

/*
SE GROWTH/INDUSTRY change accumulated nutrients/minerals proportionally.
*/
void patch_se_accumulated_resource_adjustment()
{
    // SE set upon SE dialog close
    write_call(0x004AF0F6, (int)se_accumulated_resource_adjustment);

}

/*
Patch hex_cost.
*/
void patch_hex_cost()
{
    write_call(0x00467711, (int)hex_cost);
    write_call(0x00572518, (int)hex_cost);
    write_call(0x005772D7, (int)hex_cost);
    write_call(0x005776F4, (int)hex_cost);
    write_call(0x00577E2C, (int)hex_cost);
    write_call(0x00577F0E, (int)hex_cost);
    write_call(0x00597695, (int)hex_cost);
    write_call(0x0059ACA4, (int)hex_cost);
    write_call(0x0059B61A, (int)hex_cost);
    write_call(0x0059C105, (int)hex_cost);

}

/*
Displays additional base population info in F4 screen.
*/
void patch_display_base_population_info()
{
    write_call(0x0049DCB1, (int)sayBase);

}

/*
Hooks base init computation.
This is for The Planetary Transit System patch.
*/
void patch_base_init()
{
    write_call(0x004C9870, (int)baseInit);
    write_call(0x005AF926, (int)baseInit);
    write_call(0x005B2BB8, (int)baseInit);

}

/*
HSA does not kill probe.
*/
void patch_hsa_does_not_kill_probe()
{
//	// disable HSA killing probe
//
//	int disable_hsa_kill_bytes_length = 12;
//	byte disable_hsa_kill_bytes_old[] = { 0x8B, 0x4D, 0x08, 0x51, 0xE8, 0x11, 0x14, 0x02, 0x00, 0x83, 0xC4, 0x04 };
//	byte disable_hsa_kill_bytes_new[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
//	write_bytes(0x0059F6E6, disable_hsa_kill_bytes_length, disable_hsa_kill_bytes_old, disable_hsa_kill_bytes_new);
//
	// HSA does not kill probe but exhausts its movement points instead.

    write_call(0x0059F6EA, (int)tx_veh_skip);

}

/*
Exclude defensive faciliites from probe target lists.
*/
void patch_probe_not_destroy_defense()
{
	// reusing logic for FAC_PRESSURE_DOME to determine whether base is at sea
	// now when this logic is gone there is no choice but to exclude it too

	// exclude defensive facilities and FAC_PRESSURE_DOME from probe random list

	int exclude_defensive_facilities_random_bytes_length = 51;
	byte exclude_defensive_facilities_random_bytes_old[] = { 0x83, 0xFF, 0x1A, 0x75, 0x2E, 0x0F, 0xBF, 0x8E, 0x42, 0xD0, 0x97, 0x00, 0x0F, 0xAF, 0x0D, 0xF0, 0xFA, 0x68, 0x00, 0x0F, 0xBF, 0x96, 0x40, 0xD0, 0x97, 0x00, 0xD1, 0xFA, 0x03, 0xCA, 0x8B, 0x15, 0x0C, 0xA3, 0x94, 0x00, 0x6B, 0xC9, 0x2C, 0x33, 0xC0, 0x8A, 0x04, 0x11, 0x24, 0xE0, 0x83, 0xF8, 0x60, 0x7C, 0x34 };
	byte exclude_defensive_facilities_random_bytes_new[] = { 0x83, 0xFF, 0x1A, 0x74, 0x62, 0x83, 0xFF, 0x04, 0x74, 0x5D, 0x83, 0xFF, 0x05, 0x74, 0x58, 0x83, 0xFF, 0x1C, 0x74, 0x53, 0x83, 0xFF, 0x1D, 0x74, 0x4E, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
	write_bytes
	(
		0x005A06C8,
		exclude_defensive_facilities_random_bytes_length,
		exclude_defensive_facilities_random_bytes_old,
		exclude_defensive_facilities_random_bytes_new
	);

	// exclude defensive facilities and FAC_PRESSURE_DOME from probe selected list

	int exclude_defensive_facilities_selected_bytes_length = 54;
	byte exclude_defensive_facilities_selected_bytes_old[] = { 0x83, 0xFF, 0x1A, 0x75, 0x31, 0x0F, 0xBF, 0x86, 0x42, 0xD0, 0x97, 0x00, 0x0F, 0xAF, 0x05, 0xF0, 0xFA, 0x68, 0x00, 0x0F, 0xBF, 0x8E, 0x40, 0xD0, 0x97, 0x00, 0xD1, 0xF9, 0x03, 0xC1, 0x8B, 0x0D, 0x0C, 0xA3, 0x94, 0x00, 0x6B, 0xC0, 0x2C, 0x33, 0xD2, 0x8A, 0x14, 0x08, 0x83, 0xE2, 0xE0, 0x83, 0xFA, 0x60, 0x7C, 0x56, 0xEB, 0x05 };
	byte exclude_defensive_facilities_selected_bytes_new[] = { 0x83, 0xFF, 0x1A, 0x0F, 0x84, 0x81, 0x00, 0x00, 0x00, 0x83, 0xFF, 0x04, 0x0F, 0x84, 0x78, 0x00, 0x00, 0x00, 0x83, 0xFF, 0x05, 0x0F, 0x84, 0x6F, 0x00, 0x00, 0x00, 0x83, 0xFF, 0x1C, 0x0F, 0x84, 0x66, 0x00, 0x00, 0x00, 0x83, 0xFF, 0x1D, 0x0F, 0x84, 0x5D, 0x00, 0x00, 0x00, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
	write_bytes
	(
		0x005A0920,
		exclude_defensive_facilities_selected_bytes_length,
		exclude_defensive_facilities_selected_bytes_old,
		exclude_defensive_facilities_selected_bytes_new
	);

}

/*
Hooks itoa call in display ability routine.
This is to expand single number packed ability cost into human readable text.
*/
void patch_help_ability_cost_text()
{
	// in Datalinks

    write_call(0x0042EF7A, (int)getAbilityCostText);

    // in workshop

    write_call(0x0043B202, (int)appendAbilityCostTextInWorkshop);
    write_call(0x0043B29D, (int)appendAbilityCostTextInWorkshop);
    write_call(0x0043B334, (int)appendAbilityCostTextInWorkshop);
    write_call(0x0043B168, (int)appendAbilityCostTextInWorkshop);
    write_call(0x0043B417, (int)appendAbilityCostTextInWorkshop);
    write_call(0x0043B4FC, (int)appendAbilityCostTextInWorkshop);
    write_call(0x0043B5E1, (int)appendAbilityCostTextInWorkshop);
    write_call(0x0043B6A7, (int)appendAbilityCostTextInWorkshop);

}

/*
Disabes Cloning Vats impunities.
*/
void patch_cloning_vats_impunities()
{
	// disable cloning vats impunities display

	int disable_cv_display_bytes_length = 5;
	byte disable_cv_display_bytes_old[] = { 0xA1, 0x74, 0x65, 0x9A, 0x00 };
	byte disable_cv_display_bytes_new[] = { 0xB8, 0xFF, 0xFF, 0xFF, 0xFF };
	write_bytes(0x004AF514, disable_cv_display_bytes_length, disable_cv_display_bytes_old, disable_cv_display_bytes_new);
	write_bytes(0x004AF686, disable_cv_display_bytes_length, disable_cv_display_bytes_old, disable_cv_display_bytes_new);

	// disable cloning vats impunities computation

	int disable_cv_compute_bytes_length = 6;
	byte disable_cv_compute_bytes_old[] = { 0x8B, 0x0D, 0x74, 0x65, 0x9A, 0x00 };
	byte disable_cv_compute_bytes_new[] = { 0xB9, 0xFF, 0xFF, 0xFF, 0xFF, 0x90 };
	write_bytes(0x005B4225, disable_cv_compute_bytes_length, disable_cv_compute_bytes_old, disable_cv_compute_bytes_new);
	write_bytes(0x005B434D, disable_cv_compute_bytes_length, disable_cv_compute_bytes_old, disable_cv_compute_bytes_new);

}

/*
Wraps social_calc.
This is initially to enable CV GROWTH effect.
*/
void patch_social_calc()
{
    write_call(0x004AEC6E, (int)modifiedSocialCalc); // sets SE_pending values
    write_call(0x004AEC45, (int)modifiedSocialCalc);
    write_call(0x004AECC4, (int)modifiedSocialCalc); // sets SE_pending values
    write_call(0x004AEE4B, (int)modifiedSocialCalc);
    write_call(0x004AEE74, (int)modifiedSocialCalc);
    write_call(0x004AEECD, (int)modifiedSocialCalc); // resets SE_pending values
    // this particular call is already used by se_accumulated_resource_adjustment function
    // se_accumulated_resource_adjustment will call modifiedSocialCalc instead of tx_social_calc
//    write_call(0x004AF0F6, (int)modifiedSocialCalc);
    write_call(0x004B25B6, (int)modifiedSocialCalc);
    write_call(0x004B25DF, (int)modifiedSocialCalc);
    write_call(0x004B2635, (int)modifiedSocialCalc);
    write_call(0x005B4515, (int)modifiedSocialCalc);
    write_call(0x005B4527, (int)modifiedSocialCalc); // sets SE values
    write_call(0x005B4539, (int)modifiedSocialCalc); // sets SE_2 values
    write_call(0x005B464B, (int)modifiedSocialCalc);
    write_call(0x005B4AE1, (int)modifiedSocialCalc);

}

/*
Patches cloning vats mechanics.
*/
void patch_cloning_vats_mechanics()
{
	// disable triggering population boom

	int disable_cv_population_boom_bytes_length = 1;
	byte disable_cv_population_boom_bytes_old[] = {0x7C};
	byte disable_cv_population_boom_bytes_new[] = {0xEB};
	write_bytes
	(
		0x004EF389,
		disable_cv_population_boom_bytes_length,
		disable_cv_population_boom_bytes_old,
		disable_cv_population_boom_bytes_new
	);

	// CV GROWTH effect is set in modifiedSocialCalc

}

/*
Modifies SE GROWTH rating cap.
*/
void patch_se_growth_rating_cap(int se_growth_rating_cap)
{
	// patch population boom condition in base_growth

	int popboom_condition_base_growth_bytes_length = 7;
	byte popboom_condition_base_growth_bytes_old[] = { 0x83, 0x3D, 0x18, 0xE9, 0x90, 0x00, 0x06 };
	byte popboom_condition_base_growth_bytes_new[] = { 0x83, 0x3D, 0x18, 0xE9, 0x90, 0x00, (byte)(se_growth_rating_cap + 1) };
	write_bytes
	(
		0x004EF3A8,
		popboom_condition_base_growth_bytes_length,
		popboom_condition_base_growth_bytes_old,
		popboom_condition_base_growth_bytes_new
	);

	// patch population boom condition in base_doctors

	int popboom_condition_base_doctors_bytes_length = 7;
	byte popboom_condition_base_doctors_bytes_old[] = { 0x83, 0x3D, 0x18, 0xE9, 0x90, 0x00, 0x06 };
	byte popboom_condition_base_doctors_bytes_new[] = { 0x83, 0x3D, 0x18, 0xE9, 0x90, 0x00, (byte)(se_growth_rating_cap + 1) };
	write_bytes
	(
		0x004F6070,
		popboom_condition_base_doctors_bytes_length,
		popboom_condition_base_doctors_bytes_old,
		popboom_condition_base_doctors_bytes_new
	);

	// patch cost_factor nutrient cap

	int cost_factor_nutrient_cap_check_bytes_length = 3;
	byte cost_factor_nutrient_cap_check_bytes_old[] = { 0x83, 0xFB, 0x05 };
	byte cost_factor_nutrient_cap_check_bytes_new[] = { 0x83, 0xFB, (byte)se_growth_rating_cap };
	write_bytes
	(
		0x004E4577,
		cost_factor_nutrient_cap_check_bytes_length,
		cost_factor_nutrient_cap_check_bytes_old,
		cost_factor_nutrient_cap_check_bytes_new
	);

	int cost_factor_nutrient_cap_set_bytes_length = 5;
	byte cost_factor_nutrient_cap_set_bytes_old[] = { 0xB8, 0x05, 0x00, 0x00, 0x00 };
	byte cost_factor_nutrient_cap_set_bytes_new[] = { 0xB8, (byte)se_growth_rating_cap, 0x00, 0x00, 0x00 };
	write_bytes
	(
		0x004E457C,
		cost_factor_nutrient_cap_set_bytes_length,
		cost_factor_nutrient_cap_set_bytes_old,
		cost_factor_nutrient_cap_set_bytes_new
	);

}

/*
Wraps _strcat call in base nutrient display to reflect correct population boom growth turns.
*/
void patch_growth_turns_population_boom()
{
	write_call(0x004118BE, (int)correctGrowthTurnsForPopulationBoom);

}

/*
Wraps has_fac call in base_minerals to sneak in RT mineral increase.
*/
void patch_recycling_tank_minerals()
{
	write_call(0x004E9E68, (int)modifiedRecyclingTanksMinerals);

}

/*
Modifies free minerals.
*/
void patch_free_minerals(int freeMinerals)
{
	int free_minerals_bytes_length = 3;
	byte free_minerals_bytes_old[] = { 0x83, 0xC6, 0x10 };
	byte free_minerals_bytes_new[] = { 0x83, 0xC6, (byte)freeMinerals };
	write_bytes
	(
		0x004E9E41,
		free_minerals_bytes_length,
		free_minerals_bytes_old,
		free_minerals_bytes_new
	);

}

/*
Fixes population box incorrectly drawn superdrones.
*/
void patch_base_scren_population_superdrones()
{
	int base_screen_population_superdrones_human_bytes_length = 6;
	byte base_screen_population_superdrones_human_bytes_old[] = { 0x2B, 0x8B, 0x20, 0x01, 0x00, 0x00 };
	byte base_screen_population_superdrones_human_bytes_new[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
	write_bytes
	(
		0x00414ED6,
		base_screen_population_superdrones_human_bytes_length,
		base_screen_population_superdrones_human_bytes_old,
		base_screen_population_superdrones_human_bytes_new
	);

	int base_screen_population_superdrones_alien_bytes_length = 6;
	byte base_screen_population_superdrones_alien_bytes_old[] = { 0x2B, 0x8A, 0x20, 0x01, 0x00, 0x00 };
	byte base_screen_population_superdrones_alien_bytes_new[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
	write_bytes
	(
		0x00414D43,
		base_screen_population_superdrones_alien_bytes_length,
		base_screen_population_superdrones_alien_bytes_old,
		base_screen_population_superdrones_alien_bytes_new
	);

}

/*
Equally distant territory is given to base with smaller ID (presumably older one).
*/
void patch_conflicting_territory()
{
	int conflicting_territory_bytes_length = 2;
	byte conflicting_territory_bytes_old[] = { 0x7F, 0x09 };
	byte conflicting_territory_bytes_new[] = { 0x7D, 0x09 };
	write_bytes
	(
		0x004E3EAE,
		conflicting_territory_bytes_length,
		conflicting_territory_bytes_old,
		conflicting_territory_bytes_new
	);

}

/*
Modifies abundance of native life.
*/
void patch_native_life_frequency(int constant, int multiplier)
{
	// check allowed values

	if (constant < 0 || constant > 255 || !(multiplier == 2 || multiplier == 3 || multiplier == 5))
		return;

	// calculate multiplier code

	int multiplierCode;
	switch (multiplier)
	{
	case 2:
		multiplierCode = 0x00;
		break;
	case 3:
		multiplierCode = 0x40;
		break;
	case 5:
		multiplierCode = 0x80;
		break;
	default:
		return;
	}

	// modify number tries for natives appearance

	int native_life_multiplier1_bytes_length = 4;
	byte native_life_multiplier1_bytes_old[] = { 0x8D, 0x54, 0x09, 0x02 };
	byte native_life_multiplier1_bytes_new[] = { 0x8D, 0x54, (byte)(multiplierCode + 0x09), (byte)constant };
	write_bytes
	(
		0x00522C02,
		native_life_multiplier1_bytes_length,
		native_life_multiplier1_bytes_old,
		native_life_multiplier1_bytes_new
	);
	int native_life_multiplier2_bytes_length = 4;
	byte native_life_multiplier2_bytes_old[] = { 0x8D, 0x4C, 0x00, 0x02 };
	byte native_life_multiplier2_bytes_new[] = { 0x8D, 0x4C, (byte)(multiplierCode + 0x00), (byte)constant };
	write_bytes
	(
		0x00522C13,
		native_life_multiplier2_bytes_length,
		native_life_multiplier2_bytes_old,
		native_life_multiplier2_bytes_new
	);

}

/*
Makes sea native appear every turn.
*/
void patch_native_life_sea_creatures()
{
	int native_life_sea_ratio1_bytes_length = 5;
	byte native_life_sea_ratio1_bytes_old[] = { 0xBE, 0x05, 0x00, 0x00, 0x00 };
	byte native_life_sea_ratio1_bytes_new[] = { 0xBE, 0x01, 0x00, 0x00, 0x00 };
	write_bytes
	(
		0x0052261A,
		native_life_sea_ratio1_bytes_length,
		native_life_sea_ratio1_bytes_old,
		native_life_sea_ratio1_bytes_new
	);
	int native_life_sea_ratio2_bytes_length = 2;
	byte native_life_sea_ratio2_bytes_old[] = { 0x2B, 0xF2 };
	byte native_life_sea_ratio2_bytes_new[] = { 0x90, 0x90 };
	write_bytes
	(
		0x00522621,
		native_life_sea_ratio2_bytes_length,
		native_life_sea_ratio2_bytes_old,
		native_life_sea_ratio2_bytes_new
	);
	int native_life_sea_ratio3_bytes_length = 2;
	byte native_life_sea_ratio3_bytes_old[] = { 0xD1, 0xE6 };
	byte native_life_sea_ratio3_bytes_new[] = { 0x90, 0x90 };
	write_bytes
	(
		0x00522624,
		native_life_sea_ratio3_bytes_length,
		native_life_sea_ratio3_bytes_old,
		native_life_sea_ratio3_bytes_new
	);

}

/*
Disable native sudden death.
*/
void patch_native_disable_sudden_death()
{
	int native_life_die_ratio_bytes_length = 3;
	byte native_life_die_ratio_bytes_old[] = { 0xF6, 0xC2, 0x07 };
	byte native_life_die_ratio_bytes_new[] = { 0xF6, 0xC2, 0xFF };
	write_bytes
	(
		0x00566E2E,
		native_life_die_ratio_bytes_length,
		native_life_die_ratio_bytes_old,
		native_life_die_ratio_bytes_new
	);

}

/*
Modified inefficiency computation.
*/
void patch_alternative_inefficiency()
{
	write_call(0x004EB5AE, (int)modifiedInefficiency);
	write_call(0x004EB93C, (int)modifiedInefficiency);

}

/*
Wraps setup_player adding custom functionality.
*/
void patch_setup_player()
{
	write_call(0x00525CC7, (int)modifiedSetupPlayer);
	write_call(0x005A3C9B, (int)modifiedSetupPlayer);
	write_call(0x005B341C, (int)modifiedSetupPlayer);
	write_call(0x005B3C03, (int)modifiedSetupPlayer);
	write_call(0x005B3C4C, (int)modifiedSetupPlayer);

}

/*
Wraps balance to correctly generate number of colonies at start.
*/
void patch_balance()
{
	write_call(0x005B41F5, (int)modifiedBalance);

}

/*
Enables building Recycling Tanks even if Pressure Dome presents.
*/
void patch_recycling_tanks_not_disabled_by_pressure_dome()
{
	int recycling_tanks_disabled_by_pressure_dome_bytes_length = 3;
	byte recycling_tanks_disabled_by_pressure_dome_bytes_old[] = { 0x83, 0xFB, 0x03 };
	byte recycling_tanks_disabled_by_pressure_dome_bytes_new[] = { 0x83, 0xFB, 0xFF };
	write_bytes
	(
		0x005BA436,
		recycling_tanks_disabled_by_pressure_dome_bytes_length,
		recycling_tanks_disabled_by_pressure_dome_bytes_old,
		recycling_tanks_disabled_by_pressure_dome_bytes_new
	);

}

/*
Overrides world_build.
*/
void patch_world_build()
{
	write_call(0x004E1061, (int)modifiedWorldBuild);
	write_call(0x004E113B, (int)modifiedWorldBuild);
	write_call(0x0058B9BF, (int)modifiedWorldBuild);
	write_call(0x0058DDD8, (int)modifiedWorldBuild);

}

/*
Overrides check for frendly unit in target tile.
*/
void patch_zoc_move_to_friendly_unit_tile_check()
{
	write_call(0x005948E5, (int)modifiedZocMoveToFriendlyUnitTileCheck);

}

/*
Modifies biology lab research bonus.
*/
void patch_biology_lab_research_bonus(int biology_lab_research_bonus)
{
	int biology_lab_research_bonus_bytes_length = 7;
	byte biology_lab_research_bonus_bytes_old[] = { 0x83, 0x80, 0x08, 0x01, 0x00, 0x00, 0x02 };
	byte biology_lab_research_bonus_bytes_new[] = { 0x83, 0x80, 0x08, 0x01, 0x00, 0x00, (byte)biology_lab_research_bonus };
	write_bytes
	(
		0x004EBC85,
		biology_lab_research_bonus_bytes_length,
		biology_lab_research_bonus_bytes_old,
		biology_lab_research_bonus_bytes_new
	);

}

/*
Weapon help always shows cost even if it equals to firepower.
*/
void patch_weapon_help_always_show_cost()
{
	int weapon_help_show_cost_always_bytes_length = 2;
	byte weapon_help_show_cost_always_bytes_old[] = { 0x75, 0x24 };
	byte weapon_help_show_cost_always_bytes_new[] = { 0xEB, 0x24 };
	write_bytes
	(
		0x0042E5D5,
		weapon_help_show_cost_always_bytes_length,
		weapon_help_show_cost_always_bytes_old,
		weapon_help_show_cost_always_bytes_new
	);

}

/*
Wraps base_first call.
*/
void patch_base_first()
{
	write_call(0x004E4D47, (int)modifiedBaseFirst);
	write_call(0x0050D36E, (int)modifiedBaseFirst);

}

/*
Disables restoring movement points to terraforming vehicle.
*/
void patch_veh_wake()
{
	int veh_wake_does_not_restore_movement_points_bytes_length = 2;
	byte veh_wake_does_not_restore_movement_points_bytes_old[] = { 0x75, 0x45 };
	byte veh_wake_does_not_restore_movement_points_bytes_new[] = { 0xEB, 0x45 };
	write_bytes
	(
		0x005C1D9A,
		veh_wake_does_not_restore_movement_points_bytes_length,
		veh_wake_does_not_restore_movement_points_bytes_old,
		veh_wake_does_not_restore_movement_points_bytes_new
	);

}

/*
Flattens extra prototype cost.
*/
void patch_flat_extra_prototype_cost()
{
	// in DesignWin::draw_unit_preview

	// prepare parameters for non prototyped components cost calculation

	int draw_unit_preview_flat_prototype_cost_bytes_length = 0x1e;
/*
0:  8b 86 0c 42 01 00       mov    eax,DWORD PTR [esi+0x1420c]
6:  8b 8e 04 42 01 00       mov    ecx,DWORD PTR [esi+0x14204]
c:  8b 96 00 42 01 00       mov    edx,DWORD PTR [esi+0x14200]
12: 50                      push   eax
13: 8b 86 fc 41 01 00       mov    eax,DWORD PTR [esi+0x141fc]
19: 6a 00                   push   0x0
1b: 51                      push   ecx
1c: 52                      push   edx
1d: 50                      push   eax
*/
	byte draw_unit_preview_flat_prototype_cost_bytes_old[] = { 0x8B, 0x86, 0x0C, 0x42, 0x01, 0x00, 0x8B, 0x8E, 0x04, 0x42, 0x01, 0x00, 0x8B, 0x96, 0x00, 0x42, 0x01, 0x00, 0x50, 0x8B, 0x86, 0xFC, 0x41, 0x01, 0x00, 0x6A, 0x00, 0x51, 0x52, 0x50 };
/*
0:  ff 75 cc                push   DWORD PTR [ebp-0x34]
3:  ff 75 d0                push   DWORD PTR [ebp-0x30]
6:  ff 75 d4                push   DWORD PTR [ebp-0x2c]
9:  ff b6 04 42 01 00       push   DWORD PTR [esi+0x14204]
f:  ff b6 00 42 01 00       push   DWORD PTR [esi+0x14200]
15: ff b6 fc 41 01 00       push   DWORD PTR [esi+0x141fc]
...
*/
	byte draw_unit_preview_flat_prototype_cost_bytes_new[] = { 0xFF, 0x75, 0xCC, 0xFF, 0x75, 0xD0, 0xFF, 0x75, 0xD4, 0xFF, 0xB6, 0x04, 0x42, 0x01, 0x00, 0xFF, 0xB6, 0x00, 0x42, 0x01, 0x00, 0xFF, 0xB6, 0xFC, 0x41, 0x01, 0x00, 0x90, 0x90, 0x90 };
	write_bytes
	(
		0x0043702E,
		draw_unit_preview_flat_prototype_cost_bytes_length,
		draw_unit_preview_flat_prototype_cost_bytes_old,
		draw_unit_preview_flat_prototype_cost_bytes_new
	);

	// call non prototyped components cost calculation

	write_call(0x0043704C, (int)calculateNotPrototypedComponentsCostForDesign);

	// correct stack pointer after the call as we passed 6 parameters instead of 5

	int calculateNotPrototypedComponentsCostForDesign_stack_pointer_bytes_length = 0x3;
/*
0:  83 c4 20                add    esp,0x20
*/
	byte calculateNotPrototypedComponentsCostForDesign_stack_pointer_bytes_old[] = { 0x83, 0xC4, 0x20 };
/*
0:  83 c4 24                add    esp,0x24
*/
	byte calculateNotPrototypedComponentsCostForDesign_stack_pointer_bytes_new[] = { 0x83, 0xC4, 0x24 };
	write_bytes
	(
		0x0043709E,
		calculateNotPrototypedComponentsCostForDesign_stack_pointer_bytes_length,
		calculateNotPrototypedComponentsCostForDesign_stack_pointer_bytes_old,
		calculateNotPrototypedComponentsCostForDesign_stack_pointer_bytes_new
	);

	// in veh_cost

	// change call for base_cost to calculate non prototyped components cost

	write_call(0x005C198D, (int)calculateNotPrototypedComponentsCostForProduction);

}

/*
Adjusts mineral contribution based on INDUSTRY rating.
*/
void patch_mineral_contribution()
{
	// adjust artifact mineral contribution

	int artifact_mineral_contribution_bytes_length = 0x1f;
/*
0:  8d 14 f6                lea    edx,[esi+esi*8]
3:  53                      push   ebx
4:  8d 0c 56                lea    ecx,[esi+edx*2]
7:  33 d2                   xor    edx,edx
9:  8a 90 91 b8 9a 00       mov    dl,BYTE PTR [eax+0x9ab891]
f:  8b c2                   mov    eax,edx
11: 8d 0c 8e                lea    ecx,[esi+ecx*4]
14: c1 e1 02                shl    ecx,0x2
17: 8d 04 80                lea    eax,[eax+eax*4]
1a: d1 e0                   shl    eax,1
1c: 99                      cdq
1d: 2b c2                   sub    eax,edx
*/
	byte artifact_mineral_contribution_bytes_old[] = { 0x8D, 0x14, 0xF6, 0x53, 0x8D, 0x0C, 0x56, 0x33, 0xD2, 0x8A, 0x90, 0x91, 0xB8, 0x9A, 0x00, 0x8B, 0xC2, 0x8D, 0x0C, 0x8E, 0xC1, 0xE1, 0x02, 0x8D, 0x04, 0x80, 0xD1, 0xE0, 0x99, 0x2B, 0xC2 };
/*
0:  53                      push   ebx
1:  31 d2                   xor    edx,edx
3:  8a 90 91 b8 9a 00       mov    dl,BYTE PTR [eax+0x9ab891]
9:  52                      push   edx
a:  e8 fd ff ff ff          call   c <_main+0xc>     ; getActiveFactionMineralCostFactor
f:  5a                      pop    edx
10: f7 e2                   mul    edx
12: 8d 14 f6                lea    edx,[esi+esi*8]
15: 8d 0c 56                lea    ecx,[esi+edx*2]
18: 8d 0c 8e                lea    ecx,[esi+ecx*4]
1b: c1 e1 02                shl    ecx,0x2
1e: 90                      nop
*/
	byte artifact_mineral_contribution_bytes_new[] = { 0x53, 0x31, 0xD2, 0x8A, 0x90, 0x91, 0xB8, 0x9A, 0x00, 0x52, 0xE8, 0xFD, 0xFF, 0xFF, 0xFF, 0x5A, 0xF7, 0xE2, 0x8D, 0x14, 0xF6, 0x8D, 0x0C, 0x56, 0x8D, 0x0C, 0x8E, 0xC1, 0xE1, 0x02, 0x90 };
	write_bytes
	(
		0x005996C9,
		artifact_mineral_contribution_bytes_length,
		artifact_mineral_contribution_bytes_old,
		artifact_mineral_contribution_bytes_new
	);

	write_call(0x005996D3, (int)getActiveFactionMineralCostFactor);

	// display artifact mineral contribution for project

	write_call(0x00594D14, (int)displayArtifactMineralContributionInformation);

	// display artifact mineral contribution for prototype

	write_call(0x00594D8C, (int)displayArtifactMineralContributionInformation);

}

/*
Displays base production for pact faction bases same way as for infiltration.
*/
void patch_pact_base_map_production_display()
{
	write_call(0x0046836E, (int)modifiedSpyingForPactBaseProductionDisplay);

}

/*
Introduce a hook to modify probe action risks.
*/
void patch_modified_probe_action_risks()
{
	int modified_probe_action_risk_hook_bytes_length = 0x13;
/*
0:  8b 55 08                mov    edx,DWORD PTR [ebp+0x8]
3:  6a 00                   push   0x0
5:  6a 01                   push   0x1
7:  52                      push   edx
8:  e8 f2 dc 01 00          call   0x1dcff
d:  83 c4 0c                add    esp,0xc
10: 89 45 b4                mov    DWORD PTR [ebp-0x4c],eax
*/
	byte modified_probe_action_risk_hook_bytes_old[] = { 0x8B, 0x55, 0x08, 0x6A, 0x00, 0x6A, 0x01, 0x52, 0xE8, 0xF2, 0xDC, 0x01, 0x00, 0x83, 0xC4, 0x0C, 0x89, 0x45, 0xB4 };
/*
0:  89 45 b4                mov    DWORD PTR [ebp-0x4c],eax
3:  8d 45 d0                lea    eax,[ebp-0x30]
6:  50                      push   eax
7:  ff 75 e8                push   DWORD PTR [ebp-0x18]
a:  e8 fd ff ff ff          call   c <_main+0xc>
f:  83 c4 08                add    esp,0x8
...
*/
	byte modified_probe_action_risk_hook_bytes_new[] = { 0x89, 0x45, 0xB4, 0x8D, 0x45, 0xD0, 0x50, 0xFF, 0x75, 0xE8, 0xE8, 0xFD, 0xFF, 0xFF, 0xFF, 0x83, 0xC4, 0x08, 0x90 };
	write_bytes
	(
		0x005A3141,
		modified_probe_action_risk_hook_bytes_length,
		modified_probe_action_risk_hook_bytes_old,
		modified_probe_action_risk_hook_bytes_new
	);

	write_call(0x005A314B, (int)modifiedProbeActionRisk);

}

/*
Patches Infiltrate Datalinks option for infiltration expiration.
*/
void patch_infiltrate_datalinks()
{
	// always display Infiltrate Datalinks option

	int infiltrate_datalinks_option_always_on_bytes_length = 0x2;
/*
0:  75 14                   jne    0x16
*/
	byte infiltrate_datalinks_option_always_on_bytes_old[] = { 0x75, 0x14 };
/*
...
*/
	byte infiltrate_datalinks_option_always_on_bytes_new[] = { 0x90, 0x90 };
	write_bytes
	(
		0x0059F90F,
		infiltrate_datalinks_option_always_on_bytes_length,
		infiltrate_datalinks_option_always_on_bytes_old,
		infiltrate_datalinks_option_always_on_bytes_new
	);

	// adds number of planted devices information to Infiltrate Datalinks option text

	write_call(0x0059F8ED, (int)modifiedInfiltrateDatalinksOptionTextGet);

	// always set infiltrator flag even if it is already set

	int always_set_infiltrator_flag_bytes_length = 0x2;
/*
0:  75 23                   jne    0x25
*/
	byte always_set_infiltrator_flag_bytes_old[] = { 0x75, 0x23 };
/*
...
*/
	byte always_set_infiltrator_flag_bytes_new[] = { 0x90, 0x90 };
	write_bytes
	(
		0x0059FB9C,
		always_set_infiltrator_flag_bytes_length,
		always_set_infiltrator_flag_bytes_old,
		always_set_infiltrator_flag_bytes_new
	);

	// wrap set_treaty call for infiltration flag to also set number of devices

	write_call(0x0059FBA7, (int)modifiedSetTreatyForInfiltrationExpiration);

}

/*
Disable setting knowledge price over the credits.
*/
void patch_min_knowledge_price()
{
	int disable_min_knowledge_price_bytes_length = 0x2;
/*
0:  8b c1                   mov    eax,ecx
*/
	byte disable_min_knowledge_price_bytes_old[] = { 0x8B, 0xC1 };
/*
...
*/
	byte disable_min_knowledge_price_bytes_new[] = { 0x90, 0x90 };
	write_bytes
	(
		0x00540A35,
		disable_min_knowledge_price_bytes_length,
		disable_min_knowledge_price_bytes_old,
		disable_min_knowledge_price_bytes_new
	);

}

/*
Search right base for returned sea probe.
*/
void patch_find_returned_probe_base()
{
	int find_returned_probe_base_bytes_length = 0x37;
/*
0:  8b 1d cc 64 9a 00       mov    ebx,DWORD PTR ds:0x9a64cc
6:  83 ce ff                or     esi,0xffffffff
9:  83 cf ff                or     edi,0xffffffff
c:  33 c0                   xor    eax,eax
e:  85 db                   test   ebx,ebx
10: 7e 76                   jle    0x88
12: ba 46 d0 97 00          mov    edx,0x97d046
17: 33 c9                   xor    ecx,ecx
19: 8a 4a fe                mov    cl,BYTE PTR [edx-0x2]
1c: 3b 4d e4                cmp    ecx,DWORD PTR [ebp-0x1c]
1f: 75 0b                   jne    0x2c
21: 0f be 0a                movsx  ecx,BYTE PTR [edx]
24: 3b cf                   cmp    ecx,edi
26: 7e 04                   jle    0x2c
28: 8b f9                   mov    edi,ecx
2a: 8b f0                   mov    esi,eax
2c: 40                      inc    eax
2d: 81 c2 34 01 00 00       add    edx,0x134
33: 3b c3                   cmp    eax,ebx
35: 7c e0                   jl     0x17
*/
	byte find_returned_probe_base_bytes_old[] = { 0x8B, 0x1D, 0xCC, 0x64, 0x9A, 0x00, 0x83, 0xCE, 0xFF, 0x83, 0xCF, 0xFF, 0x33, 0xC0, 0x85, 0xDB, 0x7E, 0x76, 0xBA, 0x46, 0xD0, 0x97, 0x00, 0x33, 0xC9, 0x8A, 0x4A, 0xFE, 0x3B, 0x4D, 0xE4, 0x75, 0x0B, 0x0F, 0xBE, 0x0A, 0x3B, 0xCF, 0x7E, 0x04, 0x8B, 0xF9, 0x8B, 0xF0, 0x40, 0x81, 0xC2, 0x34, 0x01, 0x00, 0x00, 0x3B, 0xC3, 0x7C, 0xE0 };
/*
0:  ff 75 e0                push   DWORD PTR [ebp-0x20]
3:  e8 fd ff ff ff          call   5 <_main+0x5>
8:  83 c4 04                add    esp,0x4
b:  89 c6                   mov    esi,eax
...
*/
	byte find_returned_probe_base_bytes_new[] = { 0xFF, 0x75, 0xE0, 0xE8, 0xFD, 0xFF, 0xFF, 0xFF, 0x83, 0xC4, 0x04, 0x89, 0xC6, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
	write_bytes
	(
		0x0059700F,
		find_returned_probe_base_bytes_length,
		find_returned_probe_base_bytes_old,
		find_returned_probe_base_bytes_new
	);

	write_call(0x0059700F + 0x3, (int)modifiedFindReturnedProbeBase);

}

/*
Wraps best_defender call.
*/
void patch_best_defender()
{
	write_call(0x00506D07, (int)modifiedBestDefender);

}

/*
Fixes global variable when action_destroy is called.
*/
void patch_action_destroy()
{
	write_call(0x004CAA7C, (int)modifiedVehSkipForActionDestroy);

}

/*
Requests for break treaty before fight.
*/
void patch_break_treaty_before_fight()
{
	write_call(0x00506ADE, (int)modifiedBattleFight2);
	write_call(0x00568B1C, (int)modifiedBattleFight2);
	write_call(0x005697AC, (int)modifiedBattleFight2);
	write_call(0x0056A2E2, (int)modifiedBattleFight2);

}

/*
Displays TALENT icon in SE dialog.
*/
void patch_talent_display()
{
	// in SocialWin__draw_social

	// do not skip TALENT when counting icon display width

	int talent_count_width_bytes_length = 0xb;
/*
0:  3d d8 b2 94 00          cmp    eax,0x94b2d8
5:  0f 84 e9 00 00 00       je     0xf4
*/
	byte talent_count_width_bytes_old[] = { 0x3D, 0xD8, 0xB2, 0x94, 0x00, 0x0F, 0x84, 0xE9, 0x00, 0x00, 0x00 };
/*
...
*/
	byte talent_count_width_bytes_new[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
	write_bytes
	(
		0x004AF509,
		talent_count_width_bytes_length,
		talent_count_width_bytes_old,
		talent_count_width_bytes_new
	);

	// do not skip TALENT when displaying icons

	int talent_display_icon_bytes_length = 0xb;
/*
0:  3d d8 b2 94 00          cmp    eax,0x94b2d8
5:  0f 84 d9 01 00 00       je     0x1e4
*/
	byte talent_display_icon_bytes_old[] = { 0x3D, 0xD8, 0xB2, 0x94, 0x00, 0x0F, 0x84, 0xD9, 0x01, 0x00, 0x00 };
/*
...
*/
	byte talent_display_icon_bytes_new[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
	write_bytes
	(
		0x004AF67B,
		talent_display_icon_bytes_length,
		talent_display_icon_bytes_old,
		talent_display_icon_bytes_new
	);

	// do not shift icon index by -1 for all effects starting from TALENT and above

	int talent_index_shift_bytes_length = 0x7;
/*
0:  3d d8 b2 94 00          cmp    eax,0x94b2d8
5:  7d 25                   jge    0x2c
*/
	byte talent_index_shift_bytes_old[] = { 0x3D, 0xD8, 0xB2, 0x94, 0x00, 0x7D, 0x25 };
/*
...
*/
	byte talent_index_shift_bytes_new[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
	write_bytes
	(
		0x004AF7F3,
		talent_index_shift_bytes_length,
		talent_index_shift_bytes_old,
		talent_index_shift_bytes_new
	);

	// calculate sprite offset

	int calculate_sprite_offset_bytes_length = 0x28;
/*
0:  33 c9                   xor    ecx,ecx
2:  83 fe 01                cmp    esi,0x1
5:  0f 9d c1                setge  cl
8:  41                      inc    ecx
9:  6a 01                   push   0x1
b:  03 c1                   add    eax,ecx
d:  6a 01                   push   0x1
f:  8d 0c 80                lea    ecx,[eax+eax*4]
12: 8d 04 48                lea    eax,[eax+ecx*2]
15: 8d 8c 87 20 2f 00 00    lea    ecx,[edi+eax*4+0x2f20]
1c: 8b 45 e8                mov    eax,DWORD PTR [ebp-0x18]
1f: 50                      push   eax
20: 33 c0                   xor    eax,eax
22: 8a 41 08                mov    al,BYTE PTR [ecx+0x8]
25: 53                      push   ebx
26: 50                      push   eax
27: 52                      push   edx
*/
	byte calculate_sprite_offset_bytes_old[] = { 0x33, 0xC9, 0x83, 0xFE, 0x01, 0x0F, 0x9D, 0xC1, 0x41, 0x6A, 0x01, 0x03, 0xC1, 0x6A, 0x01, 0x8D, 0x0C, 0x80, 0x8D, 0x04, 0x48, 0x8D, 0x8C, 0x87, 0x20, 0x2F, 0x00, 0x00, 0x8B, 0x45, 0xE8, 0x50, 0x33, 0xC0, 0x8A, 0x41, 0x08, 0x53, 0x50, 0x52 };
/*
0:  6a 01                   push   0x1
2:  6a 01                   push   0x1
4:  ff 75 e8                push   DWORD PTR [ebp-0x18]
7:  53                      push   ebx
8:  52                      push   edx
9:  56                      push   esi
a:  50                      push   eax
b:  e8 fd ff ff ff          call   d <_main+0xd>
10: 83 c4 08                add    esp,0x8
13: 89 c1                   mov    ecx,eax
15: 5a                      pop    edx
16: 31 c0                   xor    eax,eax
18: 8a 41 08                mov    al,BYTE PTR [ecx+0x8]
1b: 50                      push   eax
1c: 52                      push   edx
*/
	byte calculate_sprite_offset_bytes_new[] = { 0x6A, 0x01, 0x6A, 0x01, 0xFF, 0x75, 0xE8, 0x53, 0x52, 0x56, 0x50, 0xE8, 0xFD, 0xFF, 0xFF, 0xFF, 0x83, 0xC4, 0x08, 0x89, 0xC1, 0x5A, 0x31, 0xC0, 0x8A, 0x41, 0x08, 0x50, 0x52, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
	write_bytes
	(
		0x004AF825,
		calculate_sprite_offset_bytes_length,
		calculate_sprite_offset_bytes_old,
		calculate_sprite_offset_bytes_new
	);

	write_call(0x004AF825 + 0xb, (int)modifiedSocialWinDrawSocialCalculateSpriteOffset);

	// do not skip TALENT when counting icons in description

	int talent_count_icon_description_bytes_length = 0xc;
/*
0:  81 fa d8 b2 94 00       cmp    edx,0x94b2d8
6:  0f 84 ca 01 00 00       je     0x1d6
*/
	byte talent_count_icon_description_bytes_old[] = { 0x81, 0xFA, 0xD8, 0xB2, 0x94, 0x00, 0x0F, 0x84, 0xCA, 0x01, 0x00, 0x00 };
/*
...
*/
	byte talent_count_icon_description_bytes_new[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
	write_bytes
	(
		0x004B122E,
		talent_count_icon_description_bytes_length,
		talent_count_icon_description_bytes_old,
		talent_count_icon_description_bytes_new
	);

	// do not skip TALENT when displaying icons in description

	int talent_display_icon_description_bytes_length = 0xc;
/*
0:  81 fa d8 b2 94 00       cmp    edx,0x94b2d8
6:  0f 84 8d 02 00 00       je     0x299
*/
	byte talent_display_icon_description_bytes_old[] = { 0x81, 0xFA, 0xD8, 0xB2, 0x94, 0x00, 0x0F, 0x84, 0x8D, 0x02, 0x00, 0x00 };
/*
...
*/
	byte talent_display_icon_description_bytes_new[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
	write_bytes
	(
		0x004B149B,
		talent_display_icon_description_bytes_length,
		talent_display_icon_description_bytes_old,
		talent_display_icon_description_bytes_new
	);

}

/*
Jams effect icons closer together for 5 icons to fit model box.
*/
void patch_compact_effect_icons()
{
	// === in SE dialog under model name ===

	// sprite display position shifts before and after

	int originalWidth = 26;
	int shiftBefore = -2;
	int shiftAfter = -4;
	int shiftInitial = -3;
	int shiftCombined = shiftBefore + shiftAfter;
	int modifiedWidth = originalWidth + shiftCombined;

	// modify effect sprite width for the purpose total diplayed width

	int effect_sprite_width_bytes_length = 0x3;
/*
0:  8d 51 01                lea    edx,[ecx+0x1]
*/
	byte effect_sprite_width_bytes_old[] = { 0x8D, 0x51, 0x01 };
/*
0:  8d 51 f7                lea    edx,[ecx+<1 + shiftCombined>]
...
*/
	byte effect_sprite_width_bytes_new[] = { 0x8D, 0x51, (byte)((1 + shiftCombined) & 0xFF) };
	write_bytes
	(
		0x004AF628,
		effect_sprite_width_bytes_length,
		effect_sprite_width_bytes_old,
		effect_sprite_width_bytes_new
	);

	// modify starting horizontal position

	int starting_horizontal_position_bytes_length = 0x7;
/*
0:  8b 45 d4                mov    eax,DWORD PTR [ebp-0x2c]
3:  d1 fb                   sar    ebx,1
5:  03 d8                   add    ebx,eax
*/
	byte starting_horizontal_position_bytes_old[] = { 0x8B, 0x45, 0xD4, 0xD1, 0xFB, 0x03, 0xD8 };
/*
0:  d1 fb                   sar    ebx,1
2:  83 c3 13                add    ebx,<29 + shiftInitial>
...
*/
	byte starting_horizontal_position_bytes_new[] = { 0xD1, 0xFB, 0x83, 0xC3, (byte)((29 + shiftInitial) & 0xFF), 0x90, 0x90 };
	write_bytes
	(
		0x004AF650,
		starting_horizontal_position_bytes_length,
		starting_horizontal_position_bytes_old,
		starting_horizontal_position_bytes_new
	);

	// modify current horizontal position before effect sprite

	int horizontal_position_before_effect_bytes_length = 0x7;
/*
...
*/
	byte horizontal_position_before_effect_bytes_old[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
/*
0:  8d 5b 01                lea    ebx,[ebx+<shiftBefore>]
...
*/
	byte horizontal_position_before_effect_bytes_new[] = { 0x8D, 0x5B, (byte)((shiftBefore) & 0xFF), 0x90, 0x90, 0x90, 0x90 };
	write_bytes
	(
		0x004AF7F3,
		horizontal_position_before_effect_bytes_length,
		horizontal_position_before_effect_bytes_old,
		horizontal_position_before_effect_bytes_new
	);

	// modify current horizontal position after effect sprite

	int horizontal_position_after_effect_bytes_length = 0x4;
/*
0:  8d 5c 0b 01             lea    ebx,[ebx+ecx*1+0x1]
*/
	byte horizontal_position_after_effect_bytes_old[] = { 0x8D, 0x5C, 0x0B, 0x01 };
/*
0:  8d 5c 0b f7             lea    ebx,[ebx+ecx*1+<1 + shiftAfter>]
*/
	byte horizontal_position_after_effect_bytes_new[] = { 0x8D, 0x5C, 0x0B, (byte)((1 + shiftAfter) & 0xFF) };
	write_bytes
	(
		0x004AF858,
		horizontal_position_after_effect_bytes_length,
		horizontal_position_after_effect_bytes_old,
		horizontal_position_after_effect_bytes_new
	);

	// === in description window when hovering mouse over SE choice ===

	// vertical positions and steps

	int descriptionVerticalStepOriginal = 26;
	int descriptionVerticalStep = 20;
	int topMarginExtra = -2;
	int topMargin = (descriptionVerticalStep - descriptionVerticalStepOriginal) / 2 + topMarginExtra;

	// set top margin to half of the vertical step difference

	int description_top_margin_bytes_length = 0x8;
/*
0:  99                      cdq
1:  2b c2                   sub    eax,edx
3:  8b 55 d4                mov    edx,DWORD PTR [ebp-0x2c]
6:  d1 f8                   sar    eax,1
*/
	byte description_top_margin_bytes_old[] = { 0x99, 0x2B, 0xC2, 0x8B, 0x55, 0xD4, 0xD1, 0xF8 };
/*
0:  b8 01 00 00 00          mov    eax,<top margin>
5:  8b 55 d4                mov    edx,DWORD PTR [ebp-0x2c]
*/
	byte description_top_margin_bytes_new[] = { 0xB8, (byte)((topMargin >> 0) & 0xFF), (byte)((topMargin >> 8) & 0xFF), (byte)((topMargin >> 16) & 0xFF), (byte)((topMargin >> 24) & 0xFF), 0x8B, 0x55, 0xD4 };
	write_bytes
	(
		0x004B1458,
		description_top_margin_bytes_length,
		description_top_margin_bytes_old,
		description_top_margin_bytes_new
	);

	// change vertical step in description

	int description_vertical_step_bytes_length = 0x7;
/*
0:  8b 97 34 2e 00 00       mov    edx,DWORD PTR [edi+0x2e34]
*/
	byte description_vertical_step_bytes_old[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
/*
0:  c7 45 d4 01 00 00 00    mov    DWORD PTR [ebp-0x2c],<vertical step>
*/
	byte description_vertical_step_bytes_new[] = { 0xC7, 0x45, 0xD4, (byte)(descriptionVerticalStep & 0xFF), 0x00, 0x00, 0x00 };
	write_bytes
	(
		0x004B149B,
		description_vertical_step_bytes_length,
		description_vertical_step_bytes_old,
		description_vertical_step_bytes_new
	);

}

/*
Wraps tech_research to modify SE RESEARCH bonus.
*/
void patch_research_bonus_multiplier()
{
	write_call(0x004F4F9C, (int)modifiedTechResearch);

}

/*
Removes fungal tower defense bonus.
*/
void patch_remove_fungal_tower_defense_bonus()
{
	// fungal tower defense bonus

	int disable_fungal_tower_defense_bonus_bytes_length = 0x2;
/*
0:  75 09                   jne    0xb
*/
	byte disable_fungal_tower_defense_bonus_bytes_old[] = { 0x75, 0x09 };
/*
0:  eb 09                   jmp    0xb
*/
	byte disable_fungal_tower_defense_bonus_bytes_new[] = { 0xEB, 0x09 };
	write_bytes
	(
		0x00501D0F,
		disable_fungal_tower_defense_bonus_bytes_length,
		disable_fungal_tower_defense_bonus_bytes_old,
		disable_fungal_tower_defense_bonus_bytes_new
	);

	// fungal tower defense bonus display

	int disable_fungal_tower_defense_bonus_display_bytes_length = 0x2;
/*
0:  75 39                   jne    0x3b
*/
	byte disable_fungal_tower_defense_bonus_display_bytes_old[] = { 0x75, 0x39 };
/*
0:  eb 39                   jmp    0x3b
*/
	byte disable_fungal_tower_defense_bonus_display_bytes_new[] = { 0xEB, 0x39 };
	write_bytes
	(
		0x005020C7,
		disable_fungal_tower_defense_bonus_display_bytes_length,
		disable_fungal_tower_defense_bonus_display_bytes_old,
		disable_fungal_tower_defense_bonus_display_bytes_new
	);

}

/*
aliens fight at half strength until this turn.
*/
void patch_aliens_fight_half_strength_unit_turn(int turn)
{
       int aliens_fight_half_strength_unit_turn_bytes_length = 0x7;
/*
0:  83 3d d4 64 9a 00 0f    cmp    DWORD PTR ds:0x9a64d4,0xf
*/
       byte aliens_fight_half_strength_unit_turn_bytes_old[] = { 0x83, 0x3D, 0xD4, 0x64, 0x9A, 0x00, 0x0F };
/*
0:  83 3d d4 64 9a 00 01    cmp    DWORD PTR ds:0x9a64d4,<turn>
*/
       byte aliens_fight_half_strength_unit_turn_bytes_new[] = { 0x83, 0x3D, 0xD4, 0x64, 0x9A, 0x00, (byte)turn };
       write_bytes
       (
               0x00507C22,
               aliens_fight_half_strength_unit_turn_bytes_length,
               aliens_fight_half_strength_unit_turn_bytes_old,
               aliens_fight_half_strength_unit_turn_bytes_new
       );

}

// ========================================
// patch setup
// ========================================

bool patch_setup(Config* cf) {
    DWORD attrs;
    int lm = ~cf->landmarks;
    if (!VirtualProtect(AC_IMAGE_BASE, AC_IMAGE_LEN, PAGE_EXECUTE_READWRITE, &attrs))
        return false;

    write_jump(0x527290, (int)faction_upkeep);
    write_call(0x52768A, (int)turn_upkeep);
    write_call(0x52A4AD, (int)turn_upkeep);
    write_call(0x4E61D0, (int)suggestBaseProduction);
    write_call(0x5BDC4C, (int)tech_value);
    write_call(0x579362, (int)enemy_move);
    write_call(0x5C0908, (int)log_veh_kill);
    write_call(0x4E888C, (int)mod_crop_yield);
    write_call(0x4672A7, (int)mod_base_draw);
    write_call(0x40F45A, (int)mod_base_draw);

    /*
    Fixes issue where attacking other satellites doesn't work in
    Orbital Attack View when smac_only is activated.
    */
    if (*(int*)0x4AB327 == 0x1D2) {
        *(byte*)0x4AB327 = 0x75;
    }
    /*
    Make sure the game engine can read movie settings from "movlistx.txt".
    */
    if (FileExists(ac_movlist_txt) && !FileExists(ac_movlistx_txt)) {
        CopyFile(ac_movlist_txt, ac_movlistx_txt, TRUE);
    }

    if (cf->smac_only) {
        if (!FileExists("ac_mod/alphax.txt") || !FileExists("ac_mod/conceptsx.txt")
        || !FileExists("ac_mod/tutor.txt") || !FileExists("ac_mod/helpx.txt")) {
            MessageBoxA(0, "Error while opening ac_mod folder. Unable to start smac_only mode.",
                MOD_VERSION, MB_OK | MB_ICONSTOP);
            exit(EXIT_FAILURE);
        }
        *(int*)0x45F97A = 0;
        *(const char**)0x691AFC = ac_alpha;
        *(const char**)0x691B00 = ac_help;
        *(const char**)0x691B14 = ac_tutor;
        write_offset(0x42B30E, ac_concepts);
        write_offset(0x42B49E, ac_concepts);
        write_offset(0x42C450, ac_concepts);
        write_offset(0x42C7C2, ac_concepts);
        write_offset(0x403BA8, ac_movlist);
        write_offset(0x4BEF8D, ac_movlist);
        write_offset(0x52AB68, ac_opening);

        /* Enable custom faction selection during the game setup. */
        memset((void*)0x58A5E1, 0x90, 6);
        memset((void*)0x58B76F, 0x90, 2);
        memset((void*)0x58B9F3, 0x90, 2);
    }
    if (cf->faction_placement) {
        const byte asm_find_start[] = {
            0x8D,0x45,0xF8,0x50,0x8D,0x45,0xFC,0x50,0x8B,0x45,
            0x08,0x50,0xE8,0x00,0x00,0x00,0x00,0x83,0xC4,0x0C
        };
        remove_call(0x5B1DFF);
        remove_call(0x5B2137);
        memset((void*)0x5B220F, 0x90, 63);
        memset((void*)0x5B2257, 0x90, 11);
        memcpy((void*)0x5B220F, asm_find_start, sizeof(asm_find_start));
        write_call(0x5B221B, (int)find_start);
    }
    if (cf->revised_tech_cost) {
        write_call(0x4452D5, (int)tech_rate);
        write_call(0x498D26, (int)tech_rate);
        write_call(0x4A77DA, (int)tech_rate);
        write_call(0x521872, (int)tech_rate);
        write_call(0x5218BE, (int)tech_rate);
        write_call(0x5581E9, (int)tech_rate);
        write_call(0x5BEA4D, (int)tech_rate);
        write_call(0x5BEAC7, (int)tech_rate);

        write_call(0x52935F, (int)tech_selection);
        write_call(0x5BE5E1, (int)tech_selection);
        write_call(0x5BE690, (int)tech_selection);
        write_call(0x5BEB5D, (int)tech_selection);
    }
    if (cf->auto_relocate_hq) {
        write_call(0x4CCF13, (int)mod_capture_base);
        write_call(0x598778, (int)mod_capture_base);
        write_call(0x5A4AB0, (int)mod_capture_base);
        write_call(0x4CD629, (int)mod_base_kill); // action_oblit
        write_call(0x4F1466, (int)mod_base_kill); // base_production_choice
        write_call(0x4EF319, (int)mod_base_kill); // base_growth
        write_call(0x500FD7, (int)mod_base_kill); // planet_busting
        write_call(0x50ADA8, (int)mod_base_kill); // battle_fight_2
        write_call(0x50AE77, (int)mod_base_kill); // battle_fight_2
        write_call(0x520E1A, (int)mod_base_kill); // random_events
        write_call(0x521121, (int)mod_base_kill); // random_events
        write_call(0x5915A6, (int)mod_base_kill); // alt_set
        write_call(0x598673, (int)mod_base_kill); // order_veh
    }
    if (cf->eco_damage_fix) {
        const byte old_bytes[] = {0x84,0x05,0xE8,0x64,0x9A,0x00,0x74,0x24};
        const byte new_bytes[] = {0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90};
        write_bytes(0x4EA0B9, old_bytes, new_bytes, sizeof(new_bytes));
    }
    if (cf->collateral_damage_value != 3) {
        const byte old_bytes[] = {0xB2, 0x03};
        const byte new_bytes[] = {0xB2, (byte)cf->collateral_damage_value};
        write_bytes(0x50AAA5, old_bytes, new_bytes, sizeof(new_bytes));
    }
    if (cf->disable_aquatic_bonus_minerals) {
        const byte old_bytes[] = {0x46};
        const byte new_bytes[] = {0x90};
        write_bytes(0x4E7604, old_bytes, new_bytes, sizeof(new_bytes));
    }
    if (cf->disable_planetpearls) {
        const byte old_bytes[] = {
            0x8B,0x45,0x08,0x6A,0x00,0x6A,0x01,0x50,0xE8,0x46,
            0xAD,0x0B,0x00,0x83,0xC4,0x0C,0x40,0x8D,0x0C,0x80,
            0x8B,0x06,0xD1,0xE1,0x03,0xC1,0x89,0x06
        };
        write_bytes(0x5060ED, old_bytes, NULL, sizeof(old_bytes));
    }
    if (cf->patch_content_pop) {
        const byte old_bytes[] = {
            0x74,0x19,0x8B,0xD3,0xC1,0xE2,0x06,0x03,0xD3,0x8D,
            0x04,0x53,0x8D,0x0C,0xC3,0x8D,0x14,0x4B,0x8B,0x04,
            0x95,0xE8,0xC9,0x96,0x00,0xEB,0x05,0xB8,0x03,0x00,
            0x00,0x00,0xBE,0x06,0x00,0x00,0x00,0x2B,0xF0
        };
        const byte new_bytes[] = {
            0xE8,0x00,0x00,0x00,0x00,0x89,0xC6,0x90,0x90,0x90,
            0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,
            0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,
            0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90,0x90
        };
        write_bytes(0x4EA56D, old_bytes, new_bytes, sizeof(new_bytes));
        write_call(0x4EA56D, (int)content_pop);
    }

    if (lm & LM_JUNGLE) remove_call(0x5C88A0);
    if (lm & LM_CRATER) remove_call(0x5C88A9);
    if (lm & LM_VOLCANO) remove_call(0x5C88B5);
    if (lm & LM_MESA) remove_call(0x5C88BE);
    if (lm & LM_RIDGE) remove_call(0x5C88C7);
    if (lm & LM_URANIUM) remove_call(0x5C88D0);
    if (lm & LM_RUINS) remove_call(0x5C88D9);
    if (lm & LM_UNITY) remove_call(0x5C88EE);
    if (lm & LM_FOSSIL) remove_call(0x5C88F7);
    if (lm & LM_CANYON) remove_call(0x5C8903);
    if (lm & LM_NEXUS) remove_call(0x5C890F);
    if (lm & LM_BOREHOLE) remove_call(0x5C8918);
    if (lm & LM_SARGASSO) remove_call(0x5C8921);
    if (lm & LM_DUNES) remove_call(0x5C892A);
    if (lm & LM_FRESH) remove_call(0x5C8933);
    if (lm & LM_GEOTHERMAL) remove_call(0x5C893C);

    // ==============================
    // The Will to Power mod changes
    // ==============================

	// patch AI vehicle home base reassignment bug
	// originally it reassigned to bases with mineral_surplus < 2
	// this patch reverses the condition
	{
		const byte old_bytes[] = {0x0F, 0x8D};
		const byte new_bytes[] = {0x0F, 0x8C};
		write_bytes(0x00562094, old_bytes, new_bytes, sizeof(new_bytes));
	}

    // read basic rules

    patch_read_basic_rules();

    // patch battle_compute

    patch_battle_compute();

    // patch ignore_reactor_power_in_combat_processing

    if (cf->ignore_reactor_power_in_combat)
    {
        patch_ignore_reactor_power_in_combat_processing();

    }

    // patch combat roll

    patch_combat_roll();

    // patch calculate odds

    patch_calculate_odds();

    // patch ALIEN guaranteed technologies

    if (cf->disable_alien_guaranteed_technologies)
    {
        patch_alien_guaranteed_technologies();

    }

    // patch weapon icon selection algorithm

    if (cf->alternative_weapon_icon_selection_algorithm)
    {
        patch_weapon_icon_selection_algorithm();

    }

    // patch prototype cost formula

    if (cf->alternative_prototype_cost_formula)
    {
        patch_alternative_prototype_cost_formula();

    }

    // patch flat hurry cost

    if (cf->flat_hurry_cost)
    {
        patch_flat_hurry_cost();

    }

    // patch upgrade cost formula

    if (cf->alternative_upgrade_cost_formula)
    {
        patch_alternative_upgrade_cost_formula();

    }

    // patch base defense structure bonuses

    if (cf->alternative_base_defensive_structure_bonuses)
    {
        patch_defensive_structures_bonus(cf->perimeter_defense_multiplier, cf->tachyon_field_bonus);

    }

    // patch collateral damage defender reactor

    if (cf->collateral_damage_defender_reactor)
    {
        patch_collateral_damage_defender_reactor();

    }

// integrated into Thinker
//    // patch collateral damage value
//
//    if (cf->collateral_damage_value >= 0)
//    {
//        patch_collateral_damage_value(cf->collateral_damage_value);
//
//    }
//
// integrated into Thinker
//    // patch disable_aquatic_bonus_minerals
//
//    if (cf->disable_aquatic_bonus_minerals)
//    {
//        patch_disable_aquatic_bonus_minerals();
//
//    }
//
    // patch repair rate

    if (cf->repair_minimal != 1)
    {
        patch_repair_minimal(cf->repair_minimal);

    }
    if (cf->repair_fungus != 2)
    {
        patch_repair_fungus(cf->repair_fungus);

    }
    if (!cf->repair_friendly)
    {
        patch_repair_friendly();

    }
    if (!cf->repair_airbase)
    {
        patch_repair_airbase();

    }
    if (!cf->repair_bunker)
    {
        patch_repair_bunker();

    }
    if (cf->repair_base != 1)
    {
        patch_repair_base(cf->repair_base);

    }
    if (cf->repair_base_native != 10)
    {
        patch_repair_base_native(cf->repair_base_native);

    }
    if (cf->repair_base_facility != 10)
    {
        patch_repair_base_facility(cf->repair_base_facility);

    }
    if (cf->repair_nano_factory != 10)
    {
        patch_repair_nano_factory(cf->repair_nano_factory);

    }

// integrated into Thinker
//    // patch disable_planetpearls
//
//    if (cf->disable_planetpearls)
//    {
//        patch_disable_planetpearls();
//
//    }
//
    // patch uniform_promotions

    if (cf->uniform_promotions)
    {
        patch_uniform_promotions();

    }

    // patch very_green_no_defense_bonus

    if (cf->very_green_no_defense_bonus)
    {
        patch_very_green_no_defense_bonus();

    }

    // patch sea_territory_distance_same_as_land

    if (cf->sea_territory_distance_same_as_land)
    {
        patch_sea_territory_distance_same_as_land();

    }

    // patch coastal_territory_distance_same_as_sea

    if (cf->coastal_territory_distance_same_as_sea)
    {
        patch_coastal_territory_distance_same_as_sea();

    }

    // patch alternative_artillery_damage

    if (cf->alternative_artillery_damage)
    {
        patch_alternative_artillery_damage();

    }

    // patch disable_home_base_cc_morale_bonus

    if (cf->disable_home_base_cc_morale_bonus)
    {
        patch_disable_home_base_cc_morale_bonus();

    }

    // patch disable_current_base_cc_morale_bonus

    if (cf->disable_current_base_cc_morale_bonus)
    {
        patch_disable_current_base_cc_morale_bonus();

    }

    // patch default_morale_very_green

    if (cf->default_morale_very_green)
    {
        patch_default_morale_very_green();

    }

    // patch tile_yield

    patch_tile_yield();

    // se_accumulated_resource_adjustment

    patch_se_accumulated_resource_adjustment();

    // hex cost

    patch_hex_cost();

    // base population info

    patch_display_base_population_info();

    // base init

    patch_base_init();

    // HSA does not kill probe

    if (cf->hsa_does_not_kill_probe)
	{
		patch_hsa_does_not_kill_probe();
	}

    // probe does not destroy defense

    patch_probe_not_destroy_defense();

    // help ability

    patch_help_ability_cost_text();

	// cloning vats impunities

	if (cf->cloning_vats_disable_impunities)
	{
		patch_cloning_vats_impunities();
	}

	// social_calc
	// This is initially for cloning vats GROWTH effect but can be used for other modifications

	patch_social_calc();

    // cloning vats effect

    if (cf->cloning_vats_se_growth != 0)
	{
		patch_cloning_vats_mechanics();
	}

    // patch GROWTH rating cap

    if (cf->se_growth_rating_cap != 5)
	{
		patch_se_growth_rating_cap(cf->se_growth_rating_cap);
	}

	// patch growth turns display population boom

	patch_growth_turns_population_boom();

	// patch RT -> 50% minerals

	if (cf->recycling_tanks_mineral_multiplier)
	{
		patch_recycling_tank_minerals();
	}

	// patch free minerals

	if (cf->free_minerals != 16)
	{
		patch_free_minerals(cf->free_minerals);
	}

	// patch population incorrect superdrones in base screen population

	patch_base_scren_population_superdrones();

	// patch conflicting territory claim

	patch_conflicting_territory();

	// patch native life

	if (cf->native_life_generator_constant != 2 || cf->native_life_generator_multiplier != 2)
	{
		patch_native_life_frequency(cf->native_life_generator_constant, cf->native_life_generator_multiplier);
	}

	if (cf->native_life_generator_more_sea_creatures)
	{
		patch_native_life_sea_creatures();
	}

	if (cf->native_disable_sudden_death)
	{
		patch_native_disable_sudden_death();
	}

	if (cf->alternative_inefficiency)
	{
		patch_alternative_inefficiency();
	}

	patch_setup_player();

	patch_balance();

	if (cf->recycling_tanks_mineral_multiplier)
	{
		patch_recycling_tanks_not_disabled_by_pressure_dome();
	}

	patch_world_build();

	if (cf->zoc_regular_army_sneaking_disabled)
	{
		patch_zoc_move_to_friendly_unit_tile_check();
	}

	if (cf->biology_lab_research_bonus != 2)
	{
		patch_biology_lab_research_bonus(cf->biology_lab_research_bonus);
	}

	patch_weapon_help_always_show_cost();

	patch_base_first();

	if (cf->fix_former_wake)
	{
		patch_veh_wake();
	}

	if (cf->flat_extra_prototype_cost)
	{
		patch_flat_extra_prototype_cost();
	}

	if (cf->fix_mineral_contribution)
	{
		patch_mineral_contribution();
	}

	patch_pact_base_map_production_display();

	if (cf->modified_probe_action_risks)
	{
		patch_modified_probe_action_risks();
	}

	if (cf->infiltration_expire)
	{
		patch_infiltrate_datalinks();
	}

	patch_min_knowledge_price();

	patch_find_returned_probe_base();

	patch_best_defender();

	patch_action_destroy();

	if (cf->break_treaty_before_fight)
	{
		patch_break_treaty_before_fight();
	}

	patch_talent_display();

	if (cf->compact_effect_icons)
	{
		patch_compact_effect_icons();
	}

	if (cf->se_research_bonus_percentage != 10)
	{
		patch_research_bonus_multiplier();
	}

	if (cf->remove_fungal_tower_defense_bonus)
	{
		patch_remove_fungal_tower_defense_bonus();
	}

	if (cf->aliens_fight_half_strength_unit_turn != 15)
	{
		patch_aliens_fight_half_strength_unit_turn(cf->aliens_fight_half_strength_unit_turn);
	}


    // continue with original Thinker checks

    if (!VirtualProtect(AC_IMAGE_BASE, AC_IMAGE_LEN, PAGE_EXECUTE_READ, &attrs))
        return false;

    return true;

}

