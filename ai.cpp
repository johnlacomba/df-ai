#include "ai.h"
#include "debug.h"
#include "population.h"
#include "plan.h"
#include "plan_setup.h"
#include "stocks.h"
#include "camera.h"
#include "embark.h"
#include "trade.h"
#include "tee_ostream.h"

#include "modules/Gui.h"
#include "modules/Screen.h"

#include "df/enabler.h"
#include "df/plotinfost.h"
#include "df/viewscreen_dwarfmodest.h"
#include "df/viewscreen_titlest.h"
#include "df/world.h"

REQUIRE_GLOBAL(enabler);
REQUIRE_GLOBAL(pause_state);
REQUIRE_GLOBAL(plotinfo);
REQUIRE_GLOBAL(world);

AI::AI() :
    rng{ 0 },
    logger{ "df-ai.log", std::ios::out | std::ios::app },
    eventsJson{},
    pop{ *this },
    plan{ *this },
    stocks{ *this },
    camera{ *this },
    trade{ *this },
    pause_onupdate{ nullptr },
    tag_enemies_onupdate{ nullptr },
    announcements_onupdate{ nullptr },
    seen_focus{},
    seen_cvname{ "viewscreen_dwarfmodest" },
    last_good_x{ -1 },
    last_good_y{ -1 },
    last_good_z{ -1 },
    last_pause_id{ -1 },
    last_pause_repeats{ 0 },
    skip_persist{ false },
    last_announcement_id{ -1 }
{
    Gui::getViewCoords(last_good_x, last_good_y, last_good_z);

    for (int32_t y = 0; y < 25; y++)
    {
        for (int32_t x = 0; x < 80; x++)
        {
            lockstep_log_buffer[y][x] = ' ';
        }
        lockstep_log_color[y] = 7;
    }

    if (config.random_embark)
    {
        events.register_exclusive(std::make_unique<EmbarkExclusive>(*this));
    }
}

AI::~AI()
{
}

bool AI::is_dwarfmode_viewscreen()
{
    if (!world->status.popups.empty())
        return false;
    auto view = Gui::getCurViewscreen(true);
    if (Screen::isDismissed(view))
        return false;
    if (strict_virtual_cast<df::viewscreen_dwarfmodest>(view))
        return true;
    if (auto hack = dfhack_viewscreen::try_cast(view))
    {
        if (view->parent && strict_virtual_cast<df::viewscreen_dwarfmodest>(view->parent))
            return true;
    }
    return false;
}

command_result AI::startup(color_ostream & out)
{
    tee_color_ostream tee_out(out, logger);

    debug(out, "startup: disabling confirm plugin...");
    command_result res = Core::getInstance().runCommand(tee_out, "disable confirm");
    if (res != CR_OK)
        debug(out, "[WARN] startup: 'disable confirm' failed (non-fatal)");
    res = CR_OK;

    if (!config.manage_labors.empty())
    {
        debug(out, "startup: enabling " + config.manage_labors + "...");
        res = Core::getInstance().runCommand(tee_out, "enable " + config.manage_labors);
        if (res != CR_OK)
            debug(out, "[WARN] startup: 'enable " + config.manage_labors + "' failed (non-fatal)");
        res = CR_OK;
    }
    if (config.manage_labors == "autolabor")
    {
        Core::getInstance().runCommand(tee_out, "multicmd autolabor PLANT 5 200 ; autolabor HERBALIST 1 3");
    }
    if (config.manage_labors == "labormanager")
    {
        Core::getInstance().runCommand(tee_out, "multicmd labormanager max HERBALIST 3 ; labormanager priority MINE 250");
    }

    debug(out, "startup: pop.startup...");
    res = pop.startup(tee_out);
    if (res != CR_OK) { debug(out, "[ERROR] startup: pop.startup failed"); return res; }

    debug(out, "startup: plan.startup...");
    res = plan.startup(tee_out);
    if (res != CR_OK) { debug(out, "[ERROR] startup: plan.startup failed"); return res; }

    debug(out, "startup: stocks.startup...");
    res = stocks.startup(tee_out);
    if (res != CR_OK) { debug(out, "[ERROR] startup: stocks.startup failed"); return res; }

    debug(out, "startup: camera.startup...");
    res = camera.startup(tee_out);
    if (res != CR_OK) { debug(out, "[ERROR] startup: camera.startup failed"); return res; }

    debug(out, "startup: complete.");
    return CR_OK;
}

class AbandonExclusive : public ExclusiveCallback
{
public:
    AbandonExclusive() : ExclusiveCallback("abandon", 2) {}

    virtual void Run(color_ostream & out)
    {
        out << "AI: abandoning — blueprint setup failed. AI will stop managing this fortress." << std::endl;
        out << "AI: to retry, run: disable df-ai   then: enable df-ai" << std::endl;
    }
};

void AI::abandon(color_ostream & out)
{
    extern std::unique_ptr<AI> dwarfAI;
    if (dwarfAI)
    {
        dwarfAI->debug(out, "[ERROR] AI is shutting down (abandon called).");
        dwarfAI->onupdate_unregister(out);
    }
    events.register_exclusive(std::make_unique<AbandonExclusive>(), true);
}

