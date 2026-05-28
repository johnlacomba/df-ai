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
    last_dump_key()
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
        ai.debug(out, stl_sprintf("[SCREEN]   choose_start_sitest: doing_site_finder=%d find_select=%d",
            (int)cs->doing_site_finder, cs->find_select));
        ai.debug(out, stl_sprintf("[SCREEN]     location.region_pos=(%d,%d)",
            cs->location.region_pos.x, cs->location.region_pos.y));
    }
    else if (auto *nr = strict_virtual_cast<df::viewscreen_new_regionst>(curview))
    {
        ai.debug(out, stl_sprintf("[SCREEN]   new_regionst: raw_load=%d doing_mods=%d doing_simple_params=%d simple_world_size=%d",
            (int)nr->raw_load, (int)nr->doing_mods, nr->doing_simple_params, nr->simple_world_size));
    }
    else if (auto *sd = strict_virtual_cast<df::viewscreen_setupdwarfgamest>(curview))
    {
        ai.debug(out, stl_sprintf("[SCREEN]   setupdwarfgamest: embark_confirmation=%d initial_selection=%d",
            (int)sd->embark_confirmation, (int)sd->initial_selection));
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

        // DFHack overlays (launcher, etc.) — dismiss and wait
        if (strict_virtual_cast<dfhack_viewscreen>(Gui::getCurViewscreen(true)))
        {
            ai.debug(out, "[EMBARK] dismissing DFHack overlay");
            KeyNoDelay(interface_key::LEAVESCREEN);
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
    // TODO: viewscreen_titlest fields completely restructured in Steam DF.
    // The old sel_subpage/sel_menu_line/start_savegames/submenu_line_id fields
    // are gone. Now uses mode/selected/savegame_header/savegame_header_world.
    // The save selection subpages (StartSelectWorld, StartSelectMode) no longer
    // exist as nested enums. Needs full rewrite to match new title screen UI flow.
    ExpectedScreen<df::viewscreen_titlest> view(this);

    ai.camera.check_record_status();

    if (view->mode == title_mode_type::NONE || view->mode == title_mode_type::MAIN_MENU)
    {
        // Main menu: try Continue > Start > NewWorld
        auto continue_game = std::find(view->menu_line_id.begin(), view->menu_line_id.end(), main_choice_type::Continue);

        if (!config.random_embark_world.empty() && continue_game != view->menu_line_id.end() && std::ifstream("data/save/" + config.random_embark_world + "/world.sav").good())
        {
            ai.debug(out, "choosing \"Continue Game\"");
            SelectVerticalMenuItem(&view->selected, int32_t(continue_game - view->menu_line_id.begin()));
            return;
        }

        auto start_game = std::find(view->menu_line_id.begin(), view->menu_line_id.end(), main_choice_type::Start);

        if (!config.random_embark_world.empty() && start_game != view->menu_line_id.end() && std::ifstream("data/save/" + config.random_embark_world + "/world.dat").good())
        {
            ai.debug(out, "choosing \"Start Game\"");
            SelectVerticalMenuItem(&view->selected, int32_t(start_game - view->menu_line_id.begin()));
            return;
        }

        auto new_world = std::find(view->menu_line_id.begin(), view->menu_line_id.end(), main_choice_type::NewWorld);

        if (new_world == view->menu_line_id.end())
        {
            ai.debug(out, "[ERROR] ViewTitle: \"New World\" not found in menu");
            Delay();
            return;
        }

        ai.debug(out, stl_sprintf("choosing \"New World\" (index %d, selected=%d)",
            int32_t(new_world - view->menu_line_id.begin()), view->selected));
        SelectVerticalMenuItem(&view->selected, int32_t(new_world - view->menu_line_id.begin()));
    }
    else if (view->mode == title_mode_type::CONTINUE_INACTIVE)
    {
        // Save/world selection - replaces old StartSelectWorld
        if (config.random_embark_world.empty())
        {
            ai.debug(out, "leaving \"Select World\" (no save name)");
            Key(interface_key::LEAVESCREEN);
            return;
        }

        // TODO: navigate savegame_header_world to find the correct save
        // Old code searched start_savegames by save_dir, but that struct is gone.
        // For now, just select the first entry.
        ai.debug(out, "[STUB] selecting first available world save");
        Key(interface_key::SELECT);
    }
    else
    {
        // Unknown title mode, just wait
        ai.debug(out, stl_sprintf("[STUB] ViewTitle: unhandled title mode %d", (int)view->mode));
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
        // wait for screen to initialize (loading raw files)
        Delay();
    }

    if (view->doing_mods)
    {
        // dismiss mod selection if shown (Steam DF replaces old welcome disclaimer)
        ai.debug(out, "leaving mod selection");
        Key(interface_key::LEAVESCREEN);
        return;
    }

    if (view->doing_simple_params == 1)
    {
        ai.debug(out, "choosing \"Generate World\"");

        int32_t want_size = std::min(std::max(config.world_size, 0), 4);

        if (view->simple_world_size != want_size)
        {
            while (view->simple_sel != 0)
            {
                Key(interface_key::STANDARDSCROLL_UP);
            }

            SelectHorizontalMenuItem(&view->simple_world_size, want_size);
        }

        int32_t want_minerals = 3;

        if (view->simple_minerals != want_minerals)
        {
            while (view->simple_sel != 6)
            {
                Key(interface_key::STANDARDSCROLL_DOWN);
            }

            SelectHorizontalMenuItem(&view->simple_minerals, want_minerals);
        }

        Key(interface_key::MENU_CONFIRM);

        return;
    }

    if (!world->entities.all.empty() && view->doing_simple_params == 0 && world->worldgen_status.state == 10)
    {
        ai.debug(out, "world gen finished, save name is " + world->cur_savegame.save_dir);
        config.set(out, config.random_embark_world, world->cur_savegame.save_dir);

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

    bool no_preference = true;

    FOR_ENUM_ITEMS(embark_finder_option, o)
    {
        if (o < 0)
            continue;

        if (o == embark_finder_option::DimensionX || o == embark_finder_option::DimensionY)
        {
            continue;
        }

        if (config.embark_options[o] != -1)
        {
            no_preference = false;
            break;
        }
    }

    if (no_preference)
    {
        ai.debug(out, "no embark preferences; skipping site finder");

        DisplayEmbarkSite(out);

        return;
    }

    if (!view->doing_site_finder)
    {
        ai.debug(out, "choosing \"Site Finder\"");

        // TODO: interface_key::SETUP_FIND removed in Steam DF
        // Key(interface_key::SETUP_FIND);

        // Set site finder parameters using the flattened find_param array
        FOR_ENUM_ITEMS(embark_finder_option, o)
        {
            if (o < 0)
                continue;

            if (view->find_param[o] == config.embark_options[o])
            {
                continue;
            }

            // In Steam DF, the finder options are flattened into find_param[].
            // find_select is the cursor index, find_param_list contains visible options.
            auto visible = std::find(view->find_param_list.begin(), view->find_param_list.end(), (int32_t)o);

            if (visible == view->find_param_list.end())
            {
                ai.debug(out, "[CHEAT] Setting hidden site finder option " + enum_item_key(o));
                view->find_param[o] = config.embark_options[o];

                continue;
            }

            MoveToItem(&view->find_select, int32_t(visible - view->find_param_list.begin()));

            if (o == embark_finder_option::DimensionX || o == embark_finder_option::DimensionY)
            {
                int32_t target = std::min(std::max(config.embark_options[o], 1), 16);
                MoveToItem(&view->find_param[o], target, interface_key::STANDARDSCROLL_RIGHT, interface_key::STANDARDSCROLL_LEFT);
                continue;
            }

            if (config.embark_options[o] == -1)
            {
                while (view->find_param[o] != -1)
                {
                    Key(interface_key::STANDARDSCROLL_LEFT);
                }

                continue;
            }

            if (view->find_param[o] == -1)
            {
                Key(interface_key::STANDARDSCROLL_RIGHT);
            }

            while (view->find_param[o] != -1 && view->find_param[o] != config.embark_options[o])
            {
                if (view->find_param[o] > config.embark_options[o])
                {
                    Key(interface_key::STANDARDSCROLL_LEFT);
                }
                else
                {
                    Key(interface_key::STANDARDSCROLL_RIGHT);
                }
            }
        }

        Key(interface_key::SELECT);

        return;
    }

    while (!isFinished() && view->find_block_dx != -1)
    {
        ai.debug(out, stl_sprintf("searching for a site (%d/%d, %d/%d)",
            view->find_block_x,
            world->world_data->world_width / 16,
            view->find_block_y,
            world->world_data->world_height / 16));

        Delay();
    }

    ai.debug(out, "choosing \"Embark\"");

    Key(interface_key::LEAVESCREEN);

    df::coord2d start = view->location.region_pos;
    std::vector<df::coord2d> sites;
    for (int16_t x = 0; x < world->world_data->world_width; x++)
    {
        for (int16_t y = 0; y < world->world_data->world_height; y++)
        {
            if (world->world_data->region_map[x][y].finder_rank >= 10000)
            {
                sites.push_back(df::coord2d(x, y));
            }
        }
    }
    if (sites.empty())
    {
        ai.debug(out, "leaving embark selector (no good embarks)");
        config.set(out, config.random_embark_world, std::string());
        AI::abandon(out);
        Delay();
        return;
    }

    ai.debug(out, stl_sprintf("found sites count: %zu", sites.size()));
    // Don't embark on the same region every time.
    std::vector<std::seed_seq::result_type> seeds;
    seeds.push_back(std::seed_seq::result_type(ai.rng()));
    seeds.push_back(std::seed_seq::result_type(*cur_year));
    seeds.push_back(std::seed_seq::result_type(*cur_year_tick));
    std::seed_seq seeds_seq(seeds.begin(), seeds.end());
    std::mt19937 rng(seeds_seq);
    df::coord2d site = sites[std::uniform_int_distribution<size_t>(0, sites.size() - 1)(rng)];
    df::coord2d selected_site_diff = site - start;

    while (selected_site_diff.x > 0)
    {
        selected_site_diff.x--;

        Key(interface_key::CURSOR_RIGHT);
    }

    while (selected_site_diff.x < 0)
    {
        selected_site_diff.x++;

        Key(interface_key::CURSOR_LEFT);
    }

    while (selected_site_diff.y > 0)
    {
        selected_site_diff.y--;

        Key(interface_key::CURSOR_DOWN);
    }

    while (selected_site_diff.y < 0)
    {
        selected_site_diff.y++;

        Key(interface_key::CURSOR_UP);
    }

    // TODO: interface_key::SETUP_FIND removed in Steam DF
    // Key(interface_key::SETUP_FIND);

    DisplayEmbarkSite(out);
}

void EmbarkExclusive::DisplayEmbarkSite(color_ostream &)
{
    // TODO: viewscreen_choose_start_sitest::Biome, interface_key::SETUP_BIOME_1,
    // and interface_key::SETUP_EMBARK all removed in Steam DF.
    // The embark site display/biome selection and embark confirmation flow
    // needs to be rewritten for the new UI.
    ExpectedScreen<df::viewscreen_choose_start_sitest> view(this);

    Delay(5 * 100);

    // Just try to embark with SELECT for now
    Key(interface_key::SELECT);

    // dismiss warnings
    Key(interface_key::SELECT);
}

void EmbarkExclusive::ViewSetupDwarfGame(color_ostream & out)
{
    // TODO: viewscreen_setupdwarfgamest completely restructured in Steam DF.
    // Old fields show_play_now/choices/choice_types/choice/animal_cursor/animals/
    // items/item_cursor are all gone. Now uses mode/selected_u/selected_i/
    // selected_pet/s_item/embark_profile/embark_confirmation/initial_selection.
    // Needs full rewrite to match new embark preparation UI.
    ExpectedScreen<df::viewscreen_setupdwarfgamest> view(this);

    if (view->embark_confirmation)
    {
        Key(interface_key::SELECT);
        return;
    }

    if (view->initial_selection)
    {
        // initial_selection shows play now / prepare / profile choices
        // For now, just pick the first option ("Play Now" equivalent)
        ai.debug(out, "[STUB] ViewSetupDwarfGame: choosing first initial selection option");
        Key(interface_key::SELECT);
        return;
    }

    // If we're past the initial selection, just embark with defaults
    // TODO: interface_key::SETUP_EMBARK removed in Steam DF
    ai.debug(out, "[STUB] ViewSetupDwarfGame: embarking with default loadout");
    Key(interface_key::SELECT);
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
