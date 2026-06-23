/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//
// Docs: https://fburl.com/fbcref_scopeguard
//

/**
 * ScopeGuard is a general implementation of the "Initialization is
 * Resource Acquisition" idiom.  It guarantees that a function
 * is executed upon leaving the current scope.
 *
 * @file ScopeGuard.h
 * @refcode folly/docs/examples/folly/ScopeGuard.cpp
 */
/*
 * The makeGuard() function is used to create a new ScopeGuard object.
 * It can be instantiated with a lambda function, a std::function<void()>,
 * a functor, or a void(*)() function pointer.
 *
 *
 * Usage example: Add a friend to memory if and only if it is also added
 * to the db.
 *
 * void User::addFriend(User& newFriend) {
 *   // add the friend to memory
 *   friends_.push_back(&newFriend);
 *
 *   // If the db insertion that follows fails, we should
 *   // remove it from memory.
 *   auto guard = makeGuard([&] { friends_.pop_back(); });
 *
 *   // this will throw an exception upon error, which
 *   // makes the ScopeGuard execute UserCont::pop_back()
 *   // once the Guard's destructor is called.
 *   db_->addFriend(GetName(), newFriend.GetName());
 *
 *   // an exception was not thrown, so don't execute
 *   // the Guard.
 *   guard.dismiss();
 * }
 *
 * It is also possible to create a guard in dismissed state with
 * makeDismissedGuard(), and later rehire it with the rehire()
 * method.
 *
 * makeDismissedGuard() is not just syntactic sugar for creating a guard and
 * immediately dismissing it, but it has a subtle behavior difference if
 * move-construction of the passed function can throw: if it does, the function
 * will be called by makeGuard(), but not by makeDismissedGuard().
 *
 * Examine ScopeGuardTest.cpp for some more sample usage.
 *
 * Stolen from:
 *   Andrei's and Petru Marginean's CUJ article:
 *     http://drdobbs.com/184403758
 *   and the loki library:
 *     http://loki-lib.sourceforge.net/index.php?n=Idioms.ScopeGuardPointer
 *   and triendl.kj article:
 *     http://www.codeproject.com/KB/cpp/scope_guard.aspx
 */
#pragma once

#include <cstddef>
#include <cstdlib>
#include <functional>
#include <type_traits>
#include <utility>

namespace folly {

namespace detail {

struct ScopeGuardDismissed {};

class ScopeGuardImplBase {
public:
  void dismiss() noexcept { dismissed_ = true; }
  void rehire() noexcept { dismissed_ = false; }

protected:
  ScopeGuardImplBase(bool dismissed = false) noexcept : dismissed_(dismissed) {}

  [[noreturn]] static void terminate() noexcept;
  static ScopeGuardImplBase makeEmptyScopeGuard() noexcept {
    return ScopeGuardImplBase{};
  }

  bool dismissed_;
};

template <typename FunctionType, bool InvokeNoexcept>
class ScopeGuardImpl : public ScopeGuardImplBase {
public:
  explicit ScopeGuardImpl(FunctionType &fn) noexcept(
      std::is_nothrow_copy_constructible_v<FunctionType>)
      : ScopeGuardImpl(
            std::as_const(fn),
            makeFailsafe(std::is_nothrow_copy_constructible<FunctionType>{},
                         &fn)) {}

  explicit ScopeGuardImpl(const FunctionType &fn) noexcept(
      std::is_nothrow_copy_constructible_v<FunctionType>)
      : ScopeGuardImpl(
            fn, makeFailsafe(std::is_nothrow_copy_constructible<FunctionType>{},
                             &fn)) {}

  explicit ScopeGuardImpl(FunctionType &&fn) noexcept(
      std::is_nothrow_move_constructible_v<FunctionType>)
      : ScopeGuardImpl(
            std::move_if_noexcept(fn),
            makeFailsafe(std::is_nothrow_move_constructible<FunctionType>{},
                         &fn)) {}

