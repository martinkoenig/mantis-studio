#pragma once
#include <mantis/artifact_store.hpp>
#include <mantis/device_api.hpp>
namespace mantis::device {
// Live and recorded sources use the same semantic contract; no algorithm parses storage.
std::unique_ptr<ImageStream> recorded_source(std::shared_ptr<const artifact::Store>, Id, bool real_time);
} // namespace mantis::device
