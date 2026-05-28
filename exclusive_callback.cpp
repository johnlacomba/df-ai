#include "ai.h"
#include "exclusive_callback.h"
#include "debug.h"

#include "modules/Gui.h"
#include "modules/Screen.h"

#include "df/viewscreen.h"

#undef Key
#undef Char

ExclusiveCallback::ExclusiveCallback(const std::string & description, size_t wait_multiplier) :
    out_proxy(),
    turn(turn_t::MAIN),
    finished(false),
    current_out(nullptr),
    wait_multiplier(wait_multiplier < 1 ? 1 : wait_multiplier),
    wait_frames(0),
    did_delay(true),
    feed_keys(),
    expectedScreen(),
    expectedFocus(),
    expectedParentFocus(),
    description(description),
    dfplex_blacklist(false)
{
    worker = std::thread([this]() { worker_main(); });
}

ExclusiveCallback::~ExclusiveCallback()
{
    {
        std::lock_guard<std::mutex> lock(mtx);
        finished = true;
        turn = turn_t::WORKER;
    }
    cv_worker.notify_one();
    if (worker.joinable())
        worker.join();
}

void ExclusiveCallback::worker_main()
{
    {
        std::unique_lock<std::mutex> lock(mtx);
        cv_worker.wait(lock, [this]() { return turn == turn_t::WORKER || finished; });
        if (finished)
            return;
    }

    out_proxy.set(*current_out);
    Run(out_proxy);
    out_proxy.clear();

    if (!feed_keys.empty())
    {
        wait_multiplier = 1;
        Delay();
    }

    {
        std::lock_guard<std::mutex> lock(mtx);
        finished = true;
        turn = turn_t::MAIN;
    }
    cv_main.notify_one();
}

void ExclusiveCallback::KeyNoDelay(df::interface_key key)
{
    feed_keys.push_back(key);
}

void ExclusiveCallback::Key(df::interface_key key, const char *filename, int lineno)
{
    checkScreen(filename, lineno);
    KeyNoDelay(key);
    Delay();
}

const static char safe_char[128] =
{
    'C', 'u', 'e', 'a', 'a', 'a', 'a', 'c', 'e', 'e', 'e', 'i', 'i', 'i', 'A', 'A',
    'E', 0, 0, 'o', 'o', 'o', 'u', 'u', 'y', 'O', 'U', 0, 0, 0, 0, 0,
    'a', 'i', 'o', 'u', 'n', 'N', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

void ExclusiveCallback::Char(char c, const char *filename, int lineno)
{
    if (c < 0)
    {
        c = safe_char[(uint8_t)c - 128];
    }
    Key(Screen::charToKey(c), filename, lineno);
}

void ExclusiveCallback::Delay(size_t frames)
{
    for (size_t i = 0; i < frames; i++)
    {
        {
            std::lock_guard<std::mutex> lock(mtx);
            turn = turn_t::MAIN;
        }
        cv_main.notify_one();

        {
            std::unique_lock<std::mutex> lock(mtx);
            cv_worker.wait(lock, [this]() { return turn == turn_t::WORKER || finished; });
            if (finished)
                return;
        }
        out_proxy.set(*current_out);
    }

    while (Screen::isDismissed(Gui::getCurViewscreen(false)))
    {
        size_t real_wait_multiplier = wait_multiplier;
        wait_multiplier = 1;

        {
            std::lock_guard<std::mutex> lock(mtx);
            turn = turn_t::MAIN;
        }
        cv_main.notify_one();

        {
            std::unique_lock<std::mutex> lock(mtx);
            cv_worker.wait(lock, [this]() { return turn == turn_t::WORKER || finished; });
            if (finished)
                return;
        }
        out_proxy.set(*current_out);
        wait_multiplier = real_wait_multiplier;
    }

    did_delay = true;
}

void ExclusiveCallback::AssertDelayed()
{
    DFAI_ASSERT(did_delay, "previous iteration of exclusive callback \"" << description << "\" did not call Delay.");
    did_delay = false;
}

void ExclusiveCallback::checkScreen(const char *filename, int lineno)
{
    if (!expectedScreen)
    {
        return;
    }

    bool first = true;
    for (;;)
    {
        df::viewscreen *curview = Gui::getCurViewscreen(true);

        bool isExpectedScreen = expectedScreen->is_instance(curview) &&
            (expectedFocus.empty() || Gui::getFocusString(curview) == expectedFocus) &&
            (expectedParentFocus.empty() || Gui::getFocusString(curview->parent) == expectedParentFocus);

        if (first)
        {
            DFAI_ASSERT_LOC(isExpectedScreen,
                "expected screen to be " << expectedScreen->getName() << ":" << expectedFocus << ":" << expectedParentFocus <<
                ", but it is " << virtual_identity::get(curview)->getName() << ":" << Gui::getFocusString(curview) << ":" << Gui::getFocusString(curview->parent),
                filename, lineno);
        }

        if (isExpectedScreen)
        {
            return;
        }

        first = false;

        Delay();
    }
}

bool ExclusiveCallback::run(color_ostream & out, const std::function<void(std::vector<df::interface_key> &)> & send_keys)
{
    if (wait_frames)
    {
        wait_frames--;
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mtx);
        current_out = &out;
        turn = turn_t::WORKER;
    }
    cv_worker.notify_one();

    {
        std::unique_lock<std::mutex> lock(mtx);
        cv_main.wait(lock, [this]() { return turn == turn_t::MAIN; });
    }

    if (!feed_keys.empty())
    {
        send_keys(feed_keys);
        feed_keys.clear();
    }

    if (finished)
    {
        return true;
    }

    wait_frames = wait_multiplier - 1;
    return false;
}
