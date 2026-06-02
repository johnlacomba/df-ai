#include "ai.h"
#include "population.h"
#include "plan.h"

#include "modules/Units.h"

#include "df/entity_material_category.h"
#include "df/entity_position.h"
#include "df/entity_position_responsibility.h"
#include "df/historical_entity.h"
#include "df/historical_figure.h"
#include "df/item_type.h"
#include "df/military_routinest.h"
#include "df/plotinfost.h"
#include "df/squad.h"
#include "df/squad_month_positionst.h"
#include "df/squad_order_kill_listst.h"
#include "df/squad_order_trainst.h"
#include "df/squad_position.h"
#include "df/squad_routine_schedulest.h"
#include "df/squad_schedule_entry.h"
#include "df/squad_schedule_order.h"
#include "df/squad_uniform_spec.h"
#include "df/uniform_category.h"
#include "df/unit.h"
#include "df/world.h"

REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(cur_year_tick);
REQUIRE_GLOBAL(plotinfo);
REQUIRE_GLOBAL(squad_next_id);
REQUIRE_GLOBAL(world);

static int32_t alloc_squad_id()
{
    int32_t id = *squad_next_id;
    (*squad_next_id)++;
    return id;
}

static df::squad_uniform_spec *make_uniform_spec(df::item_type itype, df::entity_material_category mat_class, df::uniform_indiv_choice indiv = df::uniform_indiv_choice())
{
    auto spec = df::allocate<df::squad_uniform_spec>();
    if (!spec)
        return nullptr;
    spec->item = -1;
    spec->item_type = itype;
    spec->item_subtype = -1;
    spec->material_class = mat_class;
    spec->mattype = -1;
    spec->matindex = -1;
    spec->color = -1;
    spec->indiv_choice = indiv;
    return spec;
}

static void setup_squad_equipment(df::squad *squad, bool ranged)
{
    df::uniform_indiv_choice weapon_choice;
    if (ranged)
        weapon_choice.bits.ranged = 1;
    else
        weapon_choice.bits.melee = 1;

    for (auto pos : squad->positions)
    {
        if (!pos)
            continue;

        pos->equipment.flags.bits.exact_matches = 1;

        auto body = make_uniform_spec(item_type::ARMOR, entity_material_category::Armor);
        if (body) pos->equipment.uniform[uniform_category::body].push_back(body);

        auto head = make_uniform_spec(item_type::HELM, entity_material_category::Armor);
        if (head) pos->equipment.uniform[uniform_category::head].push_back(head);

        auto pants = make_uniform_spec(item_type::PANTS, entity_material_category::Armor);
        if (pants) pos->equipment.uniform[uniform_category::pants].push_back(pants);

        auto gloves = make_uniform_spec(item_type::GLOVES, entity_material_category::Armor);
        if (gloves) pos->equipment.uniform[uniform_category::gloves].push_back(gloves);

        auto shoes = make_uniform_spec(item_type::SHOES, entity_material_category::Armor);
        if (shoes) pos->equipment.uniform[uniform_category::shoes].push_back(shoes);

        auto shield = make_uniform_spec(item_type::SHIELD, entity_material_category::Armor);
        if (shield) pos->equipment.uniform[uniform_category::shield].push_back(shield);

        auto weapon = make_uniform_spec(item_type::WEAPON, entity_material_category::None, weapon_choice);
        if (weapon) pos->equipment.uniform[uniform_category::weapon].push_back(weapon);
    }
}