void AI::timeout_sameview(int32_t seconds, std::function<void(color_ostream &)> cb)
{
    // allow exclusive views to unpause the game
    if (events.has_exclusive())
    {
        cb(Core::getInstance().getConsole());
        return;
    }

    df::viewscreen *curscreen = Gui::getCurViewscreen(true);
    std::string name("unknown viewscreen");
    if (auto hack = dfhack_viewscreen::try_cast(curscreen))
    {
        name = "dfhack/" + hack->getFocusString();
    }
    else if (const virtual_identity *ident = virtual_identity::get(curscreen))
    {
        name = ident->getName();
    }
    int32_t *counter = new int32_t(enabler->fps * seconds);

    events.onupdate_register_once("timeout_sameview on " + name, [this, curscreen, counter, cb](color_ostream & out) -> bool
    {
        if (Gui::getCurViewscreen(true) != curscreen)
        {
            delete counter;
            return true;
        }

        if (--*counter <= 0)
        {
            delete counter;
            cb(out);
            return true;
        }
        return false;
    });
}

static int32_t time_paused = 0;

command_result AI::onupdate_register(color_ostream & out)
{
    command_result res = CR_OK;
    if (res == CR_OK)
        res = pop.onupdate_register(out);
    if (res == CR_OK)
        res = plan.onupdate_register(out);
    if (res == CR_OK)
        res = stocks.onupdate_register(out);
    if (res == CR_OK)
        res = camera.onupdate_register(out);
    if (res == CR_OK)
    {
        time_paused = 0;
        pause_onupdate = events.onupdate_register_once("df-ai unpause", [this](color_ostream &) -> bool
        {
            if (!*pause_state && world->status.popups.empty())
            {
                Gui::getViewCoords(last_good_x, last_good_y, last_good_z);
                time_paused = 0;
                return false;
            }

            time_paused++;

            if (time_paused == enabler->fps * 2)
            {
                unpause();
                time_paused = 0;
            }

            return false;
        });
        tag_enemies_onupdate = events.onupdate_register("df-ai tag_enemies", 1200, 1200, [this](color_ostream & out) {
            try { tag_enemies(out); }
            catch (std::exception &e) { debug(out, stl_sprintf("[tag_enemies] EXCEPTION: %s", e.what())); }
            catch (...) { debug(out, "[tag_enemies] UNKNOWN EXCEPTION"); }
        });
        announcements_onupdate = events.onupdate_register("df-ai announcement watcher", 1, 1, [this](color_ostream &) { watch_announcements(); });
        events.onstatechange_register_once("world unload watcher", [this](color_ostream & out, state_change_event st) -> bool
        {
            if (st == SC_WORLD_UNLOADED)
            {
                debug(out, "world unloaded, disabling self");
                onupdate_unregister(out);
                return true;
            }
            statechanged(out, st);
            return false;
        });
    }
    return res;
}

command_result AI::onupdate_unregister(color_ostream & out)
{
    command_result res = CR_OK;
    if (res == CR_OK)
        res = camera.onupdate_unregister(out);
    if (res == CR_OK)
        res = stocks.onupdate_unregister(out);
    if (res == CR_OK)
        res = plan.onupdate_unregister(out);
    if (res == CR_OK)
        res = pop.onupdate_unregister(out);
    if (res == CR_OK)
    {
        events.onupdate_unregister(pause_onupdate);
        events.onupdate_unregister(tag_enemies_onupdate);
        events.onupdate_unregister(announcements_onupdate);
    }
    return res;
}

command_result AI::persist(color_ostream & out)
{
    command_result res = CR_OK;
    if (skip_persist)
        return res;

    if (res == CR_OK)
        res = plan.persist(out);
    return res;
}

command_result AI::unpersist(color_ostream & out)
{
    command_result res = CR_OK;
    if (res == CR_OK)
        res = plan.unpersist(out);
    return res;
}

bool AI::is_embarking()
{
    return events.has_exclusive<EmbarkExclusive>() || events.has_exclusive<PlanSetup>();
}

DFAI_NOINLINE std::ostream & dfai_debug_log()
{
    static std::ofstream log;

    if (DFAI_UNLIKELY(!log.is_open()))
    {
        log.open("df-ai-debug.log", std::ios::out | std::ios::app);
        log << "\n\ndf-ai debug log opened. version information follows:" << std::endl;
        ai_version(log);

        color_ostream_proxy out(Core::getInstance().getConsole());
        out << std::endl;
        out << std::endl;
        out << COLOR_LIGHTRED << "It was inevitable. ";
        out << COLOR_YELLOW << "df-ai has encountered an issue." << std::endl;
        out << "Some information that might help fix this has been written to a file named ";
        out << COLOR_LIGHTCYAN << "df-ai-debug.log";
        out << COLOR_YELLOW << " in your Dwarf Fortress folder." << std::endl;
        out << "If you would like to help, create an issue at https://github.com/BenLubar/df-ai/issues/new (you can drag or copy and paste the log file into the editor)." << std::endl;

#ifndef DFAI_RELEASE
        out << COLOR_LIGHTRED << "If your game crashes after this message, please attach a debugger or use a release mode version of df-ai." << std::endl;
#endif
        out << std::endl;
        out << std::endl;
    }

    return log;
}
