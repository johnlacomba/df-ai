#include "ai.h"
#include "camera.h"
#include "population.h"
#include "stocks.h"
#include "trade.h"

#include "modules/Buildings.h"
#include "modules/Items.h"
#include "modules/Job.h"

#include "df/building.h"
#include "df/caravan_state.h"
#include "df/general_ref_building_holderst.h"
#include "df/general_ref_contains_itemst.h"
#include "df/general_ref_unit_workerst.h"
#include "df/historical_entity.h"
#include "df/item.h"
#include "df/job.h"
#include "df/plotinfost.h"
#include "df/unit.h"
#include "df/world.h"

REQUIRE_GLOBAL(plotinfo);
REQUIRE_GLOBAL(world);

void Population::update_trading(color_ostream & out)
{
    if (!ai.trade.can_move_goods())
    {
        if (trade_state != TRADE_IDLE)
        {
            ai.debug(out, "trade: caravan gone, resetting to idle");
            trade_state = TRADE_IDLE;
            trade_designated_items.clear();
        }
        did_trade = false;
        return;
    }

    if (did_trade)
        return;

    switch (trade_state)
    {
    case TRADE_IDLE:
    {
        auto depot_room = ai.find_room(room_type::tradedepot);
        if (!depot_room || !depot_room->dfbuilding())
            return;

        ai.debug(out, "trade: caravan detected, moving goods to depot");
        ai.camera.queue_event(CAMERA_TIER_FORT, depot_room->pos(), "caravan arriving at depot");
        trade_state = TRADE_MOVING_GOODS;
        trade_designated_items.clear();
        break;
    }
    case TRADE_MOVING_GOODS:
    {
        auto depot_room = ai.find_room(room_type::tradedepot);
        auto *depot = depot_room ? depot_room->dfbuilding() : nullptr;
        if (!depot)
        {
            trade_state = TRADE_IDLE;
            return;
        }

        bool any_new = false;
        for (auto item : world->items.other[items_other_id::IN_PLAY])
        {
            if (!item || item->flags.bits.forbid || item->flags.bits.dump ||
                item->flags.bits.trader || item->flags.bits.in_job ||
                item->flags.bits.removed || item->flags.bits.in_building)
                continue;

            if (trade_designated_items.count(item->id))
                continue;

            if (!ai.stocks.willing_to_trade_item(out, item))
                continue;

            if (!ai.stocks.is_item_free(item))
                continue;

            auto bld_ref = df::allocate<df::general_ref_building_holderst>();
            if (!bld_ref)
                continue;
            bld_ref->building_id = depot->id;

            auto item_ref = df::allocate<df::general_ref_contains_itemst>();
            if (!item_ref)
            {
                delete bld_ref;
                continue;
            }
            item_ref->item_id = item->id;

            auto job = df::allocate<df::job>();
            if (!job)
            {
                delete bld_ref;
                delete item_ref;
                continue;
            }
            job->job_type = job_type::BringItemToDepot;
            job->pos = df::coord(depot->x1, depot->y1, depot->z);
            job->general_refs.push_back(bld_ref);
            job->general_refs.push_back(item_ref);
            depot->jobs.push_back(job);
            Job::linkIntoWorld(job);

            item->flags.bits.in_job = true;
            trade_designated_items.insert(item->id);
            any_new = true;
        }

        if (any_new)
        {
            ai.debug(out, stl_sprintf("trade: designated %zu items for depot", trade_designated_items.size()));
        }

        // check if any designated items have arrived at the depot
        bool items_at_depot = false;
        for (auto item : world->items.other[items_other_id::IN_PLAY])
        {
            if (!item || !trade_designated_items.count(item->id))
                continue;
            if (item->pos.x >= depot->x1 && item->pos.x <= depot->x2 &&
                item->pos.y >= depot->y1 && item->pos.y <= depot->y2 &&
                item->pos.z == depot->z)
            {
                items_at_depot = true;
                break;
            }
        }

        if (items_at_depot || trade_designated_items.empty())
        {
            ai.debug(out, "trade: items arriving at depot, requesting broker");
            trade_state = TRADE_AWAITING_BROKER;
            set_up_trading(out, true);
        }
        break;
    }
    case TRADE_AWAITING_BROKER:
    {
        if (ai.trade.can_trade())
        {
            ai.debug(out, "trade: broker at depot, starting trade");
            trade_state = TRADE_TRADING;
            perform_trade(out);
        }
        else
        {
            set_up_trading(out, true);
        }
        break;
    }
    case TRADE_TRADING:
    {
        if (!ai.trade.can_trade())
        {
            ai.debug(out, "trade: trade complete or broker left");
            trade_state = TRADE_IDLE;
            trade_designated_items.clear();
            did_trade = true;
        }
        break;
    }
    }
}

