// A joinable thread with a caller-chosen stack size.
//
// On POSIX a std::thread is a pthread whose stack is whatever the platform
// defaults to - 512 KB on macOS - and there is no way to ask for more
// after the fact, so the thread is created directly with
// pthread_attr_setstacksize. On Windows every thread in the process
// already inherits the image stack reserve named in
// cmake/StandardProjectSettings.cmake, so a plain std::thread does.

#include <exception>
#include <functional>
#include <memory>
#include <system_error>
#include <utility>

#include "guchho/helpers.hpp"

#ifndef _WIN32

#include <pthread.h>

namespace guchho::helpers {

    struct Thread::Impl {
        pthread_t handle{};
        bool joinable = false;
    };

    Thread::Thread(size_t stack_size, std::function<void()> fn)
        : impl_(std::make_unique<Impl>()) {
        pthread_attr_t attr;
        int err = pthread_attr_init(&attr);
        if (err != 0) {
            impl_.reset();
            throw std::system_error(err, std::generic_category(), "pthread_attr_init");
        }

        err = pthread_attr_setstacksize(&attr, stack_size);
        if (err != 0) {
            pthread_attr_destroy(&attr);
            impl_.reset();
            throw std::system_error(err, std::generic_category(), "pthread_attr_setstacksize");
        }

        // The trampoline takes ownership of the callable so the new thread
        // cannot outlive it even if this Thread is never joined. The local
        // is named apart from the constructor's "fn" so the two never
        // shadow each other (-Wshadow).
        auto* owned = new std::function<void()>(std::move(fn));
        err = pthread_create(&impl_->handle, &attr, [](void* arg) -> void* {
            std::unique_ptr<std::function<void()>> held(static_cast<std::function<void()>*>(arg));
            (*held)();
            return nullptr;
        }, owned);
        pthread_attr_destroy(&attr);

        if (err != 0) {
            delete owned;
            impl_.reset();
            throw std::system_error(err, std::generic_category(), "pthread_create");
        }

        impl_->joinable = true;
    }

    Thread::Thread(Thread&& other) noexcept
        : impl_(std::move(other.impl_)) {}

    Thread& Thread::operator=(Thread&& other) noexcept {
        if (this != &other) {
            if (joinable()) {
                std::terminate();
            }
            impl_ = std::move(other.impl_);
        }
        return *this;
    }

    Thread::~Thread() {
        if (joinable()) {
            std::terminate();
        }
    }

    bool Thread::joinable() const {
        return impl_ != nullptr && impl_->joinable;
    }

    void Thread::join() {
        if (!joinable()) {
            throw std::system_error(std::make_error_code(std::errc::invalid_argument), "join");
        }
        int err = pthread_join(impl_->handle, nullptr);
        impl_->joinable = false;
        impl_.reset();
        if (err != 0) {
            throw std::system_error(err, std::generic_category(), "pthread_join");
        }
    }

}

#else

#include <thread>

namespace guchho::helpers {

    struct Thread::Impl {
        std::thread thread;
    };

    Thread::Thread(size_t stack_size, std::function<void()> fn)
        : impl_(std::make_unique<Impl>()) {
        (void) stack_size;
        try {
            impl_->thread = std::thread(std::move(fn));
        } catch (...) {
            impl_.reset();
            throw;
        }
    }

    Thread::Thread(Thread&& other) noexcept
        : impl_(std::move(other.impl_)) {}

    Thread& Thread::operator=(Thread&& other) noexcept {
        if (this != &other) {
            if (joinable()) {
                std::terminate();
            }
            impl_ = std::move(other.impl_);
        }
        return *this;
    }

    Thread::~Thread() {
        if (joinable()) {
            std::terminate();
        }
    }

    bool Thread::joinable() const {
        return impl_ != nullptr && impl_->thread.joinable();
    }

    void Thread::join() {
        if (!joinable()) {
            throw std::system_error(std::make_error_code(std::errc::invalid_argument), "join");
        }
        impl_->thread.join();
        impl_.reset();
    }

}

#endif
