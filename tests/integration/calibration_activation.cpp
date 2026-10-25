#include "../fixtures/calibration_api.hpp"
#include <mantis/services.hpp>
#include <mantis/capture_calibration.hpp>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>
using namespace mantis;
namespace ca = calibration::artifacts;
#define CHECK(...) do { if (!(__VA_ARGS__)) throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #__VA_ARGS__); } while (false)
Id complete(services::Runtime &r, Id id) {
    for (unsigned i=0; i<18000; ++i) {
        for (const auto &j:r.jobs()) if (j.id==id) {
            if (j.state==jobs::State::completed) { CHECK(j.result); return j.result->id; }
            if (j.state==jobs::State::failed || j.state==jobs::State::cancelled) throw std::runtime_error(j.diagnostics);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("Job timeout");
}
void exercise(const std::filesystem::path &root, const std::filesystem::path &plugins, const char *fixture) {
    nlohmann::json profile; std::ifstream(fixture) >> profile;
    profile["mode"]={{"width",1280},{"height",960},{"fourcc","GREY"},{"fps",5}};
    profile["max_v4l2_delta_ns"]=110000000; profile["stall_timeout_ms"]=1000;
    auto path=root/"profile.json"; std::ofstream(path)<<profile;
    setenv("MANTIS_X1_PROFILE",path.c_str(),1); setenv("MANTIS_X1_FAKE","normal",1);
    services::Runtime r({plugins,plugins.parent_path()/"bin/mantis-plugin-host",root/"project",{}, {"org.mantis.x1"}});
    auto descriptors=r.devices(); auto parent=std::find_if(descriptors.begin(),descriptors.end(),[](const auto &d) {return d.plugin_id=="org.mantis.x1" && d.parent.value.empty();});
    CHECK(parent!=descriptors.end()); CHECK(parent->children.size()==2);
    auto components=m6fixture::checked(services::discovered_activation_components(*parent,descriptors));
    CHECK(components.size()==2);
    // Exercise the exact activation parser seam with present malformed discovery, not an offline fallback.
    for (const auto *field:{"role","identity","width","height"}) {
        auto bad=descriptors; auto child=std::find_if(bad.begin(),bad.end(),[&](const auto &d){return d.id==parent->children[0];});
        child->metadata.erase(field);
        auto result=services::discovered_activation_components(*parent,bad); CHECK(!result && result.error().code==Status::incompatible);
    }
    for (const auto *value:{"","0","-1","1280px","4294967296"}) {
        auto bad=descriptors; auto child=std::find_if(bad.begin(),bad.end(),[&](const auto &d){return d.id==parent->children[0];});
        child->metadata["width"]=value;
        auto result=services::discovered_activation_components(*parent,bad); CHECK(!result && result.error().code==Status::incompatible);
    }
    auto store=r.project_store(); auto f=m6fixture::seed(*store);
    // Seed's ordinary cameras have different physical IDs: discovered activation must reject them.
    auto make_rig=[&](const Id &dataset) {
        auto left=complete(r,r.solve_camera_calibration({dataset,"left",3,{}}));
        auto right=complete(r,r.solve_camera_calibration({dataset,"right",3,{}}));
        return complete(r,r.solve_rig_calibration({dataset,left,right,3,{{"m6.rig"},"M6 rig"},{}}));
    };
    auto wrong=make_rig(f.dataset.descriptor.id);
    try {r.activate_calibration(parent->id,wrong); throw std::runtime_error("Incompatible discovered identity accepted");}
    catch (const Failure &e) {CHECK(e.error.code==Status::incompatible);}
    CHECK(!r.active_calibration(parent->id));
    std::vector<calibration::DatasetCamera> cameras;
    for (const auto &c:components) cameras.push_back({c.role,c.camera_id,c.image_width,c.image_height,{{"optical."+c.role},"Optical frame"}});
    std::sort(cameras.begin(),cameras.end(),[](const auto &a,const auto &b){return a.role<b.role;});
    auto dataset=m6fixture::checked(ca::create_calibration_dataset(*store,m5fixture::fixture(f.charuco.value.target,f.raw_ids,cameras),f.charuco.reference()));
    auto rig=make_rig(dataset.descriptor.id); r.activate_calibration(parent->id,rig);
    auto active=r.active_calibration(parent->id); CHECK(active && active->artifact.id==rig);
    auto n=r.calibrations().size(); r.activate_calibration(parent->id,rig); CHECK(r.calibrations().size()==n);
    auto capture=r.start_capture({parent->id});
    for(unsigned i=0;i<1000;++i) {
        auto all=r.captures(); CHECK(all.size()==1 && all[0].error.empty());
        if(all[0].committed>=2)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    auto stopped=r.stop_capture(capture.id); CHECK(stopped.error.empty());
    complete(r,stopped.finalization_job);
    auto raw=store->get(stopped.raw_artifact); CHECK(raw.provenance.calibration.id==active->reference.id);
    CHECK(raw.provenance.calibration.revision==active->reference.revision);
    CHECK(raw.provenance.parameters.at("active_rig_artifact_id")==rig.value);
    auto packet=store->packet(raw.id); CHECK(packet->header.calibration.id==active->reference.id);
    for(const auto &image:packet->frames) CHECK(image->header.calibration.revision==active->reference.revision);
    // Current geometry changes: same logical device identity, incompatible dimensions.
    r.clear_calibration(parent->id); CHECK(!r.active_calibration(parent->id));
    profile["mode"]["width"]=64; profile["mode"]["height"]=48; std::ofstream(path)<<profile;
    try {r.activate_calibration(parent->id,rig); throw std::runtime_error("Incompatible discovered dimensions accepted");}
    catch (const Failure &e) {CHECK(e.error.code==Status::incompatible);}
    CHECK(!r.active_calibration(parent->id));
    CHECK(store->get(raw.id).provenance.calibration.revision==active->reference.revision);
}
int main(int argc,char **argv) {
    auto root=std::filesystem::temp_directory_path()/Id::random().value;
    try {
        CHECK(argc==3); std::filesystem::create_directories(root);
        exercise(root,argv[1],argv[2]); std::filesystem::remove_all(root);
        std::cout<<"Calibration service discovered activation, strict parsing and M5 capture snapshot passed\n"; return 0;
    } catch(const std::exception &e) {std::filesystem::remove_all(root); std::cerr<<e.what()<<'\n';return 1;}
}
