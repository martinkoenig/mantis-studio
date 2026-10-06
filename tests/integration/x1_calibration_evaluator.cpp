// Small, generated, fixture-only projects. Never real acquisition evidence.
#define main validation_tool_main
#include "../../tools/x1_calibration_validation.cpp"
#undef main
#include "../fixtures/calibration_api.hpp"
#include <filesystem>
int main(int argc, char **argv) {
    try {
        require(argc==2,"Fixture output directory required");
        std::filesystem::path root=argv[1]; std::filesystem::create_directories(root);
        auto project=root/"Fixture.mantis";
        Json sessions=Json::array();
        {
            artifact::Store store(project);
            auto target=get(ca::create_calibration_target(store,m6fixture::physical_target()));
            for (auto name : {"a","b","c"}) {
                std::vector<Id> raws{m6fixture::raw(store,false),m6fixture::raw(store,false)};
                std::sort(raws.begin(),raws.end());
                auto observations=m5fixture::fixture(target.value.target,raws);
                if (std::string(name)=="b") {
                    // A different known camera model: affine sensor-coordinate changes
                    // map exactly to changed focal/principal point parameters.
                    for(auto &record:observations.records) if(record.observation) {
                        for(auto &pixel:record.observation->image_points_px) {
                            pixel.x_px=638+(pixel.x_px-638)*1.08;
                            pixel.y_px=482+(pixel.y_px-482)*.94+(record.key.camera_role=="right"?12:0);
                        }
                        record.diversity=get(describe_diversity(observations.target,*record.observation));
                    }
                }
                auto dataset=get(ca::create_calibration_dataset(store,observations,target.reference()));
                auto left=get(ca::create_camera_calibration(store,get(solve_camera_intrinsics(dataset.value.dataset,"left",{1,1,1,4})),dataset.reference(),target.reference(),{std::string(solver_opencv_version())}));
                auto right=get(ca::create_camera_calibration(store,get(solve_camera_intrinsics(dataset.value.dataset,"right",{1,1,1,4})),dataset.reference(),target.reference(),{std::string(solver_opencv_version())}));
                StereoSolveConfig config{1,1,1,"left","right",4,{{"validation.rig"},"Validation rig"}};
                auto rig=get(ca::create_rig_calibration(store,get(solve_stereo_rig(dataset.value.dataset,left.value.solution,right.value.solution,config)),dataset.reference(),target.reference(),left.reference(),right.reference(),{std::string(solver_opencv_version())}));
                sessions.push_back({{"schema_version",1},{"evidence_mode","fixture"},{"name",name},{"project",project.string()},{"target",target.descriptor.id.value},{"dataset",dataset.descriptor.id.value},{"left",left.descriptor.id.value},{"right",right.descriptor.id.value},{"rig",rig.descriptor.id.value}});
            }
        }
        auto a=sessions[0], b=sessions[1];
        auto original=export_session(a);
        auto ab=evaluate({{"calibration_session",a},{"dataset_session",b}});
        auto ba=evaluate({{"calibration_session",b},{"dataset_session",a}});
        require(ab["calibration_session"]=="a" && ab["dataset_session"]=="b" && ba["calibration_session"]=="b" && ba["dataset_session"]=="a","Directional labels reversed");
        for (auto evidence : {ab,ba}) {
            require(evidence["parameters_fixed"]==true,"Fixed model flag missing");
            require(evidence["left"]["residuals"]["rms_px"].get<double>()>.1 && evidence["rig"]["residuals"]["rms_px"].get<double>()>1,"Independent changed geometry must not be refitted away");
        }
        auto ac=evaluate({{"calibration_session",a},{"dataset_session",sessions[2]}});
        for(auto role : {"left","right","rig"}) require(ac[role]["residuals"]["rms_px"].get<double>()<.01,"Known unchanged synthetic model must agree");
        require(export_session(a)==original,"Evaluation mutated/refitted artifacts");
        bool rejected=false;
        try { evaluate({{"calibration_session",a},{"dataset_session",a}}); } catch(const std::exception &) {rejected=true;}
        require(rejected,"Self evaluation must not be independent");
        b["left"]=a["left"]; rejected=false;
        try { export_session(b); } catch(const std::exception &) {rejected=true;}
        require(rejected,"Wrong camera lineage must fail");
        Json binding_request;
        {
            artifact::Store store(project);
            auto rig=get(ca::load_rig_calibration(store,{a.at("rig")}));
            auto descriptor=store.get({a.at("rig")});
            const std::string device="org.mantis.x1:camera.left:camera.right";
            artifact::Provenance provenance;
            provenance.producer="org.mantis.x1"; provenance.calibration=rig.revision;
            provenance.inputs={descriptor.id};
            provenance.parameters={{"source_device_calibration_id","source.device"},{"source_device_calibration_schema_version","1"},{"source_device_calibration_revision","7"},{"producer_plugin_version","fixture-only"},{"logical_device_id",device},{"active_calibration_id",rig.revision.id.value},{"active_calibration_schema_version","1"},{"active_calibration_revision",std::to_string(rig.revision.revision)},{"active_rig_artifact_id",descriptor.id.value},{"active_rig_artifact_hash",descriptor.hash.hex},{"active_rig_artifact_hash_algorithm",descriptor.hash.algorithm}};
            auto raw=store.begin({"org.mantis.RawCapture",2},provenance);
            data::Packet packet; packet.type=schema::frameset; packet.header.calibration=rig.revision;
            for(auto &camera:{rig.solution.left_camera,rig.solution.right_camera}) {
                data::Packet image; image.type=schema::image; image.header.calibration=rig.revision;
                image.header.metadata={{"role",camera.role},{"identity",camera.camera_id.value},{"fourcc","GREY"}};
                std::vector<std::byte> pixels(size_t(camera.image_width)*camera.image_height);
                image.attributes={{{"org.mantis.pixels",schema::ScalarType::u8,{camera.image_height,camera.image_width},{camera.image_width,1},"intensity"},memory::copy(pixels)}};
                packet.frames.push_back(data::publish(std::move(image)));
            }
            store.append(raw,packet); store.finalize(raw);
            store.activate_calibration({device},descriptor.id);
            binding_request={{"project",project.string()},{"rig",descriptor.id.value},{"raw",raw.value},{"logical_device_id",device},{"source_id","source.device"},{"source_revision","7"}};
        }
        const auto first_binding=binding(binding_request);
        {
            artifact::Store store(project); store.clear_active_calibration({binding_request.at("logical_device_id")});
        }
        require(binding(binding_request)==first_binding,"Historical exact binding changed after active state change");
        auto wrong=binding_request; wrong["source_revision"]="8"; rejected=false;
        try {binding(wrong);} catch(const std::exception &) {rejected=true;}
        require(rejected,"Source calibration provenance mismatch must fail");
        for(auto s:sessions) {
            s["evidence"]=export_session(s);
            std::ofstream(root/("session-"+s["name"].get<std::string>()+".json")) << s.dump(2) << '\n';
        }
        std::cout << "Fixture-only M4 fixed evaluation, lineage and directional evidence PASS\n";
        return 0;
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
