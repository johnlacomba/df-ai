#include "ai.h"
#include "population.h"
#include "plan.h"

#include "modules/Units.h"

#include "df/entity_material_category.h"
#include "df/entity_position_responsibility.h"
#include "df/historical_entity.h"
#include "df/historical_figure.h"
#include "df/item_type.h"
#include "df/plotinfost.h"
#include "df/squad.h"
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

REQUIRE_GLOBAL(plotinfo);
REQUIRE_GLOBAL(world);

static int32_t next_squad_id()
{
    int32_t max_id = -1;
    for (auto sq : world->squads.all)
    {
        if (sq->id > max_id)
            max_id = sq->id;
    }
    return max_id + 1;
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
    auto routine = df::allocate<df::squad_routine_schedulest>();
    if (!routine)
        return;

    for (int month = 0; month < 12; month++)
    {
        auto & entry = routine->month[month];
        entry.sleep_mode = squad_sleep_option_type::InBarracksAtWill;
        entry.uniform_mode = squad_civilian_uniform_type::None;

        auto sched_order = df::allocate<df::squad_schedule_order>();
        if (sched_order)
        {
            sched_order->order = df::allocate<df::squad_order_trainst>();
            sched_order->min_count = std::max(1, (int)squad->positions.size() / 2);
            entry.orders.push_back(sched_order);
        }
    }

    squad->schedule.routine.push_back(routine);
    squad->cur_routine_idx = 0;
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

    squad->id = next_squad_id();
    squad->entity_id = entity->id;
    squad->leader_position = -1;
    squad->leader_assignment = -1;
    squad->assigned_army_controller_id = -1;
    squad->cur_routine_idx = 0;

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
        if (pos.position->responsibilities[entity_position_responsibility::ACCOUNTING] ||
            pos.position->responsibilities[entity_position_responsibility::MANAGE_PRODUCTION] ||
            pos.position->responsibilities[entity_position_responsibility::TRADE])
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
    if (!u || !squad)
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

    if (citizen.size() < 7)
        return;

    size_t target_min = citizen.size() * military_min / 100;
    size_t target_max = citizen.size() * military_max / 100;

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
        if (u->status.labors[unit_labor::MINE] || u->status.labors[unit_labor::CUTWOOD] || u->status.labors[unit_labor::HUNT])
            continue;

        draft_pool.push_back(u);
    }

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
