#include "ai.h"
#include "plan.h"
#include "debug.h"

#include "modules/Buildings.h"
#include "modules/Job.h"
#include "modules/Maps.h"

#include "df/block_square_event_material_spatterst.h"
#include "df/building_archerytargetst.h"
#include "df/building_civzonest.h"
#include "df/building_coffinst.h"
#include "df/building_def_furnacest.h"
#include "df/building_def_item.h"
#include "df/building_def_workshopst.h"
#include "df/building_doorst.h"
#include "df/building_floodgatest.h"
#include "df/building_furnacest.h"
#include "df/building_hatchst.h"
#include "df/building_stockpilest.h"
#include "df/buildingitemst.h"
#include "df/building_tablest.h"
#include "df/building_trapst.h"
#include "df/building_workshopst.h"
#include "df/builtin_mats.h"
#include "df/general_ref_building_holderst.h"
#include "df/general_ref_building_triggertargetst.h"
#include "df/item_boulderst.h"
#include "df/item.h"
#include "df/job.h"
#include "df/job_item.h"
#include "df/map_block.h"
#include "df/abstract_building_guildhallst.h"
#include "df/abstract_building_hospitalst.h"
#include "df/abstract_building_inn_tavernst.h"
#include "df/abstract_building_libraryst.h"
#include "df/abstract_building_templest.h"
#include "df/plant.h"
#include "df/plant_raw.h"
#include "df/plotinfost.h"
#include "df/viewscreen_dwarfmodest.h"
#include "df/world.h"
#include "df/world_site.h"

REQUIRE_GLOBAL(cursor);
REQUIRE_GLOBAL(plotinfo);
REQUIRE_GLOBAL(world);

static bool find_item(df::items_other_id idx, df::item *&item, bool fire_safe = false, bool non_economic = false)
{
    for (auto it = world->items.other[idx].begin(); it != world->items.other[idx].end(); it++)
    {
        df::item *i = *it;
        if (!Stocks::is_item_free(i))
            continue;
        if (fire_safe && !i->isTemperatureSafe(1))
            continue;
        if (non_economic)
        {
            auto boulder = virtual_cast<df::item_boulderst>(i);
            if (!boulder)
                continue;
            if (boulder->mat_type == 0 && plotinfo->economic_stone[boulder->mat_index])
                continue;
        }
        item = i;
        return true;
    }
    return false;
}

static bool find_items(df::items_other_id idx, std::vector<df::item *> & items, size_t n, bool fire_safe = false, bool non_economic = false)
{
    size_t j = 0;
    for (auto it = world->items.other[idx].begin(); it != world->items.other[idx].end(); it++)
    {
        df::item *i = *it;
        if (!Stocks::is_item_free(i))
            continue;
        if (fire_safe && !i->isTemperatureSafe(1))
            continue;
        if (non_economic)
        {
            auto boulder = virtual_cast<df::item_boulderst>(i);
            if (!boulder)
                continue;
            if (boulder->mat_type == 0 && plotinfo->economic_stone[boulder->mat_index])
                continue;
        }
        items.push_back(i);
        j++;
        if (j == n)
            return true;
    }
    return false;
}

// Not perfect, but it should at least cut down on cancellation spam.
static bool find_items(const std::vector<df::job_item *> & filters, std::vector<df::item *> & items, std::ostream & reason)
{
    bool found_all = true;

    for (auto filter : filters)
    {
        int32_t found = 0;

        for (auto i : world->items.other[items_other_id::IN_PLAY])
        {
            if (std::find(items.begin(), items.end(), i) != items.end())
            {
                continue;
            }

            ItemTypeInfo iinfo(i);
            MaterialInfo minfo(i);
            if (!iinfo.matches(*filter, &minfo, true) || (filter->item_type != item_type::NONE && i->getType() != filter->item_type))
            {
                continue;
            }

            if (!Stocks::is_item_free(i))
            {
                continue;
            }

            items.push_back(i);
            found++;

            if (filter->quantity <= found)
            {
                break;
            }
        }

        if (filter->quantity > found)
        {
            if (found_all)
            {
                reason << "could not find ";
                found_all = false;
            }
            else
            {
                reason << " or ";
            }

            reason << ItemTypeInfo(filter->item_type, filter->item_subtype).toString();
        }
    }
    return found_all;
}

template<typename T>
static df::job_item *make_job_item(T *t)
{
    df::job_item *item = new df::job_item();
    item->item_type = t->item_type;
    item->item_subtype = t->item_subtype;
    item->mat_type = t->mat_type;
    item->mat_index = t->mat_index;
    item->reaction_class = t->reaction_class;
    item->has_material_reaction_product = t->has_material_reaction_product;
    item->flags1.whole = t->flags1.whole;
    item->flags2.whole = t->flags2.whole;
    item->flags3.whole = t->flags3.whole;
    item->flags4 = t->flags4;
    item->flags5 = t->flags5;
    item->metal_ore = t->metal_ore;
    item->min_dimension = t->min_dimension;
    item->quantity = t->quantity;
    item->has_tool_use = t->has_tool_use;
    return item;
}

bool Plan::construct_room(color_ostream & out, room *r)
{
    ai.debug(out, "construct " + AI::describe_room(r));


    if (r->required_value > 0)
    {
        add_task(task_type::monitor_room_value, r);
    }

    if (r->type == room_type::corridor)
    {
        return furnish_room(out, r);
    }

    if (r->type == room_type::stockpile)
    {
        furnish_room(out, r);
        add_task(task_type::construct_stockpile, r);
        return true;
    }

    if (r->type == room_type::tradedepot)
    {
        add_task(task_type::construct_tradedepot, r);
        return true;
    }

    if (r->type == room_type::workshop)
    {
        add_task(task_type::construct_workshop, r);
        return true;
    }

    if (r->type == room_type::furnace)
    {
        add_task(task_type::construct_furnace, r);
        return true;
    }

    if (r->type == room_type::farmplot)
    {
        add_task(task_type::construct_farmplot, r);
        return true;
    }

    if (r->type == room_type::windmill)
    {
        add_task(task_type::construct_windmill, r);
        return true;
    }

    if (r->type == room_type::cistern)
    {
        return construct_cistern(out, r);
    }

    if (r->type == room_type::cemetery)
    {
        return furnish_room(out, r);
    }

    if (r->type == room_type::infirmary || r->type == room_type::pasture || r->type == room_type::pitcage || r->type == room_type::pond || r->type == room_type::location || r->type == room_type::garbagedump)
    {
        furnish_room(out, r);
        std::ostringstream discard;
        if (try_construct_activityzone(out, r, discard))
            return true;
        add_task(task_type::construct_activityzone, r);
        return true;
    }

    if (r->type == room_type::dininghall)
    {
        if (!r->temporary)
        {
            if (room *t = ai.find_room(room_type::dininghall, [](room *r) -> bool { return r->temporary; }))
            {
                move_dininghall_fromtemp(out, r, t);
            }
        }
        return furnish_room(out, r);
    }

    return furnish_room(out, r);
}

bool Plan::furnish_room(color_ostream &, room *r)
{
    for (auto it = r->layout.begin(); it != r->layout.end(); it++)
    {
        furniture *f = *it;
        add_task(task_type::furnish, r, f);
    }
    r->status = room_status::finished;
    return true;
}

const static struct traptypes
{
    std::map<std::string, df::trap_type> map;
    traptypes()
    {
        map["cage"] = trap_type::CageTrap;
        map["lever"] = trap_type::Lever;
        map["trackstop"] = trap_type::TrackStop;
    }
} traptypes;

