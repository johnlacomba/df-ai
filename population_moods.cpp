#include "ai.h"
#include "population.h"
#include "stocks.h"

#include "modules/Units.h"

#include "df/item.h"
#include "df/item_type.h"
#include "df/unit.h"
#include "df/world.h"

REQUIRE_GLOBAL(world);

static bool is_mood_failure(df::mood_type mood)
{
    return mood == mood_type::Berserk ||
           mood == mood_type::Melancholy ||
           mood == mood_type::Catatonic;
}

void Population::update_moods(color_ostream & out)
{
    for (auto it = moody.begin(); it != moody.end(); )
    {
        auto u = df::unit::find(it->first);
        if (!u || !Units::isAlive(u))
        {
            it = moody.erase(it);
            continue;
        }

        if (u->mood == mood_type::None)
        {
            ai.debug(out, "[moods] " + AI::describe_unit(u) + " completed mood successfully (artifact created)");
            it = moody.erase(it);
            continue;
        }

        if (is_mood_failure(u->mood) && it->second != u->mood)
        {
            ai.debug(out, "[moods] " + AI::describe_unit(u) + " mood failed: " + enum_item_key(u->mood));
            it->second = u->mood;
        }

        it++;
    }

    for (auto u : world->units.active)
    {
        if (!Units::isCitizen(u))
            continue;
        if (u->mood == mood_type::None)
            continue;
        if (is_mood_failure(u->mood))
            continue;
        if (moody.count(u->id))
            continue;

        moody[u->id] = u->mood;
        ai.debug(out, "[moods] " + AI::describe_unit(u) + " entered " + enum_item_key(u->mood) + " mood");

        if (u->job.workshop_id != -1)
        {
            ai.debug(out, "[moods]   claimed workshop id=" + std::to_string(u->job.workshop_id));
        }

        unforbid_mood_materials(out);
    }
}

void Population::update_artifacts(color_ostream & out)
{
    for (auto item : world->items.other.IN_PLAY)
    {
        if (!item)
            continue;
        if (!item->flags.bits.artifact)
            continue;
        if (item->flags.bits.foreign)
            continue;
        if (artifacts.count(item->id))
            continue;

        artifacts.insert(item->id);
        ai.debug(out, "[moods] discovered artifact: " + AI::describe_item(item));
    }

    for (auto it = artifacts.begin(); it != artifacts.end(); )
    {
        auto item = df::item::find(*it);
        if (!item || !item->flags.bits.artifact || item->flags.bits.foreign)
        {
            it = artifacts.erase(it);
            continue;
        }
        it++;
    }

    int32_t pedestal_need = std::max(3, (int32_t)artifacts.size() + 1);
    Watch.Needed[stock_item::pedestal] = pedestal_need;
}

void Population::unforbid_mood_materials(color_ostream & out)
{
    int32_t count = 0;
    for (auto item : world->items.other.IN_PLAY)
    {
        if (!item || !item->flags.bits.forbid)
            continue;
        if (item->flags.bits.dump || item->flags.bits.trader || item->flags.bits.in_job)
            continue;

        switch (item->getType())
        {
        case item_type::WOOD:
        case item_type::BOULDER:
        case item_type::BAR:
        case item_type::BLOCKS:
        case item_type::ROUGH:
        case item_type::SKIN_TANNED:
        case item_type::CLOTH:
        case item_type::THREAD:
        case item_type::BONE:
        case item_type::SHELL:
        case item_type::SKULL:
        case item_type::HORN:
            item->flags.bits.forbid = 0;
            count++;
            break;
        default:
            break;
        }
    }

    if (count > 0)
    {
        ai.debug(out, "[moods] unforbade " + std::to_string(count) + " items for mood materials");
    }
}
