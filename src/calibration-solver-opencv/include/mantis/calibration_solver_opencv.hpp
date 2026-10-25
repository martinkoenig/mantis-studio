#pragma once
#include <mantis/calibration_solve.hpp>
#include <string_view>
namespace mantis::calibration {
// Synchronous pure-dataset API. No detector, storage, cv::Mat or pixel ownership.
std::string_view solver_opencv_version();
Result<CameraCalibrationSolution> solve_camera_intrinsics(const CalibrationDataset &, std::string_view role,
                                                          const MonoSolveConfig &);
Result<std::vector<CameraCalibrationSolution>> solve_camera_intrinsics(const CalibrationDataset &,
                                                                       const MonoSolveConfig &);
Result<StereoCalibrationSolution> solve_stereo_rig(const CalibrationDataset &,
                                                   const CameraCalibrationSolution &left,
                                                   const CameraCalibrationSolution &right,
                                                   const StereoSolveConfig &);
// Fixed-model evidence helpers, also used by the staged solvers.
Result<MonoViewEvidence> evaluate_heldout_target(const TargetObservation &, const PinholeBrown5 &);
Result<StereoPairEvidence> evaluate_epipolar_pair(const FrameSetKey &, std::span<const CommonTargetPoint>,
                                                  const StereoModel &, const PinholeBrown5 &left,
                                                  const PinholeBrown5 &right);
} // namespace mantis::calibration