bool Plan::try_furnish(color_ostream & out, room *r, furniture *f, std::ostream & reason)
{
    if (f->bld_id != -1)
        return true;
    if (f->ignore)
        return true;

    df::coord tgtile = r->min + f->pos;
    DFAI_ASSERT_VALID_TILE(tgtile, " (furniture position for " << AI::describe_furniture(f) << " in room " << AI::describe_room(r) << ")");

    df::tiletype *tt_ptr = Maps::getTileType(tgtile);
    if (!tt_ptr)
    {
        reason << "tile not loaded";
        return false;
    }
    df::tiletype tt = *tt_ptr;
    if (f->construction != construction_type::NONE)
    {
        if (try_furnish_construction(out, f->construction, tgtile, reason))
        {
            if (f->type == layout_type::none)
                return true;
        }
        else
        {
            return false; // don't try to furnish item before construction is done
        }
    }

    if (ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, tt)) == tiletype_shape_basic::Wall)
    {
        reason << "waiting for wall to be excavated";
        return false;
    }

    df::building_type building_type = building_type::NONE;
    int building_subtype = -1;
    stock_item::item stocks_furniture_type;

    switch (f->type)
    {
    case layout_type::none:
        return true;

    case layout_type::archery_target:
        return try_furnish_archerytarget(out, r, f, tgtile, reason);
    case layout_type::armor_stand:
        building_type = building_type::Armorstand;
        stocks_furniture_type = stock_item::armor_stand;
        break;
    case layout_type::bed:
        building_type = building_type::Bed;
        stocks_furniture_type = stock_item::bed;
        break;
    case layout_type::bookcase:
        building_type = building_type::Bookcase;
        stocks_furniture_type = stock_item::bookcase;
        break;
    case layout_type::cabinet:
        building_type = building_type::Cabinet;
        stocks_furniture_type = stock_item::cabinet;
        break;
    case layout_type::cage:
        building_type = building_type::Cage;
        stocks_furniture_type = stock_item::cage_metal;
        break;
    case layout_type::cage_trap:
        if (ai.stocks.count_free[stock_item::cage] < 1)
        {
            // avoid too much spam
            reason << "no empty cages available";
            return false;
        }
        building_type = building_type::Trap;
        building_subtype = trap_type::CageTrap;
        stocks_furniture_type = stock_item::mechanism;
        break;
    case layout_type::chair:
        building_type = building_type::Chair;
        stocks_furniture_type = stock_item::chair;
        break;
    case layout_type::chest:
        building_type = building_type::Box;
        stocks_furniture_type = stock_item::chest;
        break;
    case layout_type::coffin:
        building_type = building_type::Coffin;
        stocks_furniture_type = stock_item::coffin;
        break;
    case layout_type::door:
    {
        auto check_wall = [&](int16_t dx, int16_t dy) -> bool
        {
            df::tiletype *ttp = Maps::getTileType(tgtile.x + dx, tgtile.y + dy, tgtile.z);
            if (!ttp) return false;
            return ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *ttp)) == tiletype_shape_basic::Wall;
        };
        if (!check_wall(-1, 0) && !check_wall(1, 0) && !check_wall(0, -1) && !check_wall(0, 1))
        {
            reason << "need adjacent wall";
            return false;
        }
        building_type = building_type::Door;
        stocks_furniture_type = stock_item::door;
        break;
    }
    case layout_type::floodgate:
        // require the floor to be smooth before we build a floodgate on it
        // because we can't smooth a floor under an open floodgate.
        if (!is_smooth(tgtile))
        {
            std::set<df::coord> tiles;
            tiles.insert(tgtile);
            smooth(tiles);
            reason << "floor under floodgate is not smooth";
            return false;
        }
        building_type = building_type::Floodgate;
        stocks_furniture_type = stock_item::floodgate;
        break;
    case layout_type::gear_assembly:
        building_type = building_type::GearAssembly;
        stocks_furniture_type = stock_item::mechanism;
        break;
    case layout_type::hatch:
        building_type = building_type::Hatch;
        stocks_furniture_type = stock_item::hatch_cover;
        break;
    case layout_type::hive:
        building_type = building_type::Hive;
        stocks_furniture_type = stock_item::hive;
        break;
    case layout_type::lever:
        building_type = building_type::Trap;
        building_subtype = trap_type::Lever;
        stocks_furniture_type = stock_item::mechanism;
        break;
    case layout_type::nest_box:
        building_type = building_type::NestBox;
        stocks_furniture_type = stock_item::nest_box;
        break;
    case layout_type::offering_place:
        building_type = building_type::OfferingPlace;
        stocks_furniture_type = stock_item::offering_place;
        break;
    case layout_type::pedestal:
        building_type = building_type::DisplayFurniture;
        stocks_furniture_type = stock_item::pedestal;
        break;
    case layout_type::restraint:
        building_type = building_type::Chain;
        stocks_furniture_type = stock_item::rope;
        break;
    case layout_type::roller:
        return try_furnish_roller(out, r, f, tgtile, reason);
    case layout_type::statue:
        building_type = building_type::Statue;
        stocks_furniture_type = stock_item::statue;
        break;
    case layout_type::table:
        building_type = building_type::Table;
        stocks_furniture_type = stock_item::table;
        break;
    case layout_type::track_stop:
        building_type = building_type::Trap;
        building_subtype = trap_type::TrackStop;
        stocks_furniture_type = stock_item::mechanism;
        break;
    case layout_type::traction_bench:
        building_type = building_type::TractionBench;
        stocks_furniture_type = stock_item::traction_bench;
        break;
    case layout_type::vertical_axle:
        building_type = building_type::AxleVertical;
        stocks_furniture_type = stock_item::wood;
        break;
    case layout_type::weapon_rack:
        building_type = building_type::Weaponrack;
        stocks_furniture_type = stock_item::weapon_rack;
        break;
    case layout_type::well:
        return try_furnish_well(out, r, f, tgtile, reason);

    case layout_type::_layout_type_count:
        return true;
    }

    if (cache_nofurnish.count(stocks_furniture_type))
    {
        reason << "no " << stocks_furniture_type << " available";
        return false;
    }

    if (Maps::getTileOccupancy(tgtile)->bits.building != tile_building_occ::None)
    {
        // TODO warn if this stays for too long?
        reason << "tile occupied by building";
        return false;
    }

    if (ENUM_ATTR(tiletype, shape, tt) == tiletype_shape::RAMP)
    {
        AI::dig_tile(tgtile, f->dig);
        reason << "tile occupied by ramp";
        return false;
    }
    auto tm = ENUM_ATTR(tiletype, material, tt);
    if (tm == tiletype_material::TREE || tm == tiletype_material::ROOT)
    {
        AI::dig_tile(tgtile, f->dig);
        reason << "tile occupied by " << enum_item_key(tm);
        return false;
    }

    if (f->type == layout_type::cage_trap)
    {
        for (auto t : tasks_generic)
        {
            if (t->type == task_type::rescue_caged)
            {
                reason << "reserving mechanisms for rescue_caged task";
                return false;
            }
        }
        for (auto t : tasks_furniture)
        {
            if (t->type == task_type::furnish && t->f->type == layout_type::lever)
            {
                reason << "reserving mechanisms for lever";
                return false;
            }
        }
        if (ai.find_room(room_type::cistern, [](room *r) -> bool
            {
                for (auto f : r->layout)
                {
                    if (f->type != layout_type::floodgate)
                    {
                        continue;
                    }
                    if (auto bld = virtual_cast<df::building_floodgatest>(df::building::find(f->bld_id)))
                    {
                        bool is_attached = false;
                        for (auto i : bld->contained_items)
                        {
                            for (auto ref : i->item->general_refs)
                            {
                                if (ref->getType() == general_ref_type::BUILDING_TRIGGER)
                                {
                                    is_attached = true;
                                    break;
                                }
                            }
                            if (is_attached)
                            {
                                break;
                            }
                        }

                        if (!is_attached)
                        {
                            return true;
                        }
                    }
                }
                return false;
            }))
        {
            reason << "reserving mechanisms for cistern";
            return false;
        }
    }

    if (df::item *itm = ai.stocks.find_free_item(stocks_furniture_type))
    {
        std::ostringstream str;
        str << "furnish " << AI::describe_furniture(f) << " in " << AI::describe_room(r);
        ai.debug(out, str.str());
        df::building *bld = Buildings::allocInstance(tgtile, building_type, building_subtype);
        Buildings::setSize(bld, df::coord(1, 1, 1));
        std::vector<df::item *> item;
        item.push_back(itm);
        Buildings::constructWithItems(bld, item);
        if (f->makeroom)
        {
            r->bld_id = bld->id;
        }
        f->bld_id = bld->id;
        add_task(task_type::check_furnish, r, f);
        return true;
    }

    cache_nofurnish.insert(stocks_furniture_type);
    reason << "no " << stocks_furniture_type << " available";
    return false;
}

