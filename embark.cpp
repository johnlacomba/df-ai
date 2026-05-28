#include "ai.h"
#include "camera.h"
#include "embark.h"
#include "event_manager.h"
#include "hooks.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "LuaTools.h"
#pragma GCC diagnostic pop

#include "modules/Gui.h"
#include "modules/Screen.h"
#include "modules/Translation.h"

#include "df/caste_raw.h"
#include "df/creature_raw.h"
#include "df/embark_profile.h"
#include "df/item.h"
#include "df/region_map_entry.h"
#include "df/viewscreen_adopt_regionst.h"
#include "df/viewscreen_choose_start_sitest.h"
#include "df/viewscreen_dwarfmodest.h"
#include "df/viewscreen_export_regionst.h"
#include "df/viewscreen_game_cleanerst.h"
#include "df/viewscreen_loadgamest.h"
#include "df/viewscreen_new_regionst.h"
#include "df/viewscreen_setupdwarfgamest.h"
#include "df/main_choice_type.h"
#include "df/title_mode_type.h"
#include "df/viewscreen_titlest.h"
#include "df/viewscreen_update_regionst.h"
#include "df/world.h"
#include "df/world_data.h"

REQUIRE_GLOBAL(cur_year);
REQUIRE_GLOBAL(cur_year_tick);
REQUIRE_GLOBAL(standing_orders_gather_refuse_outside);
REQUIRE_GLOBAL(standing_orders_job_cancel_announce);
REQUIRE_GLOBAL(world);

EmbarkExclusive::EmbarkExclusive(AI & ai) :
    ExclusiveCallback{ "embarking" },
    ai(ai),
    last_dump_key(),
    embark_confirm_attempt(0)
{
}

EmbarkExclusive::~EmbarkExclusive()
{
}

void EmbarkExclusive::DumpScreenInfo(color_ostream & out)
{
    df::viewscreen *curview = Gui::getCurViewscreen(true);
    const virtual_identity *ident = virtual_identity::get(curview);
    std::string vsName = ident ? ident->getName() : "(null)";

    auto focusStrings = Gui::getFocusStrings(curview);
    std::string focusList;
    for (auto & f : focusStrings)
    {
        if (!focusList.empty()) focusList += ", ";
        focusList += "\"" + f + "\"";
    }

    std::string dump_key = vsName + "|" + focusList;
    if (dump_key == last_dump_key)
        return;
    last_dump_key = dump_key;

    ai.debug(out, "[SCREEN] type=" + vsName + " focus=[" + focusList + "]");

    if (auto *ts = strict_virtual_cast<df::viewscreen_titlest>(curview))
    {
        ai.debug(out, stl_sprintf("[SCREEN]   titlest: mode=%d selected=%d menu_line_id.size=%zu",
            (int)ts->mode, ts->selected, ts->menu_line_id.size()));
        for (size_t i = 0; i < ts->menu_line_id.size(); i++)
        {
            ai.debug(out, stl_sprintf("[SCREEN]     menu[%zu] = %d (%s)",
                i, (int)ts->menu_line_id[i],
                ENUM_KEY_STR(main_choice_type, ts->menu_line_id[i]).c_str()));
        }
    }
    else if (auto *cs = strict_virtual_cast<df::viewscreen_choose_start_sitest>(curview))
    {
        ai.debug(out, stl_sprintf("[SCREEN]   choose_start_sitest: page=%d zoomed_in=%d choosing_embark=%d "
            "doing_site_finder=%d choosing_civ=%d choosing_reclaim=%d",
            (int)cs->page, (int)cs->zoomed_in, (int)cs->choosing_embark,
            (int)cs->doing_site_finder, (int)cs->choosing_civilization, (int)cs->choosing_reclaim));
        ai.debug(out, stl_sprintf("[SCREEN]     embark_dx=%d embark_dy=%d region_pos=(%d,%d)",
            cs->embark_dx, cs->embark_dy,
            cs->location.region_pos.x, cs->location.region_pos.y));
        ai.debug(out, stl_sprintf("[SCREEN]     embark_pos_min=(%d,%d) embark_pos_max=(%d,%d)",
            cs->location.embark_pos_min.x, cs->location.embark_pos_min.y,
            cs->location.embark_pos_max.x, cs->location.embark_pos_max.y));
        ai.debug(out, stl_sprintf("[SCREEN]     warn_flags=%d animating_quick_start=%d setting_up_map=%d",
            (int)cs->warn_flags.whole, cs->animating_quick_start_timer, cs->setting_up_map_timer));
    }
    else if (auto *nr = strict_virtual_cast<df::viewscreen_new_regionst>(curview))
    {
        ai.debug(out, stl_sprintf("[SCREEN]   new_regionst: raw_load=%d doing_mods=%d doing_simple_params=%d "
            "confirming_start_save=%d",
            (int)nr->raw_load, (int)nr->doing_mods, nr->doing_simple_params,
            nr->confirming_start_save));
        ai.debug(out, stl_sprintf("[SCREEN]     size=%d history=%d civs=%d sites=%d beasts=%d savagery=%d minerals=%d",
            nr->simple_world_size, nr->simple_history, nr->simple_civ_num,
            nr->simple_site_cap, nr->simple_beast, nr->simple_savagery, nr->simple_minerals));
    }
    else if (auto *sd = strict_virtual_cast<df::viewscreen_setupdwarfgamest>(curview))
    {
        ai.debug(out, stl_sprintf("[SCREEN]   setupdwarfgamest: mode=%d initial_selection=%d embark_confirmation=%d "
            "preparing_map_timer=%d selected_u=%d",
            sd->mode, sd->initial_selection, (int)sd->embark_confirmation,
            sd->preparing_map_timer, sd->selected_u));
    }
    else if (auto *dm = strict_virtual_cast<df::viewscreen_dwarfmodest>(curview))
    {
        ai.debug(out, "[SCREEN]   dwarfmodest (fortress mode reached)");
        (void)dm;
    }
    else if (auto *hack = dfhack_viewscreen::try_cast(curview))
    {
        ai.debug(out, "[SCREEN]   DFHack viewscreen: " + hack->getFocusString());
    }
}

