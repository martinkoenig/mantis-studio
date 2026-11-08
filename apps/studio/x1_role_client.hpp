#pragma once
#include <mantis/client.hpp>
#include <memory>

// Presentation-only seam. Production uses the authenticated public Client;
// no runtime, profile, filesystem or calibration implementation is accessible.
class X1RoleClient {
  public:
    virtual ~X1RoleClient() = default;
    virtual mantis::wire::v1::Response snapshot() const = 0;
    virtual mantis::wire::v1::X1RoleInfo query() const = 0;
    virtual std::optional<mantis::wire::v1::ActiveCalibrationBinding> active(const std::string &) const = 0;
    virtual std::string validate(const mantis::wire::v1::X1RoleRequest &) const = 0;
    virtual std::string apply(const mantis::wire::v1::X1RoleRequest &) const = 0;
    virtual void cancel(const std::string &) const = 0;
};
class PublicX1RoleClient final : public X1RoleClient {
    mantis::client::Client client_;

  public:
    explicit PublicX1RoleClient(mantis::client::Client client = mantis::client::Client{})
        : client_(std::move(client)) {}
    mantis::wire::v1::Response snapshot() const override {
        return client_.snapshot();
    }
    mantis::wire::v1::X1RoleInfo query() const override {
        return client_.x1_roles();
    }
    std::optional<mantis::wire::v1::ActiveCalibrationBinding>
    active(const std::string &parent) const override {
        return client_.active_calibration(parent);
    }
    std::string validate(const mantis::wire::v1::X1RoleRequest &request) const override {
        return client_.validate_x1_roles(request);
    }
    std::string apply(const mantis::wire::v1::X1RoleRequest &request) const override {
        return client_.apply_x1_roles(request);
    }
    void cancel(const std::string &id) const override {
        mantis::wire::v1::Request request;
        request.mutable_job_cancel()->set_id(id);
        (void)client_.call(request);
    }
};