bool Plan::try_furnish_well(color_ostream &, room *r, furniture *f, df::coord t, std::ostream & reason)
{
    df::item *block = nullptr;
    df::item *mecha = nullptr;
    df::item *buckt = nullptr;
    df::item *chain = nullptr;
    if (find_item(items_other_id::BLOCKS, block) &&
        find_item(items_other_id::TRAPPARTS, mecha) &&
        find_item(items_other_id::BUCKET, buckt) &&
        find_item(items_other_id::CHAIN, chain))
    {
        df::building *bld = Buildings::allocInstance(t, building_type::Well);
        Buildings::setSize(bld, df::coord(1, 1, 1));
        std::vector<df::item *> items;
        items.push_back(block);
        items.push_back(mecha);
        items.push_back(buckt);
        items.push_back(chain);
        Buildings::constructWithItems(bld, items);
        f->bld_id = bld->id;
        add_task(task_type::check_furnish, r, f);
        return true;
    }
    reason << "missing: ";
    if (!block)
    {
        reason << "block";
    }
    if (!mecha)
    {
        if (!block)
        {
            reason << ", ";
        }
        reason << "mechanisms";
    }
    if (!buckt)
    {
        if (!block || !mecha)
        {
            reason << ", ";
        }
        reason << "bucket";
    }
    if (!chain)
    {
        if (!block || !mecha || !buckt)
        {
            reason << ", ";
        }
        reason << "rope/chain";
    }
    return false;
}

bool Plan::try_furnish_archerytarget(color_ostream &, room *r, furniture *f, df::coord t, std::ostream & reason)
{
    df::item *bould = nullptr;
    if (!find_item(items_other_id::BOULDER, bould, false, true))
    {
        reason << "no boulder available";
        return false;
    }

    df::building *bld = Buildings::allocInstance(t, building_type::ArcheryTarget);
    Buildings::setSize(bld, df::coord(1, 1, 1));
    // archery_direction removed in Steam DF
    std::vector<df::item *> item;
    item.push_back(bould);
    Buildings::constructWithItems(bld, item);
    f->bld_id = bld->id;
    add_task(task_type::check_furnish, r, f);
    return true;
}

bool Plan::try_furnish_construction(color_ostream &, df::construction_type ctype, df::coord t, std::ostream & reason)
{
    df::tiletype *tt_ptr = Maps::getTileType(t);
    if (!tt_ptr)
    {
        reason << "tile not loaded";
        return false;
    }
    df::tiletype tt = *tt_ptr;
    if (ENUM_ATTR(tiletype, material, tt) == tiletype_material::TREE)
    {
        df::plant *tree = nullptr;
        AI::dig_tile(find_tree_base(t, &tree));
        if (auto plant = tree ? df::plant_raw::find(tree->material) : nullptr)
        {
            reason << plant->name << " ";
        }
        reason << "tree in the way";
        return false;
    }

    auto ts = ENUM_ATTR(tiletype, shape, tt);
    auto tsb = ENUM_ATTR(tiletype_shape, basic_shape, ts);

    switch (ctype)
    {
    case construction_type::NONE:
        break;
    case construction_type::Fortification:
        if (ts == tiletype_shape::FORTIFICATION)
        {
            return true;
        }
        break;
    case construction_type::Wall:
        if (tsb == tiletype_shape_basic::Wall)
        {
            return true;
        }
        break;
    case construction_type::Ramp:
        if (tsb == tiletype_shape_basic::Ramp)
        {
            return true;
        }
        break;
    case construction_type::UpStair:
    case construction_type::DownStair:
    case construction_type::UpDownStair:
        if (tsb == tiletype_shape_basic::Stair)
        {
            return true;
        }
        break;
    case construction_type::Floor:
        if (tsb == tiletype_shape_basic::Floor && ts != tiletype_shape::SAPLING)
        {
            return true;
        }
        if (tsb == tiletype_shape_basic::Ramp || tsb == tiletype_shape_basic::Wall)
        {
            AI::dig_tile(t);
            return true;
        }
        break;
    case construction_type::TrackN:
        if (tt == tiletype::ConstructedFloorTrackN)
            return true;
        break;
    case construction_type::TrackS:
        if (tt == tiletype::ConstructedFloorTrackS)
            return true;
        break;
    case construction_type::TrackE:
        if (tt == tiletype::ConstructedFloorTrackE)
            return true;
        break;
    case construction_type::TrackW:
        if (tt == tiletype::ConstructedFloorTrackW)
            return true;
        break;
    case construction_type::TrackNS:
        if (tt == tiletype::ConstructedFloorTrackNS)
            return true;
        break;
    case construction_type::TrackNE:
        if (tt == tiletype::ConstructedFloorTrackNE)
            return true;
        break;
    case construction_type::TrackNW:
        if (tt == tiletype::ConstructedFloorTrackNW)
            return true;
        break;
    case construction_type::TrackSE:
        if (tt == tiletype::ConstructedFloorTrackSE)
            return true;
        break;
    case construction_type::TrackSW:
        if (tt == tiletype::ConstructedFloorTrackSW)
            return true;
        break;
    case construction_type::TrackEW:
        if (tt == tiletype::ConstructedFloorTrackEW)
            return true;
        break;
    case construction_type::TrackNSE:
        if (tt == tiletype::ConstructedFloorTrackNSE)
            return true;
        break;
    case construction_type::TrackNSW:
        if (tt == tiletype::ConstructedFloorTrackNSW)
            return true;
        break;
    case construction_type::TrackNEW:
        if (tt == tiletype::ConstructedFloorTrackNEW)
            return true;
        break;
    case construction_type::TrackSEW:
        if (tt == tiletype::ConstructedFloorTrackSEW)
            return true;
        break;
    case construction_type::TrackNSEW:
        if (tt == tiletype::ConstructedFloorTrackNSEW)
            return true;
        break;
    case construction_type::TrackRampN:
        if (tt == tiletype::ConstructedRampTrackN)
            return true;
        break;
    case construction_type::TrackRampS:
        if (tt == tiletype::ConstructedRampTrackS)
            return true;
        break;
    case construction_type::TrackRampE:
        if (tt == tiletype::ConstructedRampTrackE)
            return true;
        break;
    case construction_type::TrackRampW:
        if (tt == tiletype::ConstructedRampTrackW)
            return true;
        break;
    case construction_type::TrackRampNS:
        if (tt == tiletype::ConstructedRampTrackNS)
            return true;
        break;
    case construction_type::TrackRampNE:
        if (tt == tiletype::ConstructedRampTrackNE)
            return true;
        break;
    case construction_type::TrackRampNW:
        if (tt == tiletype::ConstructedRampTrackNW)
            return true;
        break;
    case construction_type::TrackRampSE:
        if (tt == tiletype::ConstructedRampTrackSE)
            return true;
        break;
    case construction_type::TrackRampSW:
        if (tt == tiletype::ConstructedRampTrackSW)
            return true;
        break;
    case construction_type::TrackRampEW:
        if (tt == tiletype::ConstructedRampTrackEW)
            return true;
        break;
    case construction_type::TrackRampNSE:
        if (tt == tiletype::ConstructedRampTrackNSE)
            return true;
        break;
    case construction_type::TrackRampNSW:
        if (tt == tiletype::ConstructedRampTrackNSW)
            return true;
        break;
    case construction_type::TrackRampNEW:
        if (tt == tiletype::ConstructedRampTrackNEW)
            return true;
        break;
    case construction_type::TrackRampSEW:
        if (tt == tiletype::ConstructedRampTrackSEW)
            return true;
        break;
    case construction_type::TrackRampNSEW:
        if (tt == tiletype::ConstructedRampTrackNSEW)
            return true;
        break;
    default:
        break;
    }

    // fall through = must build actual construction

    if (ENUM_ATTR(tiletype, material, tt) == tiletype_material::CONSTRUCTION)
    {
        // remove existing invalid construction
        AI::dig_tile(t);
        reason << "have " << enum_item_key(tt) << " but want " << enum_item_key(ctype) << " construction";
        return false;
    }

    for (auto it = world->buildings.all.begin(); it != world->buildings.all.end(); it++)
    {
        df::building *b = *it;
        if (b->z == t.z && !b->room.extents &&
            b->x1 <= t.x && b->x2 >= t.x &&
            b->y1 <= t.y && b->y2 >= t.y)
        {
            reason << "building in the way: " << enum_item_key(b->getType());
            return false;
        }
    }

    df::item *mat = nullptr;
    if (!find_item(items_other_id::BLOCKS, mat))
    {
        if (ai.find_room(room_type::workshop, [](room *r) -> bool { return r->workshop_type == workshop_type::Masons && r->status == room_status::finished && r->dfbuilding() != nullptr; }) != nullptr)
        {
            // we don't have blocks but we can make them.
            reason << "waiting for blocks to become available";
            return false;
        }
        if (!find_item(items_other_id::BOULDER, mat, false, true))
        {
            reason << "no building materials available";
            return false;
        }
    }

    df::building *bld = Buildings::allocInstance(t, building_type::Construction, ctype);
    Buildings::setSize(bld, df::coord(1, 1, 1));
    std::vector<df::item *> item;
    item.push_back(mat);
    Buildings::constructWithItems(bld, item);
    return true;
}