void EmbarkExclusive::Run(color_ostream & out)
{
    while (!isFinished() && !MaybeExpectScreen<df::viewscreen_dwarfmodest>(""))
    {
        ClearExpectedScreen();
        AssertDelayed();

        DumpScreenInfo(out);

        if (Screen::isDismissed(Gui::getCurViewscreen(false)))
        {
            Delay();
            continue;
        }

        if (MaybeExpectScreen<df::viewscreen_titlest>(""))
        {
            ViewTitle(out);
            continue;
        }

        if (MaybeExpectScreen<df::viewscreen_adopt_regionst>("") || MaybeExpectScreen<df::viewscreen_export_regionst>(""))
        {
            Delay();
            continue;
        }

        if (MaybeExpectScreen<df::viewscreen_loadgamest>(""))
        {
            ViewLoadGame(out);
            continue;
        }

        if (MaybeExpectScreen<dfhack_lua_viewscreen>("dfhack/lua/load_screen", "loadgame"))
        {
            ViewLoadScreen(out);
            continue;
        }

        if (MaybeExpectScreen<dfhack_lua_viewscreen>("dfhack/lua", "dfhack/lua/load_screen"))
        {
            ViewLoadScreenOptions();
            continue;
        }

        if (MaybeExpectScreen<df::viewscreen_new_regionst>(""))
        {
            ViewNewRegion(out);
            continue;
        }

        if (MaybeExpectScreen<df::viewscreen_update_regionst>(""))
        {
            ViewUpdateRegion(out);
            continue;
        }

        if (MaybeExpectScreen<df::viewscreen_choose_start_sitest>(""))
        {
            ViewChooseStartSite(out);
            continue;
        }

        if (MaybeExpectScreen<df::viewscreen_setupdwarfgamest>(""))
        {
            ViewSetupDwarfGame(out);
            continue;
        }

        if (strict_virtual_cast<dfhack_viewscreen>(Gui::getCurViewscreen(true)))
        {
            Delay();
            continue;
        }

        // viewscreen is unknown
        ai.statechanged(out, SC_VIEWSCREEN_CHANGED);
        Delay();
    }

    ai.debug(out, "embark complete, setting up initial state.");
    // Gui::setMenuWidth removed in Steam DF (old sidebar no longer exists)
    *standing_orders_gather_refuse_outside = 1;
    *standing_orders_job_cancel_announce = config.cancel_announce;
    ai.unpause();
}