static void setup_squad_schedule(df::squad *squad)
{
    int32_t squad_size = (int32_t)squad->positions.size();
    auto & routines = plotinfo->alerts.routines;

    if (routines.empty())
    {
        auto routine = df::allocate<df::squad_routine_schedulest>();
        if (!routine)
            return;
        for (int month = 0; month < 12; month++)
        {
            for (int j = 0; j < squad_size; j++)
            {
                auto oa = df::allocate<df::squad_month_positionst>();
                if (!oa)
                    continue;
                oa->assigned_order_idx = -1;
                routine->month[month].order_assignments.push_back(oa);
            }

            auto order = df::allocate<df::squad_schedule_order>();
            if (!order)
                continue;
            order->min_count = squad_size;
            order->positions.resize(squad_size);
            auto train = df::allocate<df::squad_order_trainst>();
            train->year = *cur_year;
            train->year_tick = *cur_year_tick;
            order->order = train;
            routine->month[month].orders.push_back(order);
            routine->month[month].sleep_mode = squad_sleep_option_type::AnywhereAtWill;
            routine->month[month].uniform_mode = squad_civilian_uniform_type::Regular;
        }
        squad->schedule.routine.push_back(routine);
        squad->cur_routine_idx = 0;
        return;
    }

    for (size_t ri = 0; ri < routines.size(); ri++)
    {
        auto routine = df::allocate<df::squad_routine_schedulest>();
        if (!routine)
            continue;
        auto & asched = routine->month;

        for (int month = 0; month < 12; month++)
        {
            for (int j = 0; j < squad_size; j++)
            {
                auto oa = df::allocate<df::squad_month_positionst>();
                if (!oa)
                    continue;
                oa->assigned_order_idx = -1;
                asched[month].order_assignments.push_back(oa);
            }
        }

        if (routines[ri]->name == "Staggered training" || routines[ri]->name == "Constant training")
        {
            int start = 0;
            int count = 12;
            if (routines[ri]->name == "Staggered training")
            {
                start = (squad->id & 1) ? 0 : 6;
                count = 6;
            }

            for (int i = 0; i < count; i++)
            {
                int month = (start + i) % 12;
                auto order = df::allocate<df::squad_schedule_order>();
                if (!order)
                    continue;
                order->min_count = squad_size;
                order->positions.resize(squad_size);

                auto train = df::allocate<df::squad_order_trainst>();
                train->year = *cur_year;
                train->year_tick = *cur_year_tick;
                order->order = train;

                asched[month].orders.push_back(order);
                asched[month].sleep_mode = squad_sleep_option_type::AnywhereAtWill;
                asched[month].uniform_mode = squad_civilian_uniform_type::Regular;
            }
        }
        else if (routines[ri]->name == "Off duty")
        {
            for (int i = 0; i < 12; i++)
            {
                asched[i].sleep_mode = squad_sleep_option_type::AnywhereAtWill;
                asched[i].uniform_mode = squad_civilian_uniform_type::Civilian;
            }
        }
        else if (routines[ri]->name == "Ready")
        {
            for (int i = 0; i < 12; i++)
            {
                asched[i].sleep_mode = squad_sleep_option_type::InBarracksAtNeed;
                asched[i].uniform_mode = squad_civilian_uniform_type::Regular;
            }
        }

        squad->schedule.routine.push_back(routine);
    }

    for (size_t ri = 0; ri < routines.size(); ri++)
    {
        if (routines[ri]->name == "Staggered training")
        {
            squad->cur_routine_idx = (int32_t)ri;
            break;
        }
    }
}

static df::squad *create_squad(color_ostream & out, AI & ai)
{
    auto entity = plotinfo->main.fortress_entity;
    if (!entity)
        return nullptr;

    auto squad = df::allocate<df::squad>();
    if (!squad)
    {
        ai.debug(out, "[military] ERROR: failed to allocate squad");
        return nullptr;
    }

    squad->id = alloc_squad_id();
    squad->entity_id = entity->id;
    squad->leader_position = -1;
    squad->leader_assignment = -1;
    squad->assigned_army_controller_id = -1;
    squad->cur_routine_idx = 0;
    squad->uniform_priority = squad->id + 1;
    squad->supplies.carry_food = 2;
    squad->supplies.carry_water = squad_water_level_type::Water;

    for (int i = 0; i < 10; i++)
    {
        auto pos = df::allocate<df::squad_position>();
        if (!pos)
        {
            ai.debug(out, "[military] ERROR: failed to allocate squad_position");
            delete squad;
            return nullptr;
        }
        pos->occupant = -1;
        squad->positions.push_back(pos);
    }

    world->squads.all.push_back(squad);
    entity->squads.push_back(squad->id);

    bool ranged = (entity->squads.size() % 3 == 1);
    setup_squad_equipment(squad, ranged);
    setup_squad_schedule(squad);

    ai.debug(out, stl_sprintf("[military] created %s squad id=%d", ranged ? "ranged" : "melee", squad->id));
    return squad;
}