bool Plan::try_construct_windmill(color_ostream &, room *r, std::ostream & reason)
{
    df::coord t = r->pos();
    df::tiletype *wtt = Maps::getTileType(t);
    if (!wtt)
    {
        reason << "tile not loaded";
        return false;
    }
    auto sb = ENUM_ATTR(tiletype_shape, basic_shape, ENUM_ATTR(tiletype, shape, *wtt));
    if (sb != tiletype_shape_basic::Open)
    {
        reason << "need channel (tile is currently " << enum_item_key(sb) << ")";
        return false;
    }

    std::vector<df::item *> mat;
    if (!find_items(items_other_id::WOOD, mat, 4))
    {
        reason << "have " << mat.size() << "/4 logs";
        return false;
    }

    df::building *bld = Buildings::allocInstance(t - df::coord(1, 1, 0), building_type::Windmill);
    Buildings::setSize(bld, df::coord(3, 3, 1));
    Buildings::constructWithItems(bld, mat);
    r->bld_id = bld->id;
    add_task(task_type::check_construct, r);
    return true;
}

bool Plan::try_furnish_roller(color_ostream &, room *r, furniture *f, df::coord t, std::ostream & reason)
{
    df::item *mecha = nullptr;
    df::item *chain = nullptr;
    if (find_item(items_other_id::TRAPPARTS, mecha) &&
        find_item(items_other_id::CHAIN, chain))
    {
        df::building *bld = Buildings::allocInstance(t, building_type::Rollers);
        Buildings::setSize(bld, df::coord(1, 1, 1));
        std::vector<df::item *> items;
        items.push_back(mecha);
        items.push_back(chain);
        Buildings::constructWithItems(bld, items);
        r->bld_id = bld->id;
        f->bld_id = bld->id;
        add_task(task_type::check_furnish, r, f);
        return true;
    }
    if (mecha)
    {
        reason << "need rope or chain";
    }
    else if (chain)
    {
        reason << "need mechanisms";
    }
    else
    {
        reason << "need mechanisms and rope or chain";
    }
    return false;
}

static void init_managed_workshop(color_ostream &, room *, df::building *bld)
{
    if (auto w = virtual_cast<df::building_workshopst>(bld))
    {
        w->profile.max_general_orders = 10;
    }
    else if (auto f = virtual_cast<df::building_furnacest>(bld))
    {
        f->profile.max_general_orders = 10;
    }
    else if (auto t = virtual_cast<df::building_trapst>(bld))
    {
        t->profile.max_general_orders = 10;
    }
}

bool Plan::try_construct_tradedepot(color_ostream & out, room *r, std::ostream & reason)
{
    ai.debug(out, "[try_construct_tradedepot] start: " + AI::describe_room(r));
    std::vector<df::item *> mats;
    bool found = find_items(items_other_id::BLOCKS, mats, 3);
    if (!found) { mats.clear(); found = find_items(items_other_id::BOULDER, mats, 3, false, true); }
    if (!found) { mats.clear(); found = find_items(items_other_id::WOOD, mats, 3); }
    if (found)
    {
        ai.debug(out, stl_sprintf("[try_construct_tradedepot] found %zu materials, allocating", mats.size()));
        df::building *bld = Buildings::allocInstance(r->min, building_type::TradeDepot);
        if (!bld)
        {
            ai.debug(out, "[try_construct_tradedepot] allocInstance returned null!");
            reason << "failed to allocate building";
            return false;
        }
        Buildings::setSize(bld, r->size());
        Buildings::constructWithItems(bld, mats);
        r->bld_id = bld->id;
        add_task(task_type::check_construct, r);
        return true;
    }
    reason << "could not find 3 building materials (blocks, boulders, or logs)";
    return false;
}