void EmbarkExclusive::SelectVerticalMenuItem(int32_t *current, int32_t target)
{
    MoveToItem(current, target);

    Key(interface_key::SELECT);
}

void EmbarkExclusive::SelectHorizontalMenuItem(int32_t *current, int32_t target)
{
    MoveToItem(current, target, interface_key::STANDARDSCROLL_RIGHT, interface_key::STANDARDSCROLL_LEFT);

    Key(interface_key::SELECT);
}

void EmbarkExclusive::ViewTitle(color_ostream & out)
{
    ExpectedScreen<df::viewscreen_titlest> view(this);

    ai.camera.check_record_status();

    if (view->mode == title_mode_type::NONE || view->mode == title_mode_type::MAIN_MENU)
    {
        int32_t target = -1;
        std::string choice_name;

        auto continue_game = std::find(view->menu_line_id.begin(), view->menu_line_id.end(), main_choice_type::Continue);
        if (!config.random_embark_world.empty() && continue_game != view->menu_line_id.end() &&
            std::ifstream("data/save/" + config.random_embark_world + "/world.sav").good())
        {
            target = int32_t(continue_game - view->menu_line_id.begin());
            choice_name = "Continue";
        }

        if (target == -1)
        {
            auto start_game = std::find(view->menu_line_id.begin(), view->menu_line_id.end(), main_choice_type::Start);
            if (!config.random_embark_world.empty() && start_game != view->menu_line_id.end() &&
                std::ifstream("data/save/" + config.random_embark_world + "/world.dat").good())
            {
                target = int32_t(start_game - view->menu_line_id.begin());
                choice_name = "Start";
            }
        }

        if (target == -1)
        {
            auto new_world = std::find(view->menu_line_id.begin(), view->menu_line_id.end(), main_choice_type::NewWorld);
            if (new_world != view->menu_line_id.end())
            {
                target = int32_t(new_world - view->menu_line_id.begin());
                choice_name = "NewWorld";
            }
        }

        if (target == -1)
        {
            ai.debug(out, "[ERROR] ViewTitle: no suitable menu item found");
            Delay();
            return;
        }

        ai.debug(out, stl_sprintf("[STEAM] selecting \"%s\" (index %d)", choice_name.c_str(), target));
        view->selected = target;
        view->game_start_proceed = 1;
        ClearExpectedScreen();
        Delay();
    }
    else if (view->mode == title_mode_type::CONTINUE_INACTIVE)
    {
        if (config.random_embark_world.empty() && view->savegame_header_world.empty())
        {
            ai.debug(out, "[STEAM] no worlds available, going back");
            Key(interface_key::LEAVESCREEN);
            return;
        }

        ai.debug(out, stl_sprintf("[STEAM] selecting world (available: %zu)", view->savegame_header_world.size()));
        view->selected = 0;
        view->game_start_proceed = 1;
        ClearExpectedScreen();
        Delay();
    }
    else
    {
        ai.debug(out, stl_sprintf("[STEAM] ViewTitle: mode=%d selected=%d game_start_proceed=%d",
            (int)view->mode, view->selected, (int)view->game_start_proceed));
        Delay();
    }
}

void EmbarkExclusive::ViewLoadGame(color_ostream & out)
{
    // TODO: viewscreen_loadgamest completely restructured in Steam DF.
    // It no longer contains saves/sel_idx/loading. Save selection moved
    // to the title screen. This viewscreen now only handles the load process.
    ExpectedScreen<df::viewscreen_loadgamest> view(this);

    ai.debug(out, "[STUB] ViewLoadGame: load game screen not yet ported to Steam DF");

    // The load screen is now just a progress indicator. Wait for it.
    Delay();
}

void EmbarkExclusive::ViewLoadScreen(color_ostream & out)
{
    // TODO: viewscreen_loadgamest no longer contains saves list.
    // Save selection is now handled via the title screen.
    // This DFHack lua load_screen overlay may no longer exist in Steam DF.
    ai.debug(out, "[STUB] ViewLoadScreen: lua load screen not yet ported to Steam DF");

    Delay();
}

