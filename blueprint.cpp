#include "blueprint.h"

#include <filesystem>
#include "Core.h"
#include "modules/Filesystem.h"

template<typename T>
static T *load_json(const std::string & filename, const std::string & type, const std::string & name, std::string & error)
{
    error.clear();
    std::ifstream f(filename);
    Json::Value val;
    Json::CharReaderBuilder b;
    if (Json::parseFromStream(b, f, &val, &error))
    {
        if (f.good())
        {
            if (val.isObject())
            {
                T *t = new T(type, name);
                if (t->apply(val, error))
                {
                    return t;
                }
                delete t;
            }
            else
            {
                error = "must be a JSON object";
            }
        }
        else
        {
            error = "error reading file";
        }
    }
    error = filename + ": " + error;
    return nullptr;
}

template<typename T>
void load_objects(color_ostream & out, const std::string & base, const std::string & subtype, std::function<void(const std::string & type, const std::string & name, T *t)> store)
{
    std::string error;
    std::vector<std::filesystem::path> types;
    if (!Filesystem::listdir(base + "/rooms/" + subtype, types))
    {
        for (auto & type_path : types)
        {
            std::string type = type_path.string();
            if (type.find('.') != std::string::npos)
            {
                continue;
            }

            std::vector<std::filesystem::path> names;
            if (!Filesystem::listdir(base + "/rooms/" + subtype + "/" + type, names))
            {
                for (auto & name_path : names)
                {
                    std::string name = name_path.string();
                    auto ext = name.rfind(".json");
                    if (ext == std::string::npos || ext != name.size() - strlen(".json"))
                    {
                        continue;
                    }

                    std::string path = base + "/rooms/" + subtype + "/" + type + "/" + name;
                    std::string base_name = name.substr(0, ext);

                    if (auto t = load_json<T>(path, type, base_name, error))
                    {
                        store(type, base_name, t);
                    }
                    else
                    {
                        out.printerr("%s\n", error.c_str());
                    }
                }
            }
        }
    }
}

static std::string find_blueprint_base()
{
    if (Filesystem::isdir("df-ai-blueprints"))
        return "df-ai-blueprints";

    auto hack_path = Core::getInstance().getHackPath();
    auto parent = hack_path.parent_path();
    auto alt = (parent / "df-ai-blueprints").string();
    if (Filesystem::isdir(alt))
        return alt;

    return std::string();
}

blueprints_t::blueprints_t(color_ostream & out) : is_valid(true)
{
    std::string base = find_blueprint_base();
    if (base.empty())
    {
        is_valid = false;
        out << "df-ai: CWD=" << Filesystem::getcwd().string()
            << " hackPath=" << Core::getInstance().getHackPath().string() << std::endl;
        out.printerr("The df-ai-blueprints folder is missing!\n");
        out.printerr("Looked in: %s/df-ai-blueprints/ and next to hack/ directory.\n", Filesystem::getcwd().string().c_str());
        return;
    }
    out << "df-ai: using blueprint base: " << base << std::endl;

    std::map<std::string, std::pair<std::vector<std::pair<std::string, room_template *>>, std::vector<std::pair<std::string, room_instance *>>>> rooms;

    load_objects<room_template>(out, base, "templates", [&rooms](const std::string & type, const std::string & name, room_template *tmpl)
    {
        rooms[type].first.push_back(std::make_pair(name, tmpl));
    });
    load_objects<room_instance>(out, base, "instances", [&rooms](const std::string & type, const std::string & name, room_instance *inst)
    {
        rooms[type].second.push_back(std::make_pair(name, inst));
    });

    out << "df-ai: loaded " << rooms.size() << " room types" << std::endl;

    std::string error;
    for (auto & type : rooms)
    {
        auto & rbs = blueprints[type.first];
        rbs.reserve(type.second.first.size() * type.second.second.size());
        if (type.second.first.empty())
        {
            is_valid = false;
            out << "[ERROR] df-ai: " << type.first << ": no templates" << std::endl;
        }
        if (type.second.second.empty())
        {
            is_valid = false;
            out << "[ERROR] df-ai: " << type.first << ": no instances" << std::endl;
        }
        for (auto tmpl : type.second.first)
        {
            for (auto inst : type.second.second)
            {
                if (inst.second->blacklist.count(tmpl.first))
                {
                    continue;
                }

                room_blueprint *rb = new room_blueprint(tmpl.second, inst.second);
                if (!rb->apply(error))
                {
                    is_valid = false;
                    out << "[ERROR] df-ai: " << tmpl.first << " + " << inst.first << ": " << error << std::endl;
                    delete rb;
                    continue;
                }
                if (rb->warn(error))
                {
                    is_valid = false;
                    out << "[WARN] df-ai: " << tmpl.first << " + " << inst.first << ": " << error << std::endl;
                }
                rbs.push_back(rb);
            }
            delete tmpl.second;
        }
        for (auto inst : type.second.second)
        {
            delete inst.second;
        }
    }

    std::vector<std::filesystem::path> plan_names;
    if (!Filesystem::listdir(base + "/plans", plan_names))
    {
        out << "df-ai: found " << plan_names.size() << " files in plans directory" << std::endl;
        for (auto & name_path : plan_names)
        {
            std::string name = name_path.string();
            auto ext = name.rfind(".json");
            if (ext == std::string::npos || ext != name.size() - strlen(".json"))
            {
                continue;
            }

            std::string path = base + "/plans/" + name;

            if (auto plan = load_json<blueprint_plan_template>(path, "plan", name.substr(0, ext), error))
            {
                out << "df-ai: loaded plan: " << name.substr(0, ext) << std::endl;
                plans[name.substr(0, ext)] = plan;
            }
            else
            {
                is_valid = false;
                out << "[ERROR] df-ai: failed to load plan " << name << ": " << error << std::endl;
            }
        }
    }
    else
    {
        out << "[ERROR] df-ai: could not list " << base << "/plans directory" << std::endl;
    }

    out << "df-ai: blueprint loading complete. " << plans.size() << " plans, " << blueprints.size() << " room types loaded." << std::endl;
}

blueprints_t::~blueprints_t()
{
    for (auto & type : blueprints)
    {
        for (auto rb : type.second)
        {
            delete rb;
        }
    }

    for (auto & plan : plans)
    {
        delete plan.second;
    }
}

void blueprints_t::write_rooms(std::ostream & f)
{
    for (auto & type : blueprints)
    {
        for (auto rb : type.second)
        {
            f << rb->type << " / " << rb->tmpl_name << " / " << rb->name << std::endl;
            rb->write_layout(f);
            f << std::endl;
        }
    }
}
