#include <mantis/client.hpp>
#include <iostream>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
int main(int argc, char **argv) {
    try {
        CHECK(argc == 4);
        mantis::client::Client client;
        auto entries = client.calibrations(); CHECK(!entries.empty());
        auto info = client.calibration_info(argv[1]); CHECK(info.has_target_info());
        CHECK(!client.active_calibration("cpp.offline"));
        mantis::wire::v1::CalibrationTargetSpecification spec;
        spec.set_squares_x(8); spec.set_squares_y(6); spec.set_nominal_square_size_mm(40); spec.mutable_checkerboard();
        auto target = client.create_calibration_target(spec); CHECK(target.reference().revision() == 1);
        auto next = client.create_calibration_target(spec, target.reference().id()); CHECK(next.reference().revision() == 2);
        auto job = client.build_calibration_dataset(target.artifact().id(), {argv[2], argv[3]}, {"left", "right"}, 3);
        CHECK(!job.empty()); auto result = client.wait(job, std::chrono::seconds(90)); CHECK(!result.result_artifact().empty());
        auto dataset = client.calibration_info(result.result_artifact()); CHECK(dataset.has_dataset_info());
        CHECK(dataset.dataset_info().target().id() == target.artifact().id());
        CHECK(dataset.dataset_info().total_records() == 4);
        CHECK(!client.calibration_info(target.artifact().id()).target_info().target().measurement().has_provenance());
        std::cout << "C++ calibration SDK against real mantisd passed\n"; return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