bool Population::set_up_trading(color_ostream & out, bool should_be_trading, bool)
{
    if (!should_be_trading)
        return false;

    auto depot_room = ai.find_room(room_type::tradedepot);
    auto *depot = depot_room ? depot_room->dfbuilding() : nullptr;
    if (!depot)
        return false;

    for (auto & job : depot->jobs)
    {
        if (job->job_type == job_type::TradeAtDepot)
            return true;
    }

    auto ref = df::allocate<df::general_ref_building_holderst>();
    if (!ref)
        return false;
    ref->building_id = depot->id;

    auto job = df::allocate<df::job>();
    if (!job)
    {
        delete ref;
        return false;
    }
    job->job_type = job_type::TradeAtDepot;
    job->pos = df::coord(depot->x1, depot->y1, depot->z);
    job->general_refs.push_back(ref);
    depot->jobs.push_back(job);
    Job::linkIntoWorld(job);

    ai.debug(out, "trade: requested broker at depot");
    return true;
}

bool Population::perform_trade(color_ostream & out)
{
    auto depot_room = ai.find_room(room_type::tradedepot);
    auto *depot = depot_room ? depot_room->dfbuilding() : nullptr;
    if (!depot)
        return false;

    df::caravan_state *active_caravan = nullptr;
    for (auto & caravan : plotinfo->caravans)
    {
        if (caravan && caravan->trade_state == df::caravan_state::AtDepot && caravan->time_remaining > 0)
        {
            active_caravan = caravan;
            break;
        }
    }
    if (!active_caravan)
        return false;

    auto entity = df::historical_entity::find(active_caravan->entity);
    if (!entity)
        return false;

    std::vector<df::item *> depot_items;
    std::vector<df::item *> caravan_items;

    for (auto item : world->items.other[items_other_id::IN_PLAY])
    {
        if (!item)
            continue;

        if (item->flags.bits.trader)
        {
            if (ai.stocks.want_trader_item(out, item, caravan_items))
                caravan_items.push_back(item);
        }
        else if (item->pos.x >= depot->x1 && item->pos.x <= depot->x2 &&
                 item->pos.y >= depot->y1 && item->pos.y <= depot->y2 &&
                 item->pos.z == depot->z)
        {
            if (ai.stocks.willing_to_trade_item(out, item))
                depot_items.push_back(item);
        }
    }

    if (depot_items.empty())
    {
        ai.debug(out, "trade: no items at depot to sell");
        return false;
    }

    std::sort(caravan_items.begin(), caravan_items.end(), [this](df::item *a, df::item *b) -> bool
    {
        return ai.stocks.want_trader_item_more(a, b);
    });

    int32_t sell_value = 0;
    for (auto item : depot_items)
    {
        sell_value += ai.trade.item_or_container_price_for_caravan(item, active_caravan, entity, nullptr, item->getStackSize(), nullptr, nullptr);
    }

    int32_t buy_value = 0;
    std::vector<df::item *> buy_list;
    for (auto item : caravan_items)
    {
        int32_t v = ai.trade.item_or_container_price_for_caravan(item, active_caravan, entity, nullptr, item->getStackSize(), nullptr, nullptr);
        if (buy_value + v <= sell_value)
        {
            buy_list.push_back(item);
            buy_value += v;
        }
    }

    ai.debug(out, stl_sprintf("trade: selling %zu items (value %d), buying %zu caravan items (value %d)",
        depot_items.size(), sell_value, buy_list.size(), buy_value));

    // Transfer fortress items to caravan
    for (auto item : depot_items)
    {
        item->flags.bits.trader = true;
        for (auto & ref : item->general_refs)
        {
            if (auto contains = virtual_cast<df::general_ref_contains_itemst>(ref))
            {
                auto contained = df::item::find(contains->item_id);
                if (contained)
                    contained->flags.bits.trader = true;
            }
        }
    }

    // Transfer merchant items to fortress
    for (auto item : buy_list)
    {
        item->flags.bits.trader = false;
        for (auto & ref : item->general_refs)
        {
            if (auto contains = virtual_cast<df::general_ref_contains_itemst>(ref))
            {
                auto contained = df::item::find(contains->item_id);
                if (contained)
                    contained->flags.bits.trader = false;
            }
        }
    }

    active_caravan->export_value_total += sell_value;

    // Cancel the TradeAtDepot job so the broker leaves
    df::job *trade_job = nullptr;
    for (auto & j : depot->jobs)
    {
        if (j->job_type == job_type::TradeAtDepot)
        {
            trade_job = j;
            break;
        }
    }
    if (trade_job)
        Job::removeJob(trade_job);

    ai.debug(out, stl_sprintf("trade: complete! sold %zu items (value %d), bought %zu items (value %d)",
        depot_items.size(), sell_value, buy_list.size(), buy_value));

    return true;
}