bool Plan::try_construct_workshop(color_ostream & out, room *r, std::ostream & reason)
{
    ai.debug(out, "[try_construct_workshop] start: " + AI::describe_room(r));
    if (!r->constructions_done(reason))
        return false;

    ai.debug(out, "[try_construct_workshop] constructions done, type=" + stl_sprintf("%d", (int)r->workshop_type));
    if (r->workshop_type == workshop_type::Dyers)
    {
        df::item *barrel = nullptr, *bucket = nullptr;
        if (find_item(items_other_id::BARREL, barrel) &&
            find_item(items_other_id::BUCKET, bucket))
        {
            df::building *bld = Buildings::allocInstance(r->min, building_type::Workshop, workshop_type::Dyers);
            Buildings::setSize(bld, r->size());
            std::vector<df::item *> items;
            items.push_back(barrel);
            items.push_back(bucket);
            Buildings::constructWithItems(bld, items);
            r->bld_id = bld->id;
            init_managed_workshop(out, r, bld);
            add_task(task_type::check_construct, r);
            return true;
        }
        reason << "could not find ";
        if (!barrel)
        {
            reason << "barrel";
        }
        if (!bucket)
        {
            if (!barrel)
            {
                reason << " or ";
            }
            reason << "bucket";
        }
    }
    else if (r->workshop_type == workshop_type::Ashery)
    {
        df::item *block = nullptr, *barrel = nullptr, *bucket = nullptr;
        if (find_item(items_other_id::BLOCKS, block) &&
            find_item(items_other_id::BARREL, barrel) &&
            find_item(items_other_id::BUCKET, bucket))
        {
            df::building *bld = Buildings::allocInstance(r->min, building_type::Workshop, workshop_type::Ashery);
            Buildings::setSize(bld, r->size());
            std::vector<df::item *> items;
            items.push_back(block);
            items.push_back(barrel);
            items.push_back(bucket);
            Buildings::constructWithItems(bld, items);
            r->bld_id = bld->id;
            init_managed_workshop(out, r, bld);
            add_task(task_type::check_construct, r);
            return true;
        }
        reason << "could not find ";
        if (!block)
        {
            reason << "blocks";
        }
        if (!barrel)
        {
            if (!block)
            {
                if (!bucket)
                {
                    reason << ", ";
                }
                else
                {
                    reason << " or ";
                }
            }
            reason << "barrel";
        }
        if (!bucket)
        {
            if (!block || !barrel)
            {
                if (!block && !barrel)
                {
                    reason << ", ";
                }
                reason << " or ";
            }
            reason << "bucket";
        }
    }
    else if (r->workshop_type == workshop_type::MetalsmithsForge)
    {
        df::item *anvil = nullptr, *bould = nullptr;
        if (find_item(items_other_id::ANVIL, anvil, true) &&
            find_item(items_other_id::BOULDER, bould, true, true))
        {
            df::building *bld = Buildings::allocInstance(r->min, building_type::Workshop, workshop_type::MetalsmithsForge);
            Buildings::setSize(bld, r->size());
            std::vector<df::item *> items;
            items.push_back(anvil);
            items.push_back(bould);
            Buildings::constructWithItems(bld, items);
            r->bld_id = bld->id;
            init_managed_workshop(out, r, bld);
            add_task(task_type::check_construct, r);
            return true;
        }
        reason << "could not find ";
        if (!anvil)
        {
            reason << "anvil";
        }
        if (!bould)
        {
            if (!anvil)
            {
                reason << " or ";
            }
            reason << "fire-safe boulder";
        }
    }
    else if (r->workshop_type == workshop_type::Quern)
    {
        df::item *quern = nullptr;
        if (find_item(items_other_id::QUERN, quern))
        {
            df::building *bld = Buildings::allocInstance(r->min, building_type::Workshop, workshop_type::Quern);
            Buildings::setSize(bld, r->size());
            std::vector<df::item *> item;
            item.push_back(quern);
            Buildings::constructWithItems(bld, item);
            r->bld_id = bld->id;
            init_managed_workshop(out, r, bld);
            add_task(task_type::check_construct, r);
            return true;
        }
        reason << "could not find quern";
    }
    else if (r->workshop_type == workshop_type::Custom)
    {
        auto cursor = std::find_if(world->raws.buildings.all.begin(), world->raws.buildings.all.end(), [r](df::building_def *def) -> bool { return def->code == r->raw_type; });
        df::building_def_workshopst *def = cursor == world->raws.buildings.all.end() ? nullptr : virtual_cast<df::building_def_workshopst>(*cursor);
        if (!def)
        {
            ai.debug(out, "Cannot find workshop type: " + r->raw_type);
            return true;
        }
        std::vector<df::job_item *> filters;
        for (auto it = def->build_items.begin(); it != def->build_items.end(); it++)
        {
            filters.push_back(make_job_item(*it));
        }
        std::vector<df::item *> items;
        if (!find_items(filters, items, reason))
        {
            for (auto it = filters.begin(); it != filters.end(); it++)
            {
                delete *it;
            }
            return false;
        }
        df::building *bld = Buildings::allocInstance(r->min, building_type::Workshop, workshop_type::Custom, def->id);
        Buildings::setSize(bld, r->size());
        Buildings::constructWithFilters(bld, filters);
        r->bld_id = bld->id;
        init_managed_workshop(out, r, bld);
        add_task(task_type::check_construct, r);
        return true;
    }
    else
    {
        df::item *bould;
        if (find_item(items_other_id::BLOCKS, bould) ||
            // use boulder if we can't find blocks
            find_item(items_other_id::BOULDER, bould, false, true) ||
            // use wood if we can't find stone
            find_item(items_other_id::WOOD, bould))
        {
            df::building *bld = Buildings::allocInstance(r->min, building_type::Workshop, r->workshop_type);
            Buildings::setSize(bld, r->size());
            std::vector<df::item *> item;
            item.push_back(bould);
            Buildings::constructWithItems(bld, item);
            r->bld_id = bld->id;
            init_managed_workshop(out, r, bld);
            add_task(task_type::check_construct, r);
            return true;
            // XXX else quarry?
        }
        reason << "could not find building material";
    }
    return false;
}

bool Plan::try_construct_furnace(color_ostream & out, room *r, std::ostream & reason)
{
    if (!r->constructions_done(reason))
        return false;

    if (r->furnace_type == furnace_type::Custom)
    {
        auto cursor = std::find_if(world->raws.buildings.all.begin(), world->raws.buildings.all.end(), [r](df::building_def *def) -> bool { return def->code == r->raw_type; });
        df::building_def_furnacest *def = cursor == world->raws.buildings.all.end() ? nullptr : virtual_cast<df::building_def_furnacest>(*cursor);
        if (!def)
        {
            ai.debug(out, "Cannot find furnace type: " + r->raw_type);
            return true;
        }
        std::vector<df::job_item *> filters;
        for (auto it = def->build_items.begin(); it != def->build_items.end(); it++)
        {
            filters.push_back(make_job_item(*it));
        }
        std::vector<df::item *> items;
        if (!find_items(filters, items, reason))
        {
            for (auto it = filters.begin(); it != filters.end(); it++)
            {
                delete *it;
            }
            return false;
        }
        df::building *bld = Buildings::allocInstance(r->min, building_type::Furnace, furnace_type::Custom, def->id);
        Buildings::setSize(bld, r->size());
        Buildings::constructWithFilters(bld, filters);
        r->bld_id = bld->id;
        init_managed_workshop(out, r, bld);
        add_task(task_type::check_construct, r);
        return true;
    }
    else
    {
        df::item *bould = nullptr;
        if (find_item(items_other_id::BOULDER, bould, true, true))
        {
            df::building *bld = Buildings::allocInstance(r->min, building_type::Furnace, r->furnace_type);
            Buildings::setSize(bld, r->size());
            std::vector<df::item *> item;
            item.push_back(bould);
            Buildings::constructWithItems(bld, item);
            r->bld_id = bld->id;
            init_managed_workshop(out, r, bld);
            add_task(task_type::check_construct, r);
            return true;
        }
        reason << "could not find fire-safe boulder";
        return false;
    }
}

const static struct stockpile_keys
{
    std::map<stockpile_type::type, df::stockpile_list> map;

    stockpile_keys()
    {
        map[stockpile_type::animals] = stockpile_list::Animals;
        map[stockpile_type::food] = stockpile_list::Food;
        map[stockpile_type::weapons] = stockpile_list::Weapons;
        map[stockpile_type::armor] = stockpile_list::Armor;
        map[stockpile_type::furniture] = stockpile_list::Furniture;
        map[stockpile_type::corpses] = stockpile_list::Corpses;
        map[stockpile_type::refuse] = stockpile_list::Refuse;
        map[stockpile_type::wood] = stockpile_list::Wood;
        map[stockpile_type::stone] = stockpile_list::Stone;
        map[stockpile_type::gems] = stockpile_list::Gems;
        map[stockpile_type::bars_blocks] = stockpile_list::BarsBlocks;
        map[stockpile_type::cloth] = stockpile_list::Cloth;
        map[stockpile_type::leather] = stockpile_list::Leather;
        map[stockpile_type::ammo] = stockpile_list::Ammo;
        map[stockpile_type::coins] = stockpile_list::Coins;
        map[stockpile_type::finished_goods] = stockpile_list::Goods;
        map[stockpile_type::sheets] = stockpile_list::Sheet;
        map[stockpile_type::fresh_raw_hide] = stockpile_list::Refuse;
    }
} stockpile_keys;

