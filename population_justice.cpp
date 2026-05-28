#include "ai.h"
#include "population.h"

#include "modules/Units.h"

#include "df/building_civzonest.h"
#include "df/crime.h"
#include "df/general_ref_contains_itemst.h"
#include "df/general_ref_contains_unitst.h"
#include "df/item.h"
#include "df/items_other_id.h"
#include "df/plotinfost.h"
#include "df/world.h"

REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(cur_year_tick);
REQUIRE_GLOBAL(plotinfo);
REQUIRE_GLOBAL(world);

void Population::update_caged(color_ostream & out)
{
    int32_t count = 0;
    for (auto it = world->items.other[items_other_id::CAGE].begin(); it != world->items.other[items_other_id::CAGE].end(); it++)
    {
        df::item *cage = *it;
        if (!cage->flags.bits.on_ground)
        {
            bool ignore = true;
            if (cage->flags.bits.forbid && !cage->flags.bits.trader)
            {
                for (auto ref : cage->general_refs)
                {
                    auto bld = ref->getBuilding();
                    if (ref->getType() == general_ref_type::BUILDING_HOLDER && bld && bld->getType() == building_type::TradeDepot)
                    {
                        ignore = false;
                        break;
                    }
                }
            }
            if (ignore)
            {
                continue;
            }
        }
        for (auto ref = cage->general_refs.begin(); ref != cage->general_refs.end(); ref++)
        {
            if (virtual_cast<df::general_ref_contains_itemst>(*ref))
            {
                df::item *i = (*ref)->getItem();
                if (i->flags.bits.dump && !i->flags.bits.forbid)
                {
                    continue;
                }
                count++;
                i->flags.bits.dump = 1;
                i->flags.bits.forbid = 0;
            }
            else if (virtual_cast<df::general_ref_contains_unitst>(*ref))
            {
                df::unit *u = (*ref)->getUnit();
                if (Units::isOwnCiv(u))
                {
                    room *r = ai.find_room(room_type::releasecage, [](room *r) -> bool { return r->status >= room_status::finished; });
                    if (r)
                    {
                        for (auto f : r->layout)
                        {
                            if (f->type == layout_type::lever && f->bld_id != -1)
                            {
                                ai.plan.add_task(task_type::rescue_caged, r, f, cage->id);
                                break;
                            }
                        }
                    }
                }
                else
                {
                    size_t waiting_items = 0;

                    for (auto ii : u->inventory)
                    {
                        if (Items::getOwner(ii->item))
                        {
                            continue;
                        }
                        waiting_items++;
                        if (ii->item->flags.bits.dump && !ii->item->flags.bits.forbid)
                        {
                            continue;
                        }
                        count++;
                        ii->item->flags.bits.dump = 1;
                        ii->item->flags.bits.forbid = 0;
                        ai.debug(out, "pop: marked item " + AI::describe_item(ii->item) + " for dumping");
                    }

                    if (!waiting_items)
                    {
                        room *r = ai.find_room(room_type::pitcage, [](room *r) -> bool { return r->dfbuilding(); });
                        if (r && AI::spiral_search(r->pos(), 1, 1, [cage](df::coord t) -> bool { return t == cage->pos; }).isValid())
                        {
                            assign_unit_to_zone(u, virtual_cast<df::building_civzonest>(r->dfbuilding()));
                            ai.debug(out, "pop: marked " + AI::describe_unit(u) + " for pitting");
                            military_random_squad_attack_unit(out, u, "just in case pitting fails");
                        }
                        else
                        {
                            military_cancel_attack_order(out, u, "caged, but not in place for pitting");
                        }
                    }
                    else
                    {
                        ai.debug(out, stl_sprintf("pop: waiting for %s to be stripped for pitting (%zu items remain)", AI::describe_unit(u).c_str(), waiting_items));
                        military_cancel_attack_order(out, u, "caged, but not ready for pitting");
                    }
                }
            }
        }
    }
    if (count > 0)
    {
        ai.debug(out, stl_sprintf("pop: dumped %d items from cages", count));
    }
}

void Population::update_crimes(color_ostream &)
{
    last_checked_crime_year = *cur_year;
    last_checked_crime_tick = *cur_year_tick;
}