void EmbarkExclusive::ViewLoadScreenOptions()
{
    // TODO: lua load_screen_options overlay may no longer exist in Steam DF.
    // Save loading is now handled differently via the restructured title screen.
    Delay();
}

void EmbarkExclusive::ViewNewRegion(color_ostream & out)
{
    ExpectedScreen<df::viewscreen_new_regionst> view(this);

    config.set(out, config.random_embark_world, std::string());

    while (!isFinished() && view->raw_load)
    {
        Delay();
    }

    if (view->doing_mods)
    {
        ai.debug(out, "[STEAM] dismissing mod selection");
        view->doing_mods = false;
        Delay();
        return;
    }

    if (view->doing_simple_params == 1)
    {
        int32_t want_size = std::min(std::max(config.world_size, 0), 4);
        int32_t want_minerals = 3;

        ai.debug(out, stl_sprintf("[STEAM] setting world params: size=%d minerals=%d (current: size=%d minerals=%d)",
            want_size, want_minerals, view->simple_world_size, view->simple_minerals));

        view->simple_world_size = want_size;
        view->simple_minerals = want_minerals;

        ai.debug(out, "[STEAM] triggering world generation");
        view->confirming_start_save = 1;
        ClearExpectedScreen();
        Delay();
        return;
    }

    if (!world->entities.all.empty() && view->doing_simple_params == 0 && world->worldgen_status.state == 10)
    {
        ai.debug(out, "world gen finished, save name is " + world->cur_savegame.save_dir);
        config.set(out, config.random_embark_world, world->cur_savegame.save_dir);

        ClearExpectedScreen();
        Key(interface_key::SELECT);
        return;
    }

    Delay();
}

void EmbarkExclusive::ViewUpdateRegion(color_ostream & out)
{
    ExpectedScreen<df::viewscreen_update_regionst> view(this);

    ai.debug(out, "updating world, goal: " + AI::timestamp(view->year, view->year_tick));
    Delay();
}

void EmbarkExclusive::ViewChooseStartSite(color_ostream & out)
{
    ExpectedScreen<df::viewscreen_choose_start_sitest> view(this);

    if (!view->zoomed_in)
    {
        ai.debug(out, "[STEAM] not zoomed in, zooming into current region");
        view->zoomed_in = true;
        Delay();
        return;
    }

    if (view->doing_site_finder)
    {
        if (view->find_block_dx != -1)
        {
            ai.debug(out, stl_sprintf("searching for a site (%d/%d, %d/%d)",
                view->find_block_x,
                world->world_data->world_width / 16,
                view->find_block_y,
                world->world_data->world_height / 16));
            Delay();
            return;
        }

        ai.debug(out, "site finder complete, leaving finder view");
        view->doing_site_finder = false;
        Delay();
        return;
    }

    if (!view->choosing_embark)
    {
        int32_t want_x = std::min(std::max(config.embark_options[embark_finder_option::DimensionX], 1), 16);
        int32_t want_y = std::min(std::max(config.embark_options[embark_finder_option::DimensionY], 1), 16);

        int16_t region_x = view->location.region_pos.x;
        int16_t region_y = view->location.region_pos.y;
        int16_t center_ex = region_x * 16 + 8 - want_x / 2;
        int16_t center_ey = region_y * 16 + 8 - want_y / 2;

        ai.debug(out, stl_sprintf("[STEAM] entering embark placement (%dx%d) at region (%d,%d), "
            "embark tiles (%d,%d)-(%d,%d)",
            want_x, want_y, region_x, region_y,
            center_ex, center_ey,
            center_ex + want_x - 1, center_ey + want_y - 1));

        view->choosing_embark = true;
        view->embark_dx = want_x;
        view->embark_dy = want_y;
        embark_confirm_attempt = 0;
        view->location.embark_pos_min.x = center_ex;
        view->location.embark_pos_min.y = center_ey;
        view->location.embark_pos_max.x = center_ex + want_x - 1;
        view->location.embark_pos_max.y = center_ey + want_y - 1;

        Delay();
        return;
    }

    // choosing_embark is active — try to confirm the embark
    embark_confirm_attempt++;

    if (embark_confirm_attempt <= 3)
    {
        ai.debug(out, stl_sprintf("[STEAM] embark confirm attempt %d: SELECT (pos=(%d,%d)-(%d,%d) warn=%d)",
            embark_confirm_attempt,
            view->location.embark_pos_min.x, view->location.embark_pos_min.y,
            view->location.embark_pos_max.x, view->location.embark_pos_max.y,
            (int)view->warn_flags.whole));
        ClearExpectedScreen();
        Key(interface_key::SELECT);
    }
    else if (embark_confirm_attempt <= 6)
    {
        ai.debug(out, stl_sprintf("[STEAM] embark confirm attempt %d: MENU_CONFIRM", embark_confirm_attempt));
        ClearExpectedScreen();
        Key(interface_key::MENU_CONFIRM);
    }
    else if (embark_confirm_attempt <= 9)
    {
        ai.debug(out, stl_sprintf("[STEAM] embark confirm attempt %d: SEC_SELECT", embark_confirm_attempt));
        ClearExpectedScreen();
        Key(interface_key::SEC_SELECT);
    }
    else
    {
        ai.debug(out, stl_sprintf("[STEAM] embark confirm failed after %d attempts. "
            "page=%d choosing_embark=%d warn_flags=%d. "
            "Try clicking \"Embark!\" manually — keyboard confirmation may not work in Steam DF.",
            embark_confirm_attempt, (int)view->page, (int)view->choosing_embark,
            (int)view->warn_flags.whole));
        Delay(10 * 100);
        embark_confirm_attempt = 0;
    }
}