class ConstructStockpile : public ExclusiveCallback
{
    AI & ai;
    room *r;

public:
    ConstructStockpile(AI & ai, room *r) :
        ExclusiveCallback("construct stockpile for " + AI::describe_room(r)),
        ai(ai),
        r(r)
    {
    }

protected:
    void Run(color_ostream & out)
    {
      try
      {
        ai.debug(out, "[ConstructStockpile] start: " + AI::describe_room(r) +
            stl_sprintf(" min=(%d,%d,%d) size=(%d,%d,%d)", r->min.x, r->min.y, r->min.z, r->size().x, r->size().y, r->size().z));

        df::building *bld_raw = Buildings::allocInstance(r->min, building_type::Stockpile);
        if (!bld_raw)
        {
            ai.debug(out, "[ConstructStockpile] Failed to allocate: " + AI::describe_room(r));
            return;
        }
        ai.debug(out, "[ConstructStockpile] allocInstance OK");

        Buildings::setSize(bld_raw, r->size());
        ai.debug(out, "[ConstructStockpile] setSize OK");

        Buildings::constructAbstract(bld_raw);
        ai.debug(out, "[ConstructStockpile] constructAbstract OK");

        df::building_stockpilest *bld = virtual_cast<df::building_stockpilest>(bld_raw);
        if (!bld)
        {
            ai.debug(out, "[ConstructStockpile] Failed to cast: " + AI::describe_room(r));
            return;
        }
        ai.debug(out, "[ConstructStockpile] cast OK, setting flags");

        bld->settings.flags.whole = 0;
        switch (r->stockpile_type)
        {
        case stockpile_type::animals:        bld->settings.flags.bits.animals = true; break;
        case stockpile_type::food:           bld->settings.flags.bits.food = true; break;
        case stockpile_type::weapons:        bld->settings.flags.bits.weapons = true; break;
        case stockpile_type::armor:          bld->settings.flags.bits.armor = true; break;
        case stockpile_type::furniture:      bld->settings.flags.bits.furniture = true; break;
        case stockpile_type::corpses:        bld->settings.flags.bits.corpses = true; break;
        case stockpile_type::refuse:         bld->settings.flags.bits.refuse = true; break;
        case stockpile_type::wood:           bld->settings.flags.bits.wood = true; break;
        case stockpile_type::stone:          bld->settings.flags.bits.stone = true; break;
        case stockpile_type::gems:           bld->settings.flags.bits.gems = true; break;
        case stockpile_type::bars_blocks:    bld->settings.flags.bits.bars_blocks = true; break;
        case stockpile_type::cloth:          bld->settings.flags.bits.cloth = true; break;
        case stockpile_type::leather:        bld->settings.flags.bits.leather = true; break;
        case stockpile_type::ammo:           bld->settings.flags.bits.ammo = true; break;
        case stockpile_type::coins:          bld->settings.flags.bits.coins = true; break;
        case stockpile_type::finished_goods: bld->settings.flags.bits.finished_goods = true; break;
        case stockpile_type::sheets:         bld->settings.flags.bits.sheet = true; break;
        case stockpile_type::fresh_raw_hide:
            bld->settings.flags.bits.refuse = true;
            bld->settings.refuse.fresh_raw_hide = true;
            break;
        default: break;
        }
        ai.debug(out, "[ConstructStockpile] flags set, assigning bld_id");

        r->bld_id = bld->id;
        ai.plan.furnish_room(out, r);
        ai.debug(out, "[ConstructStockpile] furnish_room done, linking");

        if (r->workshop && r->stockpile_type == stockpile_type::stone)
        {
            ai.plan.room_items(out, r, [](df::item *i) { i->flags.bits.dump = 1; });
        }

        if (r->level == 0 &&
            r->stockpile_type != stockpile_type::stone && r->stockpile_type != stockpile_type::wood)
        {
            if (room *rr = ai.find_room(room_type::stockpile, [&](room *o) -> bool { return o->stockpile_type == r->stockpile_type && o->level == 1; }))
            {
                ai.plan.wantdig(out, rr);
            }
        }

        ai.find_room(room_type::stockpile, [&](room *o) -> bool
        {
            int32_t diff = o->level - r->level;
            if (o->workshop && r->workshop)
                return false;
            if (o->workshop)
                diff = -1;
            else if (r->workshop)
                diff = 1;
            if (o->stockpile_type == r->stockpile_type && diff != 0)
            {
                if (df::building_stockpilest *obld = virtual_cast<df::building_stockpilest>(o->dfbuilding()))
                {
                    df::building_stockpilest *b_from, *b_to;
                    if (diff > 0)
                    {
                        b_from = obld;
                        b_to = bld;
                    }
                    else
                    {
                        b_from = bld;
                        b_to = obld;
                    }
                    for (auto btf : b_to->links.take_from_pile)
                    {
                        if (btf->id == b_from->id)
                            return false;
                    }
                    b_to->links.take_from_pile.push_back(b_from);
                    b_from->links.give_to_pile.push_back(b_to);
                }
            }
            return false;
        });
        ai.debug(out, "[ConstructStockpile] complete: " + AI::describe_room(r));
      }
      catch (std::exception &e)
      {
        ai.debug(out, std::string("[ConstructStockpile] EXCEPTION: ") + e.what());
      }
    }
};

bool Plan::try_construct_stockpile(color_ostream &, room *r, std::ostream & reason)
{
    if (!r->constructions_done(reason))
    {
        return false;
    }

    events.queue_exclusive(std::make_unique<ConstructStockpile>(ai, r));

    return true;
}

class ConstructActivityZone : public ExclusiveCallback
{
    AI & ai;
    room *r;

public:
    ConstructActivityZone(AI & ai, room *r) :
        ExclusiveCallback("construct activity zone for " + AI::describe_room(r), 2),
        ai(ai),
        r(r)
    {
    }

protected:
    void Run(color_ostream & out)
    {
        try
        {
            ai.debug(out, "[ConstructActivityZone] start: " + AI::describe_room(r) + stl_sprintf(" type=%d", (int)r->type));

            if (r->dfbuilding())
            {
                ai.debug(out, "[ConstructActivityZone] already has building, skipping");
                return;
            }

            ai.debug(out, "[ConstructActivityZone] allocating civzone");
            auto bld = virtual_cast<df::building_civzonest>(
                Buildings::allocInstance(r->min, building_type::Civzone));
            if (!bld)
            {
                ai.debug(out, "Failed to allocate activity zone: " + AI::describe_room(r));
                return;
            }
            ai.debug(out, "[ConstructActivityZone] setSize");
            Buildings::setSize(bld, r->size());
            ai.debug(out, "[ConstructActivityZone] constructAbstract");
            Buildings::constructAbstract(bld);
            ai.debug(out, "[ConstructActivityZone] constructAbstract OK");

            r->bld_id = bld->id;
            bld->spec_sub_flag.bits.active = 1;

            if (r->type == room_type::infirmary)
            {
                bld->type = civzone_type::MeetingHall;

                auto site = plotinfo->main.fortress_site;
                if (!site)
                {
                    ai.debug(out, "[ConstructActivityZone] ERROR: no fortress site for infirmary zone");
                    return;
                }

                auto ab = (df::abstract_building *)df::abstract_building_hospitalst::_identity.instantiate();
                if (!ab)
                {
                    ai.debug(out, "[ConstructActivityZone] ERROR: failed to allocate abstract_building_hospitalst");
                    return;
                }

                ab->id = site->next_building_id++;
                ab->site_id = site->id;
                ab->site_owner_id = plotinfo->group_id;
                insert_into_vector(site->buildings, &df::abstract_building::id, ab);

                bld->site_id = site->id;
                bld->location_id = ab->id;

                auto contents = ab->getContents();
                if (contents)
                {
                    insert_into_vector(contents->building_ids, bld->id);
                }

                ai.debug(out, stl_sprintf("[ConstructActivityZone] created hospital location ab_id=%d for infirmary zone bld_id=%d",
                    ab->id, bld->id));
            }
            else if (r->type == room_type::garbagedump)
            {
                bld->type = civzone_type::Dump;
            }
            else if (r->type == room_type::pasture)
            {
                bld->type = civzone_type::Pen;
            }
            else if (r->type == room_type::pitcage)
            {
                bld->type = civzone_type::Pond;
                bld->zone_settings.pond.flag.bits.keep_filled = 0;
            }
            else if (r->type == room_type::pond)
            {
                bld->type = civzone_type::Pond;
                bld->zone_settings.pond.flag.bits.keep_filled = 1;
                if (r->temporary && r->workshop && r->workshop->type == room_type::farmplot)
                {
                    ai.plan.add_task(task_type::monitor_farm_irrigation, r);
                }
            }
            else if (r->type == room_type::location)
            {
                bld->type = civzone_type::MeetingHall;

                auto site = plotinfo->main.fortress_site;
                if (!site)
                {
                    ai.debug(out, "[ConstructActivityZone] ERROR: no fortress site for location zone");
                    return;
                }

                df::abstract_building *ab = nullptr;
                switch (r->location_type)
                {
                    case location_type::tavern:
                        ab = (df::abstract_building *)df::abstract_building_inn_tavernst::_identity.instantiate();
                        break;
                    case location_type::library:
                        ab = (df::abstract_building *)df::abstract_building_libraryst::_identity.instantiate();
                        break;
                    case location_type::temple:
                        ab = (df::abstract_building *)df::abstract_building_templest::_identity.instantiate();
                        break;
                    case location_type::guildhall:
                        ab = (df::abstract_building *)df::abstract_building_guildhallst::_identity.instantiate();
                        break;
                    case location_type::hospital:
                        ab = (df::abstract_building *)df::abstract_building_hospitalst::_identity.instantiate();
                        break;
                    case location_type::museum:
                        // Museums use a MeetingHall zone with pedestals for artifact display.
                        // No dedicated abstract_building type confirmed in DFHack 53.14-r2 yet.
                        // The zone still works as a display area without one.
                        break;
                    default:
                        ai.debug(out, "[ConstructActivityZone] ERROR: unknown location_type");
                        return;
                }

                if (ab)
                {
                    ab->id = site->next_building_id++;
                    ab->site_id = site->id;
                    ab->site_owner_id = plotinfo->group_id;
                    insert_into_vector(site->buildings, &df::abstract_building::id, ab);

                    bld->site_id = site->id;
                    bld->location_id = ab->id;

                    auto contents = ab->getContents();
                    if (contents)
                    {
                        insert_into_vector(contents->building_ids, bld->id);
                    }

                    ai.debug(out, stl_sprintf("[ConstructActivityZone] created location ab_id=%d for zone bld_id=%d type=%d",
                        ab->id, bld->id, (int)r->location_type));
                }
                else if (r->location_type == location_type::museum)
                {
                    ai.debug(out, stl_sprintf("[ConstructActivityZone] created museum zone bld_id=%d (no abstract_building)", bld->id));
                }
                else
                {
                    ai.debug(out, "[ConstructActivityZone] ERROR: failed to allocate abstract_building");
                    return;
                }
            }
            ai.debug(out, "[ConstructActivityZone] complete: " + AI::describe_room(r));
        }
        catch (std::exception &e)
        {
            ai.debug(out, std::string("[ConstructActivityZone] EXCEPTION: ") + e.what());
        }
    }
};

