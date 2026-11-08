#include <iostream>
#include <mantis/client.hpp>
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Expected desired mapping");
        mantis::client::Client client;
        auto state = client.x1_roles();
        const bool swapped = std::string(argv[1]) == "swapped";
        if (!state.supported() || state.blocked() || state.swapped() != swapped || state.cameras_size() != 2)
            throw std::runtime_error("C++ role query disagrees with requested fixture state");
        mantis::wire::v1::X1RoleRequest request;
        request.set_schema_version(1);
        request.set_expected_generation(state.generation());
        request.set_discovery_token(state.discovery_token());
        request.set_parent_id(state.parent_id());
        request.set_swapped(swapped);
        client.wait(client.validate_x1_roles(request));
        std::cout << "C++ public role validation Job passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
