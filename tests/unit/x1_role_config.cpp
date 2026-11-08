#include "../../plugins/first-party/devices/x1/role_config.hpp"
#include <fstream>
#include <iostream>
#include <mantis/base.hpp>
#include <sys/stat.h>
using namespace x1::roles;
using Json = nlohmann::json;
#define CHECK(...)                                                                                           \
    do {                                                                                                     \
        if (!(__VA_ARGS__))                                                                                  \
            throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #__VA_ARGS__);                \
    } while (false)
template <class F> void rejects(F fn) {
    bool rejected = false;
    try {
        fn();
    } catch (const std::exception &) {
        rejected = true;
    }
    CHECK(rejected);
}
void write(const std::filesystem::path &file, const std::string &bytes) {
    std::ofstream out(file);
    out << bytes;
    out.close();
    CHECK(out);
}
struct Fixture {
    std::filesystem::path root = std::filesystem::temp_directory_path() / mantis::Id::random().value;
    std::filesystem::path dir = root / "state", profile = root / "base.json";
    std::string bytes;
    explicit Fixture(const std::filesystem::path &source) {
        std::filesystem::create_directories(dir);
        CHECK(::chmod(root.c_str(), 0700) == 0);
        CHECK(::chmod(dir.c_str(), 0700) == 0);
        bytes = read_profile(source);
        write(profile, bytes);
    }
    ~Fixture() {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
};
int main(int argc, char **argv) {
    try {
        CHECK(argc == 2);
        {
            Fixture f(argv[1]);
            {
                Store store(f.dir, f.profile);
                rejects([&] { Store second(f.dir, f.profile); });
                CHECK(store.mapping() == Mapping{});
                unsigned refreshed = 0;
                store.apply({}, true, [&](Mapping m) {
                    ++refreshed;
                    auto resolved = resolve(f.dir, f.bytes);
                    CHECK(resolved.mapping == m);
                    CHECK(resolved.profile["measurement_cameras"]["left"]["sensor_identity"] ==
                          "ov9281 20-0060");
                    CHECK(resolved.profile["measurement_cameras"]["right"]["sensor_identity"] ==
                          "ov9281 18-0060");
                });
                CHECK(refreshed == 1);
                CHECK(store.mapping() == Mapping{1, true});
                rejects([&] { store.apply({}, true, [](Mapping) {}); });
                store.apply({1, true}, true, [](Mapping) {}); // Repeating desired state cannot swap back.
                CHECK(store.mapping() == Mapping{1, true});
                CHECK(read_profile(f.profile) == f.bytes);
            }
            Store restarted(f.dir, f.profile);
            CHECK(restarted.mapping() == Mapping{1, true});
            restarted.apply({1, true}, false, [](Mapping m) { CHECK(m == Mapping{2, false}); });
            CHECK(resolve(f.dir, f.bytes).profile == Json::parse(f.bytes));
        }
        for (const std::string phase :
             {"write", "file-sync", "rename", "renamed", "directory-sync", "refresh"}) {
            Fixture f(argv[1]);
            bool injected = false;
            Store store(f.dir, f.profile, [&](std::string_view point) {
                if (point == phase && !injected) {
                    injected = true;
                    throw Error("Injected " + phase);
                }
            });
            rejects([&] { store.apply({}, true, [](Mapping) {}); });
            CHECK(injected);
            CHECK(store.mapping() == Mapping{});
            CHECK(!std::filesystem::exists(f.dir / "x1-role-map.v1.json"));
            CHECK(resolve(f.dir, f.bytes).profile == Json::parse(f.bytes));
            CHECK(read_profile(f.profile) == f.bytes);
        }
        {
            Fixture f(argv[1]);
            auto base = Json::parse(f.bytes);
            for (const char *role : {"left", "right"}) {
                auto &route = base["measurement_cameras"][role]["route"];
                route = Json::array({base["measurement_cameras"][role]["sensor_identity"]});
                for (unsigned i = 0; i < 14; ++i)
                    route.push_back(std::string(220, 'x') + std::to_string(i));
                route.push_back(std::string(role) + "-capture");
            }
            f.bytes = base.dump();
            write(f.profile, f.bytes);
            bool partial = false;
            Store store(f.dir, f.profile, [&](std::string_view phase) {
                if (phase == "write-progress") {
                    const auto length = std::filesystem::file_size(f.dir / ".x1-role-map.v1.tmp");
                    CHECK(length > 0 && length < f.bytes.size());
                    partial = true;
                    throw Error("Injected partial-file failure");
                }
            });
            rejects([&] { store.apply({}, true, [](Mapping) {}); });
            CHECK(partial && !std::filesystem::exists(f.dir / "x1-role-map.v1.json"));
            CHECK(read_profile(f.profile) == f.bytes);
        }
        {
            Fixture f(argv[1]);
            bool uncertain = false;
            Store store(f.dir, f.profile, [](std::string_view phase) {
                if (phase == "directory-sync")
                    throw Error("Injected initiating directory-sync failure");
                if (phase == "rollback")
                    throw Error("Injected secondary rollback failure");
            });
            try {
                store.apply({}, true, [](Mapping) {});
            } catch (const Error &e) {
                uncertain = e.uncertain;
                CHECK(std::string(e.what()).find("Injected initiating directory-sync failure") !=
                      std::string::npos);
                CHECK(std::string(e.what()).find("Injected secondary rollback failure") != std::string::npos);
            }
            CHECK(uncertain); // A complete new file is not proof that the failed transaction rolled back.
            CHECK(read_profile(f.profile) == f.bytes);
        }
        {
            Fixture f(argv[1]);
            Store store(f.dir, f.profile);
            write(f.profile, f.bytes + " ");
            rejects([&] { store.apply({}, true, [](Mapping) {}); });
            CHECK(!std::filesystem::exists(f.dir / "x1-role-map.v1.json"));
        }
        {
            Fixture f(argv[1]);
            CHECK(::chmod(f.dir.c_str(), 0777) == 0);
            rejects([&] { Store unsafe(f.dir, f.profile); });
            CHECK(::chmod(f.dir.c_str(), 0700) == 0);
            std::filesystem::create_directory_symlink(f.dir, f.root / "alias");
            rejects([&] { Store symlink(f.root / "alias", f.profile); });
            std::filesystem::create_symlink(f.profile, f.dir / "x1-role-map.v1.json");
            rejects([&] { Store linked(f.dir, f.profile); });
            CHECK(read_profile(f.profile) == f.bytes);
        }
        {
            Fixture f(argv[1]);
            write(f.profile, f.bytes + std::string(65537, ' '));
            rejects([&] { Store oversized_base(f.dir, f.profile); });
        }
        {
            Fixture f(argv[1]);
            {
                Store store(f.dir, f.profile);
                store.apply({}, true, [](Mapping) {});
            }
            write(f.dir / "x1-role-map.v1.json", std::string(65537, 'x'));
            rejects([&] { Store oversized_override(f.dir, f.profile); });
        }
        for (int scenario = 0; scenario < 5; ++scenario) {
            Fixture f(argv[1]);
            auto base = Json::parse(f.bytes);
            if (scenario == 0)
                base["format_version"] = 1;
            if (scenario == 1)
                base["calibration_id"] = "stale.calibration";
            if (scenario == 2)
                base["measurement_cameras"]["left"].erase("bus_identity");
            if (scenario == 3)
                base["measurement_cameras"]["right"] = base["measurement_cameras"]["left"];
            if (scenario == 4)
                base["measurement_cameras"]["left"]["route"] = {"wrong-sensor", "capture"};
            write(f.profile, base.dump());
            rejects([&] { Store invalid(f.dir, f.profile); });
        }
        {
            Fixture f(argv[1]);
            {
                Store store(f.dir, f.profile);
                store.apply({}, true, [](Mapping) {});
            }
            auto path = f.dir / "x1-role-map.v1.json";
            auto bytes = read_profile(path);
            write(path, "{\"format_version\":99," + bytes.substr(1));
            rejects([&] { Store duplicate(f.dir, f.profile); });
        }
        std::cout << "X1 role identity, restart, confinement and atomic failure controls passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
