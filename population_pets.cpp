#include "ai.h"
#include "population.h"
#include "plan.h"

#include <algorithm>

#include "modules/Buildings.h"
#include "modules/Units.h"

#include "df/building_civzonest.h"
#include "df/unit.h"
#include "df/building_nest_boxst.h"
#include "df/caste_raw.h"
#include "df/creature_raw.h"
#include "df/general_ref.h"
#include "df/general_ref_building_civzone_assignedst.h"
#include "df/item_eggst.h"
#include "df/manager_order.h"
#include "df/manager_order_template.h"
#include "df/training_assignment.h"
#include "df/plotinfost.h"
#include "df/unit_misc_trait.h"
#include "df/unit_relationship_type.h"
#include "df/unit_wound.h"
#include "df/world.h"

REQUIRE_GLOBAL(plotinfo);
REQUIRE_GLOBAL(world);

void Population::update_pets(color_ostream & out)
{
    if (!ai.plan.pastures_ready(out))
    {
        // will check next time
        return;
    }

    int32_t needmilk = 0;
    int32_t needshear = 0;
    for (auto mo : world->manager_orders.all)
    {
        if (mo->job_type == job_type::MilkCreature)
        {
            needmilk -= mo->amount_left;
        }
        else if (mo->job_type == job_type::ShearCreature)
        {
            needshear -= mo->amount_left;
        }
    }

    std::map<df::caste_raw *, std::set<std::pair<int32_t, df::unit *>>> forSlaughter;

    std::map<int32_t, pet_flags> np = pet;
    for (auto it : pet_check)
    {
        np[it]; // make sure existing pasture assignments are checked
    }
    pet_check.clear();
    for (auto u : world->units.active)
    {
        df::creature_raw *race = df::creature_raw::find(u->race);
        df::caste_raw *cst = race->caste[u->caste];

        if (cst->flags.is_set(caste_raw_flags::CAN_LEARN))
        {
            continue;
        }

        if (u->flags1.bits.inactive || u->flags1.bits.merchant || u->flags1.bits.forest || u->flags2.bits.visitor || u->flags2.bits.slaughter)
        {
            continue;
        }

        if (u->flags1.bits.caged && u->civ_id == -1 && u->cultural_identity == -1 && u->training_level == animal_training_level::WildUntamed)
        {
            // train any caught wild animals
            if (!df::training_assignment::find(u->id))
            {
                auto asn = df::allocate<df::training_assignment>();
                asn->animal_id = u->id;
                asn->trainer_id = -1;
                asn->flags.whole = 0;
                asn->flags.bits.any_trainer = true;
                // plotinfo->equipment.training_assignments moved in Steam DF
                // TODO: find new location for training_assignments
                // insert_into_vector(plotinfo->equipment.training_assignments, &df::training_assignment::animal_id, asn);
                delete asn;
            }

            continue;
        }

        if (!Units::isOwnCiv(u) || Units::isOwnGroup(u) || Units::isOwnRace(u) || u->cultural_identity != -1)
        {
            continue;
        }

        int32_t age = days_since(u->birth_year, u->birth_time);

        if (u->training_level > animal_training_level::SemiWild && u->training_level < animal_training_level::Domesticated)
        {
            u->flags2.bits.slaughter = 1;
            ai.debug(out, stl_sprintf("marked %dy%dm%dd old %s:%s for slaughter (trained wild animal)", age / 12 / 28, (age / 28) % 12, age % 28, race->creature_id.c_str(), cst->caste_id.c_str()));
            continue;
        }

        if (pet.count(u->id))
        {
            // caste_raw::body_size_2 removed in Steam DF — body size data restructured
            if (true && // TODO: check full grown using new body size API
                u->profession != profession::TRAINED_HUNTER && // not trained
                u->profession != profession::TRAINED_WAR && // not trained
                u->relationship_ids[unit_relationship_type::Pet] == -1) // not owned
            {
                // unit_wound::T_parts removed in Steam DF — wound structure changed
                // TODO: rewrite gelded check using new wound API
                if (cst->sex == pronoun_type::it)
                {
                    // animal can't reproduce, can't work, and will provide maximum butchering reward. kill it.
                    u->flags2.bits.slaughter = true;
                    ai.debug(out, stl_sprintf("marked %dy%dm%dd old %s:%s for slaughter (can't reproduce)", age / 12 / 28, (age / 28) % 12, age % 28, race->creature_id.c_str(), cst->caste_id.c_str()));
                    continue;
                }

                forSlaughter[cst].insert(std::make_pair(age, u));
            }

            if (pet.at(u->id).bits.milkable && !Units::isBaby(u) && !Units::isChild(u))
            {
                bool have = false;
                for (auto mt : u->status.misc_traits)
                {
                    if (mt->id == misc_trait_type::MilkCounter)
                    {
                        have = true;
                        break;
                    }
                }
                if (!have)
                {
                    needmilk++;
                }
            }

            if (pet.at(u->id).bits.shearable && !Units::isBaby(u) && !Units::isChild(u))
            {
                // shearable_tissue_layerst removed in Steam DF
                // TODO: rewrite shearing check using new tissue layer API
                needshear++;
            }

            np.erase(u->id);
            continue;
        }

        pet_flags flags;
        flags.bits.milkable = 0;
        flags.bits.shearable = 0;
        flags.bits.hunts_vermin = 0;
        flags.bits.trainable = 0;
        flags.bits.grazer = 0;
        flags.bits.lays_eggs = 0;

        if (cst->flags.is_set(caste_raw_flags::MILKABLE))
        {
            flags.bits.milkable = 1;
        }

        // shearable_tissue_layerst removed in Steam DF
        // TODO: determine new way to check if caste is shearable
        // flags.bits.shearable = 1; // disabled until new API is known

        if (cst->flags.is_set(caste_raw_flags::HUNTS_VERMIN))
        {
            flags.bits.hunts_vermin = 1;
        }

        if (cst->flags.is_set(caste_raw_flags::TRAINABLE_HUNTING) || cst->flags.is_set(caste_raw_flags::TRAINABLE_WAR))
        {
            flags.bits.trainable = 1;
        }

        if (cst->flags.is_set(caste_raw_flags::GRAZER))
        {
            flags.bits.grazer = 1;

            if (auto bld = virtual_cast<df::building_civzonest>(ai.plan.getpasture(out, u->id)))
            {
                assign_unit_to_zone(u, bld);
                // TODO monitor grass levels
            }
            else if (u->relationship_ids[df::unit_relationship_type::Pet] == -1 && !cst->flags.is_set(caste_raw_flags::CAN_LEARN))
            {
                // TODO slaughter best candidate, keep this one
                u->flags2.bits.slaughter = 1;
                ai.debug(out, stl_sprintf("marked %dy%dm%dd old %s:%s for slaughter (no pasture)", age / 12 / 28, (age / 28) % 12, age % 28, race->creature_id.c_str(), cst->caste_id.c_str()));
            }
        }

        if (cst->flags.is_set(caste_raw_flags::LAYS_EGGS))
        {
            flags.bits.lays_eggs = 1;
        }

        pet[u->id] = flags;
    }

    for (auto p : np)
    {
        ai.plan.freepasture(out, p.first);
        pet.erase(p.first);
    }

    for (auto & cst : forSlaughter)
    {
        // we have reproductively viable animals, but there are more than 3 of
        // this sex (full-grown). kill the oldest ones for meat/leather/bones.

        if (cst.second.size() > 3)
        {
            // remove the youngest 3
            auto it = cst.second.begin();
            std::advance(it, 3);
            cst.second.erase(cst.second.begin(), it);

            for (auto it : cst.second)
            {
                int32_t age = it.first;
                df::unit *u = it.second;
                df::creature_raw *race = df::creature_raw::find(u->race);
                u->flags2.bits.slaughter = 1;
                ai.debug(out, stl_sprintf("marked %dy%dm%dd old %s:%s for slaughter (too many adults)", age / 12 / 28, (age / 28) % 12, age % 28, race->creature_id.c_str(), cst.first->caste_id.c_str()));
            }
        }
    }

    if (needmilk > 0)
    {
        df::manager_order_template tmpl;
        tmpl.job_type = job_type::MilkCreature;
        tmpl.mat_index = -1;

        ai.stocks.add_manager_order(out, tmpl, std::min(needmilk, 30));
    }

    if (needshear > 0)
    {
        df::manager_order_template tmpl;
        tmpl.job_type = job_type::ShearCreature;
        tmpl.mat_index = -1;

        ai.stocks.add_manager_order(out, tmpl, std::min(needshear, 30));
    }

    for (auto bld : world->buildings.other[buildings_other_id::NEST_BOX])
    {
        auto box = virtual_cast<df::building_nest_boxst>(bld);
        if (!box || box->getBuildStage() != box->getMaxBuildStage())
        {
            continue;
        }

        if (box->claimed_by == -1)
        {
            continue;
        }

        for (auto item : box->contained_items)
        {
            if (auto egg = virtual_cast<df::item_eggst>(item->item))
            {
                if (egg->egg_flags.bits.fertile)
                {
                    // baby chicks are preferable over cooked eggs.
                    egg->flags.bits.forbid = true;
                }
            }
        }
    }
}

void Population::assign_unit_to_zone(df::unit *u, df::building_civzonest *bld)
{
    if (auto ref = Units::getGeneralRef(u, general_ref_type::BUILDING_CIVZONE_ASSIGNED))
    {
        if (ref->getBuilding() == bld)
        {
            return;
        }

        // remove from old zone
        if (auto oldzone = virtual_cast<df::building_civzonest>(ref->getBuilding()))
        {
            auto & units = oldzone->assigned_units;
            units.erase(std::remove(units.begin(), units.end(), u->id), units.end());
        }
        u->general_refs.erase(
            std::remove(u->general_refs.begin(), u->general_refs.end(), ref),
            u->general_refs.end());
        delete ref;
    }

    auto newref = df::allocate<df::general_ref_building_civzone_assignedst>();
    if (!newref)
        return;

    newref->building_id = bld->id;
    u->general_refs.push_back(newref);
    bld->assigned_units.push_back(u->id);
}