bool Plan::try_construct_activityzone(color_ostream &, room *r, std::ostream & reason)
{
    if (!r->constructions_done(reason))
    {
        return false;
    }

    if (r->type == room_type::pond && r->workshop && !r->workshop->is_dug())
    {
        reason << "waiting for pond target to be dug";
        return false;
    }

    events.queue_exclusive(std::make_unique<ConstructActivityZone>(ai, r));

    return true;
}

bool Plan::monitor_farm_irrigation(color_ostream & out, room *r, std::ostream & reason)
{
    if (can_place_farm(out, r->workshop, false, reason))
    {
        auto zone = virtual_cast<df::building_civzonest>(r->dfbuilding());
        zone->zone_settings.pond.flag.bits.keep_filled = 0;
        return true;
    }

    if (auto pond = virtual_cast<df::building_civzonest>(r->dfbuilding()))
    {
        for (auto j : pond->jobs)
        {
            j->flags.bits.do_now = 1;
        }
    }

    return false;
}

bool Plan::can_place_farm(color_ostream & out, room *r, bool cheat, std::ostream & reason)
{
    size_t need = (r->max.x - r->min.x + 1) * (r->max.y - r->min.y + 1) * (r->max.z - r->min.z + 1);
    size_t have = 0;
    for (int16_t x = r->min.x; x <= r->max.x; x++)
    {
        for (int16_t y = r->min.y; y <= r->max.y; y++)
        {
            for (int16_t z = r->min.z; z <= r->max.z; z++)
            {
                df::tiletype *ftt = Maps::getTileType(x, y, z);
                if (!ftt)
                    continue;
                if (farm_allowed_materials.set.count(ENUM_ATTR(tiletype, material, *ftt)))
                {
                    have++;
                    continue;
                }

                df::map_block *block = Maps::getTileBlock(x, y, z);
                if (!block)
                    continue;
                auto e = std::find_if(block->block_events.begin(), block->block_events.end(), [](df::block_square_event *e) -> bool
                {
                    df::block_square_event_material_spatterst *spatter = virtual_cast<df::block_square_event_material_spatterst>(e);
                    return spatter &&
                        spatter->mat_type == builtin_mats::MUD &&
                        spatter->mat_index == -1;
                });
                if (cheat)
                {
                    if (e == block->block_events.end())
                    {
                        df::block_square_event_material_spatterst *spatter = df::allocate<df::block_square_event_material_spatterst>();
                        spatter->mat_type = builtin_mats::MUD;
                        spatter->mat_index = -1;
                        spatter->min_temperature = 60001;
                        spatter->max_temperature = 60001;
                        e = block->block_events.insert(e, spatter);
                    }
                    df::block_square_event_material_spatterst *spatter = virtual_cast<df::block_square_event_material_spatterst>(*e);
                    if (spatter->amount[x & 0xf][y & 0xf] == 0)
                    {
                        ai.debug(out, stl_sprintf("cheat: mud invocation (%d, %d, %d)", x, y, z));
                        spatter->amount[x & 0xf][y & 0xf] = 50; // small pile of mud
                    }
                    have++;
                }
                else
                {
                    if (e == block->block_events.end())
                    {
                        continue;
                    }

                    df::block_square_event_material_spatterst *spatter = virtual_cast<df::block_square_event_material_spatterst>(*e);
                    if (spatter->amount[x & 0xf][y & 0xf] == 0)
                    {
                        continue;
                    }
                    have++;
                }
            }
        }
    }

    if (have == need)
    {
        return true;
    }

    reason << "waiting for irrigation (" << have << "/" << need << ")";
    return false;
}

bool Plan::try_construct_farmplot(color_ostream & out, room *r, std::ostream & reason)
{
    ai.debug(out, "[try_construct_farmplot] start: " + AI::describe_room(r));
    auto pond = ai.find_room(room_type::pond, [r](room *p) -> bool
    {
        return p->temporary && p->workshop == r;
    });
    if (!can_place_farm(out, r, !pond, reason))
    {
        return false;
    }

    ai.debug(out, "[try_construct_farmplot] can_place_farm OK, allocating");
    df::building *bld = Buildings::allocInstance(r->min, building_type::FarmPlot);
    if (!bld)
    {
        ai.debug(out, "[try_construct_farmplot] allocInstance returned null!");
        reason << "failed to allocate building";
        return false;
    }
    ai.debug(out, "[try_construct_farmplot] allocInstance OK, setSize");
    Buildings::setSize(bld, r->size());
    ai.debug(out, "[try_construct_farmplot] setSize OK, constructWithItems (empty)");
    Buildings::constructWithItems(bld, std::vector<df::item *>());
    ai.debug(out, "[try_construct_farmplot] constructWithItems OK");
    r->bld_id = bld->id;
    furnish_room(out, r);
    if (room *st = ai.find_room(room_type::stockpile, [r](room *o) -> bool { return o->workshop == r; }))
    {
        digroom(out, st);
    }
    add_task(task_type::setup_farmplot, r);
    return true;
}

