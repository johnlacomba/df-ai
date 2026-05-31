#include "ai.h"
#include "population.h"

#include "df/activity_entry.h"
#include "df/activity_event_conflictst.h"
#include "df/creature_raw.h"
#include "df/historical_entity.h"
#include "df/job.h"
#include "df/squad.h"
#include "df/squad_order_kill_listst.h"
#include "df/tile_designation.h"
#include "df/plotinfost.h"
#include "df/unit.h"
#include "df/world.h"

#include "modules/Maps.h"
#include "modules/Units.h"

REQUIRE_GLOBAL(plotinfo);
REQUIRE_GLOBAL(world);

bool AI::tag_enemies(color_ostream & out)
{
    debug(out, "[tag_enemies] start");
    bool found = false;
    if (!plotinfo->main.fortress_entity)
    {
        return false;
    }
    for (auto id : plotinfo->main.fortress_entity->squads)
    {
        auto squad = df::squad::find(id);
        if (!squad)
        {
            continue;
        }
        for (auto order : squad->orders)
        {
            if (auto kill = virtual_cast<df::squad_order_kill_listst>(order))
            {
                for (auto unit_id : kill->units)
                {
                    auto unit = df::unit::find(unit_id);
                    if (unit && std::find(world->units.active.begin(), world->units.active.end(), unit) == world->units.active.end())
                    {
                        found = pop.military_cancel_attack_order(out, unit, "unit no longer active on map") || found;
                    }
                }
            }
        }
    }
    for (auto it = world->units.active.rbegin(); it != world->units.active.rend(); it++)
    {
        df::unit *u = *it;
        df::creature_raw *race = df::creature_raw::find(u->race);
        auto *_td49 = Units::getPosition(u).isValid() ? Maps::getTileDesignation(Units::getPosition(u)) : nullptr;
        if (!Units::isDead(u) && Units::getPosition(u).isValid() &&
            !Units::isOwnCiv(u) && Units::getContainer(u) == nullptr &&
            _td49 && !_td49->bits.hidden)
        {
            std::string threat_reason;
            if (race && race->flags.is_set(creature_raw_flags::HAS_ANY_MEGABEAST))
            {
                threat_reason = "primary antagonist: megabeast";
                found = pop.military_all_squads_attack_unit(out, u, threat_reason) || found;
            }
            else if (race && race->flags.is_set(creature_raw_flags::HAS_ANY_SEMIMEGABEAST))
            {
                threat_reason = "primary antagonist: semi-megabeast";
                found = pop.military_all_squads_attack_unit(out, u, threat_reason) || found;
            }
            else if (race && race->flags.is_set(creature_raw_flags::HAS_ANY_FEATURE_BEAST))
            {
                threat_reason = "primary antagonist: forgotten beast";
                found = pop.military_all_squads_attack_unit(out, u, threat_reason) || found;
            }
            else if (race && race->flags.is_set(creature_raw_flags::HAS_ANY_TITAN))
            {
                threat_reason = "primary antagonist: titan";
                found = pop.military_all_squads_attack_unit(out, u, threat_reason) || found;
            }
            else if (race && race->flags.is_set(creature_raw_flags::HAS_ANY_UNIQUE_DEMON))
            {
                threat_reason = "primary antagonist: demon";
                found = pop.military_all_squads_attack_unit(out, u, threat_reason) || found;
            }
            else if (race && race->flags.is_set(creature_raw_flags::HAS_ANY_DEMON))
            {
                threat_reason = "antagonist: demon";
                found = pop.military_all_squads_attack_unit(out, u, threat_reason) || found;
            }
            else if (race && race->flags.is_set(creature_raw_flags::HAS_ANY_NIGHT_CREATURE))
            {
                threat_reason = "antagonist: night creature";
                found = pop.military_all_squads_attack_unit(out, u, threat_reason) || found;
            }
            else if (Units::isOpposedToLife(u))
            {
                threat_reason = "undead";
                found = pop.military_random_squad_attack_unit(out, u, threat_reason) || found;
            }
            else if (u->flags1.bits.active_invader)
            {
                threat_reason = "active invader";
                found = pop.military_random_squad_attack_unit(out, u, threat_reason) || found;
            }
            else if (u->flags1.bits.marauder)
            {
                threat_reason = "marauder";
                found = pop.military_random_squad_attack_unit(out, u, threat_reason) || found;
            }
            else if (u->flags2.bits.underworld)
            {
                threat_reason = "underworld creature";
                found = pop.military_random_squad_attack_unit(out, u, threat_reason) || found;
            }
            else if (u->flags2.bits.visitor_uninvited)
            {
                threat_reason = "uninvited visitor";
                found = pop.military_random_squad_attack_unit(out, u, threat_reason) || found;
            }
            else if (auto hunter = is_hunting_target(u))
            {
                found = pop.military_cancel_attack_order(out, u, "hunting target of " + AI::describe_unit(hunter)) || found;
            }
            else if (auto citizen = u->flags2.bits.roaming_wilderness_population_source ? is_attacking_citizen(u) : nullptr)
            {
                threat_reason = "attacking citizen: " + AI::describe_unit(citizen);
                found = pop.military_random_squad_attack_unit(out, u, threat_reason) || found;
            }
            if (!threat_reason.empty())
            {
                camera.queue_event(CAMERA_TIER_COMBAT, Units::getPosition(u), "combat (" + threat_reason + "): " + AI::describe_unit(u));
            }
        }
    }
    return found;
}

df::unit *AI::is_attacking_citizen(df::unit * /* u */)
{
    // activity_event_conflictst::T_sides, conflict_sidest, and related
    // conflict side/enemy structures removed in Steam DF.
    // This function needs rewriting for the new conflict API.
    df::unit *citizen = nullptr;

    return citizen;
}

df::unit *AI::is_hunting_target(df::unit *u)
{
    for (auto c : world->units.active)
    {
        if (Units::isSane(c) && Units::isCitizen(c) && c->job.current_job && c->job.current_job->job_type == job_type::Hunt && c->job.hunt_target == u)
        {
            return c;
        }
    }

    return nullptr;
}

bool AI::is_in_conflict(df::unit *u, std::function<bool(df::activity_event_conflictst *)> filter)
{
    for (auto id : u->activities)
    {
        if (auto act = df::activity_entry::find(id))
        {
            for (auto event : act->events)
            {
                if (auto conflict = virtual_cast<df::activity_event_conflictst>(event))
                {
                    if (filter(conflict))
                    {
                        return true;
                    }
                }
            }
        }
    }

    return false;
}