void EmbarkExclusive::DisplayEmbarkSite(color_ostream & out)
{
    ExpectedScreen<df::viewscreen_choose_start_sitest> view(this);

    ai.debug(out, stl_sprintf("[STEAM] DisplayEmbarkSite: pos=(%d,%d)-(%d,%d)",
        view->location.embark_pos_min.x, view->location.embark_pos_min.y,
        view->location.embark_pos_max.x, view->location.embark_pos_max.y));

    Delay(5 * 100);

    ai.debug(out, "[STEAM] pressing SELECT to embark");
    ClearExpectedScreen();
    Key(interface_key::SELECT);
}

void EmbarkExclusive::ViewSetupDwarfGame(color_ostream & out)
{
    ExpectedScreen<df::viewscreen_setupdwarfgamest> view(this);

    if (view->preparing_map_timer > 0)
    {
        ai.debug(out, stl_sprintf("[STEAM] preparing map (timer=%d)", view->preparing_map_timer));
        Delay();
        return;
    }

    if (view->embark_confirmation)
    {
        ai.debug(out, "[STEAM] confirming embark");
        view->embark_confirmation = false;
        ClearExpectedScreen();
        Delay();
        return;
    }

    if (view->initial_selection)
    {
        ai.debug(out, stl_sprintf("[STEAM] initial selection screen (mode=%d), choosing Play Now",
            view->mode));
        view->initial_selection = 0;
        ClearExpectedScreen();
        Delay();
        return;
    }

    ai.debug(out, stl_sprintf("[STEAM] ViewSetupDwarfGame: mode=%d selected_u=%d, embarking with defaults",
        view->mode, view->selected_u));
    view->embark_confirmation = true;
    Delay();
}

void EmbarkExclusive::ViewTextViewer(color_ostream &)
{
}

RestartWaitExclusive::RestartWaitExclusive(AI & ai) :
    ExclusiveCallback("restart wait"),
    ai(ai)
{
}

RestartWaitExclusive::~RestartWaitExclusive()
{
}

void RestartWaitExclusive::Run(color_ostream & out)
{
    ai.debug(out, "game over. restarting in 1 minute.");

    Delay(60 * 100);

    ai.debug(out, "restarting.");

    Key(interface_key::LEAVESCREEN);

    while (!isFinished() && !MaybeExpectScreen<df::viewscreen_titlest>(""))
    {
        Delay();
    }

    if (!config.no_quit)
    {
        if (config.lockstep)
        {
            Hook_Shutdown_Now();
        }
        else
        {
            Gui::getCurViewscreen(true)->breakdown_level = interface_breakdown_types::QUIT;
        }
    }
    else
    {
        extern bool full_reset_requested;
        full_reset_requested = true;
    }
}