bool Plan::try_setup_farmplot(color_ostream & out, room *r, std::ostream & reason)
{
    df::building *bld = r->dfbuilding();
    if (!bld)
        return true;
    if (bld->getBuildStage() < bld->getMaxBuildStage())
    {
        reason << "waiting for field to be tilled (" << bld->getBuildStage() << "/" << bld->getMaxBuildStage() << ")";
        return false;
    }

    ai.stocks.farmplot(out, r);

    return true;
}

bool Plan::try_endfurnish(color_ostream & out, room *r, furniture *f, std::ostream & reason)
{
    if (!AI::is_dwarfmode_viewscreen())
    {
        // some of these things need to use the UI.
        reason << "not on main viewscreen";
        return false;
    }

    df::building *bld = df::building::find(f->bld_id);
    if (!bld)
    {
        // destroyed building?
        return true;
    }
    if (bld->getBuildStage() < bld->getMaxBuildStage())
    {
        reason << "waiting for construction (" << bld->getBuildStage() << "/" << bld->getMaxBuildStage() << ")";
        return false;
    }

    if (f->type == layout_type::archery_target)
    {
        f->makeroom = true;
    }
    else if (f->type == layout_type::coffin)
    {
        // burial_mode removed in Steam DF
    }
    else if (f->type == layout_type::door)
    {
        // pet_passable and internal flags removed in Steam DF
    }
    else if (f->type == layout_type::floodgate)
    {
        for (auto rr : rooms_and_corridors)
        {
            if (rr->status == room_status::plan)
                continue;
            for (auto ff : rr->layout)
            {
                if (ff->type == layout_type::lever && ff->target == f)
                {
                    link_lever(out, ff, f, reason);
                }
            }
        }
    }
    else if (f->type == layout_type::hatch)
    {
        df::building_hatchst *hatch = virtual_cast<df::building_hatchst>(bld);
        if (r->type == room_type::pitcage)
        {
            hatch->door_flags.bits.forbidden = 1;
        }
        // pet_passable and internal flags removed in Steam DF
    }
    else if (f->type == layout_type::lever)
    {
        return setup_lever(out, r, f, reason);
    }

    if (r->type == room_type::jail && (f->type == layout_type::cage || f->type == layout_type::restraint))
    {
        if (!f->makeroom)
        {
            delete[] bld->room.extents;
            bld->room.extents = new df::building_extents_type[1];
            bld->room.extents[0] = building_extents_type::DistanceBoundary;
            bld->room.x = r->min.x + f->pos.x;
            bld->room.y = r->min.y + f->pos.y;
            bld->room.width = 1;
            bld->room.height = 1;
        }
        // is_room and justice flags removed in Steam DF
    }

    if (r->type == room_type::infirmary)
    {
    }

    if (!f->makeroom)
    {
        return true;
    }
    if (!r->is_dug(reason))
    {
        reason << " (waiting to make room)";
        return false;
    }

    ai.debug(out, "makeroom " + AI::describe_room(r));

    df::coord size = r->size() + df::coord(2, 2, 0);

    delete[] bld->room.extents;
    bld->room.extents = new df::building_extents_type[size.x * size.y]();
    bld->room.x = r->min.x - 1;
    bld->room.y = r->min.y - 1;
    bld->room.width = size.x;
    bld->room.height = size.y;
    auto set_ext = [&bld](int16_t x, int16_t y, df::building_extents_type v)
    {
        bld->room.extents[bld->room.width * (y - bld->room.y) + (x - bld->room.x)] = v;
    };
    for (int16_t rx = r->min.x - 1; rx <= r->max.x + 1; rx++)
    {
        for (int16_t ry = r->min.y - 1; ry <= r->max.y + 1; ry++)
        {
            df::tiletype *ext_tt = Maps::getTileType(rx, ry, r->min.z);
            if (!ext_tt || ENUM_ATTR(tiletype, shape, *ext_tt) == tiletype_shape::WALL)
            {
                set_ext(rx, ry, building_extents_type::Wall);
            }
            else
            {
                set_ext(rx, ry, r->include(df::coord(rx, ry, r->min.z)) ? building_extents_type::Interior : building_extents_type::DistanceBoundary);
            }
        }
    }
    for (auto f_ : r->layout)
    {
        if (f_->type != layout_type::door)
            continue;
        df::coord t = r->min + f_->pos;
        set_ext(t.x, t.y, building_extents_type::None);
        // tile in front of the door tile is 4 (TODO door in corner...)
        if (t.x < r->min.x)
            set_ext(t.x + 1, t.y, building_extents_type::DistanceBoundary);
        if (t.x > r->max.x)
            set_ext(t.x - 1, t.y, building_extents_type::DistanceBoundary);
        if (t.y < r->min.y)
            set_ext(t.x, t.y + 1, building_extents_type::DistanceBoundary);
        if (t.y > r->max.y)
            set_ext(t.x, t.y - 1, building_extents_type::DistanceBoundary);
    }
    // bld->is_room removed in Steam DF

    set_owner(out, r, r->owner);
    furnish_room(out, r);

    if (r->type == room_type::dininghall)
    {
        virtual_cast<df::building_tablest>(bld)->table_flags.bits.meeting_hall = 1;
    }
    else if (r->type == room_type::barracks)
    {
        df::building *bld = r->dfbuilding();
        if (f->type == layout_type::archery_target)
        {
            bld = df::building::find(f->bld_id);
        }
        if (r->squad_id != -1 && bld)
        {
            assign_barrack_squad(out, bld, r->squad_id);
        }
    }

    return true;
}

bool Plan::try_endconstruct(color_ostream & out, room *r, std::ostream & reason)
{
    df::building *bld = r->dfbuilding();
    if (bld && bld->getBuildStage() < bld->getMaxBuildStage())
    {
        reason << "waiting for construction (" << bld->getBuildStage() << "/" << bld->getMaxBuildStage() << ")";
        return false;
    }
    furnish_room(out, r);
    return true;
}

bool Plan::link_lever(color_ostream &, furniture *src, furniture *dst, std::ostream & reason)
{
    auto bld = virtual_cast<df::building_trapst>(df::building::find(src->bld_id));
    if (!bld || bld->getBuildStage() != bld->getMaxBuildStage())
    {
        reason << "lever not constructed";
        return false;
    }

    auto tbld = df::building::find(dst->bld_id);
    if (!tbld || tbld->getBuildStage() != tbld->getMaxBuildStage())
    {
        reason << "lever target not constructed";
        return false;
    }

    for (auto item : bld->contained_items)
    {
        for (auto ref : item->item->general_refs)
        {
            if (ref->getType() == general_ref_type::BUILDING_TRIGGERTARGET && ref->getBuilding() == tbld)
            {
                return true;
            }
        }
    }

    for (auto job : bld->jobs)
    {
        if (job->job_type != job_type::LinkBuildingToTrigger)
        {
            continue;
        }

        for (auto ref : job->general_refs)
        {
            if (ref->getType() == general_ref_type::BUILDING_TRIGGERTARGET && ref->getBuilding() == tbld)
            {
                reason << "waiting for lever to be linked to target";
                return false;
            }
        }
    }

    if (bld->jobs.size() >= 10)
    {
        reason << "lever job list is full";
        return false;
    }

    std::vector<df::item *> mechas;
    if (!find_items(items_other_id::TRAPPARTS, mechas, 2))
    {
        reason << "need 2 mechanisms, but have " << mechas.size();
        return false;
    }

    auto reflink = df::allocate<df::general_ref_building_triggertargetst>();
    reflink->building_id = tbld->id;
    auto refhold = df::allocate<df::general_ref_building_holderst>();
    refhold->building_id = bld->id;

    auto job = df::allocate<df::job>();
    job->job_type = job_type::LinkBuildingToTrigger;
    job->general_refs.push_back(reflink);
    job->general_refs.push_back(refhold);
    bld->jobs.push_back(job);
    Job::linkIntoWorld(job);

    // job_item_ref roles restructured in Steam DF
    // TODO: determine new API for attaching mechanism items to lever link jobs
    (void)mechas;

    reason << "waiting for lever to be linked to target";
    return false;
}
