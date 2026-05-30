#include "ai.h"
#include "camera.h"
#include "embark.h"
#include "plan.h"

#include "modules/Gui.h"
#include "modules/Screen.h"

#include "df/d_init.h"
#include "df/gamest.h"
#include "df/popup_message.h"
#include "df/report.h"
#include "df/world.h"

REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(cur_year_tick);
REQUIRE_GLOBAL(d_init);
REQUIRE_GLOBAL(game);
REQUIRE_GLOBAL(pause_state);
REQUIRE_GLOBAL(world);

void AI::unpause()
{
    if (!world->status.popups.empty())
    {
        for (auto popup : world->status.popups)
        {
            delete popup;
        }
        world->status.popups.clear();
    }

    if (game && game->main_interface.announcement_alert.open)
    {
        game->main_interface.announcement_alert.open = false;
        game->main_interface.announcement_alert.viewing_alert = nullptr;
    }

    if (game && game->main_interface.diplomacy.open)
    {
        game->main_interface.diplomacy.open = false;
    }

    if (game && game->main_interface.petitions.open)
    {
        game->main_interface.petitions.open = false;
    }

    if (*pause_state)
    {
        Gui::getCurViewscreen(true)->feed_key(interface_key::D_PAUSE);
    }
    ignore_pause(last_good_x, last_good_y, last_good_z);
}

void AI::handle_pause_event(color_ostream & out, df::report *announce)
{
    std::string fulltext = announce->text;
    auto idx = std::find(world->status.announcements.rbegin(), world->status.announcements.rend(), announce);
    while (announce->flags.bits.continuation)
    {
        idx++;
        if (idx == world->status.announcements.rend())
            break;
        announce = *idx;
        fulltext = announce->text + " " + fulltext;
    }
    if (announce->repeat_count)
    {
        fulltext += stl_sprintf(" x%d", announce->repeat_count + 1);
    }
    debug(out, "pause: " + fulltext);

    switch (announce->type)
    {
    case announcement_type::MEGABEAST_ARRIVAL:
    {
        if (!tag_enemies(out))
        {
            debug(out, "[ERROR] could not find megabeast");
        }
        break;
    }
    case announcement_type::UNDEAD_ATTACK:
        if (!tag_enemies(out))
        {
            debug(out, "[ERROR] could not find enemy combatants");
        }
        break;
    case announcement_type::BERSERK_CITIZEN:
    case announcement_type::CAVE_COLLAPSE:
        break;
    case announcement_type::DIG_CANCEL_DAMP:
    case announcement_type::DIG_CANCEL_WARM:
        ignore_pause(last_good_x, last_good_y, last_good_z);
        break;
    case announcement_type::BIRTH_CITIZEN:
    case announcement_type::BIRTH_ANIMAL:
        break;
    case announcement_type::D_MIGRANTS_ARRIVAL:
    case announcement_type::D_MIGRANT_ARRIVAL:
    case announcement_type::MIGRANT_ARRIVAL:
    case announcement_type::NOBLE_ARRIVAL:
    case announcement_type::FORT_POSITION_SUCCESSION:
        plan.make_map_walkable(out);
        break;
    case announcement_type::DIPLOMAT_ARRIVAL:
    case announcement_type::LIAISON_ARRIVAL:
    case announcement_type::CARAVAN_ARRIVAL:
    case announcement_type::FIRST_CARAVAN_ARRIVAL:
    case announcement_type::TRADE_DIPLOMAT_ARRIVAL:
    case announcement_type::DIPLOMAT_LEFT_UNHAPPY:
    case announcement_type::MONARCH_ARRIVAL:
    case announcement_type::HASTY_MONARCH:
    case announcement_type::SATISFIED_MONARCH:
    case announcement_type::MOUNTAINHOME:
    case announcement_type::FOOD_WARNING:
    case announcement_type::STRANGE_MOOD:
    case announcement_type::MOOD_BUILDING_CLAIMED:
    case announcement_type::ARTIFACT_BEGUN:
    case announcement_type::MADE_ARTIFACT:
    case announcement_type::FEATURE_DISCOVERY:
    case announcement_type::STRUCK_DEEP_METAL:
    case announcement_type::TRAINING_FULL_REVERSION:
    case announcement_type::NAMED_ARTIFACT:
    case announcement_type::DEITY_PRONOUNCEMENT:
    case announcement_type::EMBARK_MESSAGE:
        break;
    default:
    {
        const static std::string prefix("AMBUSH");
        std::string type(ENUM_KEY_STR(announcement_type, announce->type));
        if (std::mismatch(prefix.begin(), prefix.end(), type.begin()).first == prefix.end())
        {
            debug(out, "pause: an ambush!");
            if (!tag_enemies(out))
            {
                debug(out, "[ERROR] could not find enemy combatants");
            }
        }
        else
        {
            debug(out, "pause: unhandled pausing event " + type);
        }
        break;
    }
    }

    if (d_init->announcements.flags[announce->type].bits.DO_MEGA)
    {
        timeout_sameview([this](color_ostream &) { unpause(); });
    }
    else
    {
        unpause();
    }
}

void AI::statechanged(color_ostream & out, state_change_event st)
{
    if (st == SC_PAUSED)
    {
        auto la = std::find_if(world->status.announcements.rbegin(), world->status.announcements.rend(), [](df::report *a) -> bool
        {
            return d_init->announcements.flags[a->type].bits.PAUSE;
        });

        if (la != world->status.announcements.rend())
        {
            if ((*la)->year == *cur_year && (*la)->time == *cur_year_tick)
            {
                last_pause_id = (*la)->id;
                last_pause_repeats = (*la)->repeat_count;
                handle_pause_event(out, *la);
                return;
            }
            if ((*la)->id == last_pause_id && (*la)->repeat_count != last_pause_repeats)
            {
                last_pause_repeats = (*la)->repeat_count;
                handle_pause_event(out, *la);
                return;
            }
        }

        debug(out, "pause without an event");
        unpause();
    }
    else if (st == SC_VIEWSCREEN_CHANGED)
    {
        df::viewscreen *curview = Gui::getCurViewscreen(true);
        if (auto hack = dfhack_viewscreen::try_cast(curview))
        {
            std::string focus = hack->getFocusString();
            if (focus == "lua/status_overlay")
            {
                debug(out, "dismissing gui/extended-status overlay");
                Screen::dismiss(hack);
            }
            else if (focus == "lua/warn-starving")
            {
                debug(out, "exit warn-starving dialog");
                timeout_sameview([](color_ostream &)
                {
                    Gui::getCurViewscreen(true)->feed_key(interface_key::LEAVESCREEN);
                });
            }
            else if (seen_focus.insert(focus).second)
            {
                debug(out, "ignoring DFHack overlay (handled by embark loop): " + focus);
            }
        }
        else if (const virtual_identity *ident = virtual_identity::get(curview))
        {
            std::string cvname = ident->getName();
            if (seen_cvname.insert(cvname).second)
            {
                debug(out, "[ERROR] paused in unknown viewscreen " + cvname);
            }
        }
    }
}