  explicit ScopeGuardImpl(FunctionType &&fn, ScopeGuardDismissed) noexcept(
      std::is_nothrow_move_constructible_v<FunctionType>)
      // No need for failsafe in this case, as the guard is dismissed.
      : ScopeGuardImplBase{true}, function_(std::forward<FunctionType>(fn)) {}

  ScopeGuardImpl(ScopeGuardImpl &&other) noexcept(
      std::is_nothrow_move_constructible_v<FunctionType>)
      : function_(std::move_if_noexcept(other.function_)) {
    // If the above line attempts a copy and the copy throws, other is
    // left owning the cleanup action and will execute it (or not) depending
    // on the value of other.dismissed_. The following lines only execute
    // if the move/copy succeeded, in which case *this assumes ownership of
    // the cleanup action and dismisses other.
    dismissed_ = std::exchange(other.dismissed_, true);
  }

  ~ScopeGuardImpl() noexcept(InvokeNoexcept) {
    if (!dismissed_) {
      execute();
    }
  }

private:
  static ScopeGuardImplBase makeFailsafe(std::true_type,
                                         const void *) noexcept {
    return makeEmptyScopeGuard();
  }

  template <typename Fn>
  static auto makeFailsafe(std::false_type, Fn *fn) noexcept
      -> ScopeGuardImpl<decltype(std::ref(*fn)), InvokeNoexcept> {
    return ScopeGuardImpl<decltype(std::ref(*fn)), InvokeNoexcept>{
        std::ref(*fn)};
  }

  template <typename Fn>
  explicit ScopeGuardImpl(Fn &&fn, ScopeGuardImplBase &&failsafe)
      : ScopeGuardImplBase{}, function_(std::forward<Fn>(fn)) {
    failsafe.dismiss();
  }

  void *operator new(std::size_t) = delete;

  void execute() noexcept(InvokeNoexcept) {
    if constexpr (InvokeNoexcept) {
      static_assert(std::is_same_v<void, decltype(function_())>);
      catch_exception(function_, &terminate);
    } else {
      function_();
    }
  }

  FunctionType function_;
};

template <typename F, bool INE>
using ScopeGuardImplDecay = ScopeGuardImpl<std::decay_t<F>, INE>;

} // namespace detail

/**
 * Create a scope guard.
 *
 * The returned object has methods .dismiss() and .rehire(), which will
 * deactivate/reactivate the calling of the function upon destruction.
 *
 * The return value of this function must be captured. Otherwise, since it is a
 * temporary, it will be destroyed immediately, thus calling the function.
 *
 *     auto guard = makeGuard(...); // good
 *
 *     makeGuard(...); // bad
 *
 * @param f  The function to execute upon the guard's destruction.
 * @refcode folly/docs/examples/folly/ScopeGuard2.cpp
 */
template <typename F>
[[nodiscard]] detail::ScopeGuardImplDecay<F, true> makeGuard(F &&f) noexcept(
    noexcept(detail::ScopeGuardImplDecay<F, true>(static_cast<F &&>(f)))) {
  return detail::ScopeGuardImplDecay<F, true>(static_cast<F &&>(f));
}

/**
 * Create a scope guard in the dismissed state.
 *
 * The guard can be enabled using .rehire().
 *
 * @see makeGuard
 * @refcode folly/docs/examples/folly/ScopeGuard2.cpp
 */
template <typename F>
[[nodiscard]] detail::ScopeGuardImplDecay<F, true>
makeDismissedGuard(F &&f) noexcept(noexcept(
    detail::ScopeGuardImplDecay<F, true>(static_cast<F &&>(f),
                                         detail::ScopeGuardDismissed{}))) {
  return detail::ScopeGuardImplDecay<F, true>(static_cast<F &&>(f),
                                              detail::ScopeGuardDismissed{});
}

} // namespace folly
