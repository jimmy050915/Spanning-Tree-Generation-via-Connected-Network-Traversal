#pragma once

#include "application/ApplicationError.h"

#include <stdexcept>
#include <utility>
#include <variant>

namespace novel::application {

template <typename T>
class Result final {
public:
    static Result success(T value) {
        return Result(std::move(value));
    }

    static Result failure(ApplicationError error) {
        return Result(std::move(error));
    }

    bool hasValue() const noexcept {
        return std::holds_alternative<T>(storage_);
    }

    explicit operator bool() const noexcept {
        return hasValue();
    }

    T& value() & {
        requireValue();
        return std::get<T>(storage_);
    }

    const T& value() const& {
        requireValue();
        return std::get<T>(storage_);
    }

    T&& value() && {
        requireValue();
        return std::get<T>(std::move(storage_));
    }

    const ApplicationError& error() const& {
        requireError();
        return std::get<ApplicationError>(storage_);
    }

    ApplicationError&& error() && {
        requireError();
        return std::get<ApplicationError>(std::move(storage_));
    }

private:
    explicit Result(T value) : storage_(std::move(value)) {}
    explicit Result(ApplicationError error) : storage_(std::move(error)) {}

    void requireValue() const {
        if (!hasValue()) {
            throw std::logic_error("attempted to read the value of a failed Result");
        }
    }

    void requireError() const {
        if (hasValue()) {
            throw std::logic_error("attempted to read the error of a successful Result");
        }
    }

    std::variant<T, ApplicationError> storage_;
};

template <>
class Result<void> final {
public:
    static Result success() {
        return Result();
    }

    static Result failure(ApplicationError error) {
        return Result(std::move(error));
    }

    bool hasValue() const noexcept {
        return !hasError_;
    }

    explicit operator bool() const noexcept {
        return hasValue();
    }

    void value() const {
        if (hasError_) {
            throw std::logic_error("attempted to read the value of a failed Result");
        }
    }

    const ApplicationError& error() const& {
        if (!hasError_) {
            throw std::logic_error("attempted to read the error of a successful Result");
        }
        return error_;
    }

private:
    Result() = default;
    explicit Result(ApplicationError error)
        : hasError_(true), error_(std::move(error)) {}

    bool hasError_{};
    ApplicationError error_{};
};

}  // namespace novel::application
