#pragma once

#include "ColorText.h"
#include <ostream>

// Wraps a color_ostream so that all text written through it is also
// copied to a plain std::ostream (typically the df-ai.log file).
// This makes every out << / out.printerr() call in subsystem code
// visible in the log without changing call sites.
class tee_color_ostream : public DFHack::color_ostream
{
    DFHack::color_ostream &console;
    std::ostream &log;

protected:
    void add_text(color_value color, const std::string &text) override
    {
        console.color(color);
        console.print("{}", text);

        log << text;
        log.flush();
    }

    void flush_proxy() override
    {
        console.flush();
        log.flush();
    }

    void begin_batch() override {}
    void end_batch() override {}

public:
    tee_color_ostream(DFHack::color_ostream &console, std::ostream &log)
        : console(console), log(log) {}
};