static bool is_noble_excluded(df::unit *u)
{
    std::vector<Units::NoblePosition> positions;
    if (!Units::getNoblePositions(&positions, u))
        return false;

    for (auto & pos : positions)
    {
        if (pos.position->responsibilities[entity_position_responsibility::RECEIVE_DIPLOMATS] ||
            pos.position->responsibilities[entity_position_responsibility::MEET_WORKERS] ||
            pos.position->responsibilities[entity_position_responsibility::ACCOUNTING] ||
            pos.position->responsibilities[entity_position_responsibility::MANAGE_PRODUCTION] ||
            pos.position->responsibilities[entity_position_responsibility::TRADE] ||
            pos.position->responsibilities[entity_position_responsibility::HEALTH_MANAGEMENT])
        {
            return true;
        }
    }
    return false;
}

static df::squad *find_squad_with_vacancy(int32_t max_positions)
{
    auto entity = plotinfo->main.fortress_entity;
    if (!entity)
        return nullptr;

    for (auto squad_id : entity->squads)
    {
        auto squad = df::squad::find(squad_id);
        if (!squad)
            continue;

        int32_t occupied = 0;
        for (auto pos : squad->positions)
        {
            if (pos && pos->occupant != -1)
                occupied++;
        }
        if (occupied < max_positions && occupied < (int32_t)squad->positions.size())
            return squad;
    }
    return nullptr;
}

static bool draft_unit(color_ostream & out, AI & ai, df::unit *u, df::squad *squad)
{
    if (!u || !squad || u->hist_figure_id == -1)
        return false;

    for (size_t i = 0; i < squad->positions.size(); i++)
    {
        auto pos = squad->positions[i];
        if (pos && pos->occupant == -1)
        {
            pos->occupant = u->hist_figure_id;
            u->military.squad_id = squad->id;
            u->military.squad_position = (int32_t)i;

            u->status.labors[unit_labor::MINE] = false;
            u->status.labors[unit_labor::CUTWOOD] = false;
            u->status.labors[unit_labor::HUNT] = false;

            ai.pop.military[u->id] = squad->id;
            ai.plan.getsoldierbarrack(out, u->id);

            ai.debug(out, "[military] drafted " + AI::describe_unit(u) + stl_sprintf(" into squad %d pos %d", squad->id, (int)i));
            return true;
        }
    }
    return false;
}

void Population::update_military(color_ostream & out)
{
    auto entity = plotinfo->main.fortress_entity;
    if (!entity)
        return;

    for (auto it = military.begin(); it != military.end(); )
    {
        auto u = df::unit::find(it->first);
        if (!u || u->military.squad_id == -1 || !Units::isAlive(u))
        {
            ai.plan.freesoldierbarrack(out, it->first);
            it = military.erase(it);
        }
        else
        {
            it++;
        }
    }

    for (auto u : world->units.active)
    {
        if (!Units::isCitizen(u))
            continue;
        if (u->military.squad_id != -1 && !military.count(u->id))
        {
            military[u->id] = u->military.squad_id;
            ai.plan.getsoldierbarrack(out, u->id);
        }
    }

    if (citizen.size() < 10)
        return;

    size_t target_min = citizen.size() * military_min / 100;
    if (target_min < 1)
        target_min = 1;

    size_t citizen_military = 0;
    for (auto & m : military)
    {
        if (citizen.count(m.first))
            citizen_military++;
    }

    if (citizen_military >= target_min)
        return;

    std::vector<df::unit *> draft_pool;
    for (auto u : world->units.active)
    {
        if (!Units::isCitizen(u) || !Units::isAdult(u) || !Units::isSane(u))
            continue;
        if (u->military.squad_id != -1)
            continue;
        if (u->mood != mood_type::None)
            continue;
        if (is_noble_excluded(u))
            continue;

        draft_pool.push_back(u);
    }

    if (draft_pool.empty())
    {
        ai.debug(out, stl_sprintf("[military] want %zu soldiers but draft pool is empty (citizen=%zu, military=%zu)", target_min, citizen.size(), citizen_military));
        return;
    }

    ai.debug(out, stl_sprintf("[military] drafting: target=%zu, current=%zu, pool=%zu", target_min, citizen_military, draft_pool.size()));

    size_t to_draft = target_min - citizen_military;
    if (to_draft > draft_pool.size())
        to_draft = draft_pool.size();

    for (size_t i = 0; i < to_draft; i++)
    {
        int32_t squad_size = 10;
        if (citizen_military + i < 4 * 8)
            squad_size = 8;
        if (citizen_military + i < 4 * 6)
            squad_size = 6;
        if (citizen_military + i < 3 * 4)
            squad_size = 4;

        df::squad *squad = find_squad_with_vacancy(squad_size);
        if (!squad)
        {
            squad = create_squad(out, ai);
            if (!squad)
                break;
        }

        draft_unit(out, ai, draft_pool[i], squad);
    }
}

bool Population::military_random_squad_attack_unit(color_ostream & out, df::unit *u, const std::string & reason)
{
    auto entity = plotinfo->main.fortress_entity;
    if (!entity || entity->squads.empty())
        return false;

    int32_t squad_id = entity->squads[ai.rng() % entity->squads.size()];
    auto squad = df::squad::find(squad_id);
    if (!squad)
        return false;

    return military_squad_attack_unit(out, squad, u, reason);
}

bool Population::military_all_squads_attack_unit(color_ostream & out, df::unit *u, const std::string & reason)
{
    auto entity = plotinfo->main.fortress_entity;
    if (!entity)
        return false;

    bool any = false;
    for (auto squad_id : entity->squads)
    {
        auto squad = df::squad::find(squad_id);
        if (!squad)
            continue;
        any = military_squad_attack_unit(out, squad, u, reason) || any;
    }
    return any;
}

bool Population::military_squad_attack_unit(color_ostream & out, df::squad *squad, df::unit *u, const std::string & reason)
{
    if (!squad || !u)
        return false;

    for (auto order : squad->orders)
    {
        if (auto kill = virtual_cast<df::squad_order_kill_listst>(order))
        {
            if (std::find(kill->units.begin(), kill->units.end(), u->id) != kill->units.end())
                return false;
        }
    }

    auto kill = df::allocate<df::squad_order_kill_listst>();
    if (!kill)
        return false;

    kill->units.push_back(u->id);
    squad->orders.push_back(kill);

    ai.debug(out, stl_sprintf("[military] squad %d attack %s (%s)", squad->id, AI::describe_unit(u).c_str(), reason.c_str()));
    return true;
}

bool Population::military_cancel_attack_order(color_ostream & out, df::unit *u, const std::string & reason)
{
    auto entity = plotinfo->main.fortress_entity;
    if (!entity)
        return false;

    bool any = false;
    for (auto squad_id : entity->squads)
    {
        auto squad = df::squad::find(squad_id);
        if (!squad)
            continue;
        any = military_cancel_attack_order(out, squad, u, reason) || any;
    }
    return any;
}

bool Population::military_cancel_attack_order(color_ostream & out, df::squad *squad, df::unit *u, const std::string & reason)
{
    if (!squad || !u)
        return false;

    bool found = false;
    for (auto it = squad->orders.begin(); it != squad->orders.end(); )
    {
        if (auto kill = virtual_cast<df::squad_order_kill_listst>(*it))
        {
            auto uid_it = std::find(kill->units.begin(), kill->units.end(), u->id);
            if (uid_it != kill->units.end())
            {
                kill->units.erase(uid_it);
                found = true;

                if (kill->units.empty())
                {
                    delete *it;
                    it = squad->orders.erase(it);
                    continue;
                }
            }
        }
        it++;
    }

    if (found)
    {
        ai.debug(out, stl_sprintf("[military] squad %d cancel attack on %s (%s)", squad->id, AI::describe_unit(u).c_str(), reason.c_str()));
    }
    return found;
}
