// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++17 -fsyntax-only -fsycl-is-host -fcxx-exceptions -verify %s
// RUN: %clang_cc1 -triple spirv64-unknown-unknown -std=c++17 -fsyntax-only -fsycl-is-device -verify %s
// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++20 -fsyntax-only -fsycl-is-host -fcxx-exceptions -verify %s
// RUN: %clang_cc1 -triple spirv64-unknown-unknown -std=c++20 -fsyntax-only -fsycl-is-device -verify %s
// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++23 -fsyntax-only -fsycl-is-host -fcxx-exceptions -verify %s
// RUN: %clang_cc1 -triple spirv64-unknown-unknown -std=c++23 -fsyntax-only -fsycl-is-device -verify %s

// Test overload resolution for implicit calls to
// sycl_handle_special_kernel_parameters<KN>(...) synthesized for functions
// declared with the sycl_kernel_entry_point attribute when kernel parameters
// contain types marked with sycl_special_kernel_parameter.

struct [[clang::sycl_special_kernel_parameter]] SpecialType {
  int data;
};

template<typename T>
struct Wrapper {
 T data;
};

template<typename T>
auto set_kernel_arg(const T &t) {
  return t;
}

template<typename... Ts>
struct type_list {};

template <typename KernelName, typename... Ts>
auto sycl_kernel_launch(const char *, Ts...) {
    return [&](auto&&... extra_host_args) {
      return type_list<decltype(set_kernel_arg(extra_host_args))...>{};
  };
}


////////////////////////////////////////////////////////////////////////////////
// Valid declarations.
////////////////////////////////////////////////////////////////////////////////

// sycl_handle_special_kernel_parameters as function template at namespace scope.
namespace ok1 {
  template<int> struct KN;
  template<typename KN, typename... Ts>
  auto sycl_handle_special_kernel_parameters(Ts...) {
    return [](auto ...Args){ return; };
  }
  template <typename KNT, typename KT>
  [[clang::sycl_kernel_entry_point(KNT)]] void skep(KT Kernel) {
    Kernel();
  }
  void test() {
    Wrapper<SpecialType> KernelArg;
    skep<KN<1>>([KernelArg](){});
  }
}

// sycl_handle_special_kernel_parameters with overload set.
namespace ok2 {
  template<int> struct KN;
  template<typename KN>
  auto sycl_handle_special_kernel_parameters() {
    return [](auto ...Args){ return; };
  }
  template<typename KN, typename... Ts>
  auto sycl_handle_special_kernel_parameters(Ts...) {
    return [](auto ...Args){ return; };
  }
  template <typename KNT, typename KT>
  [[clang::sycl_kernel_entry_point(KNT)]] void skep(KT Kernel) {
    Kernel();
  }
  void test() {
    Wrapper<SpecialType> KernelArg;
    skep<KN<2>>([KernelArg](){});
  }
}


////////////////////////////////////////////////////////////////////////////////
// Invalid declarations.
////////////////////////////////////////////////////////////////////////////////

// Undeclared sycl_handle_special_kernel_parameters from function template.
namespace bad1 {
  template<int> struct BADKN;
  struct KT {
    Wrapper<SpecialType> w;
    void operator()() const;
  };
  // expected-error@+5 {{use of undeclared identifier 'sycl_handle_special_kernel_parameters'}}
  // expected-note@+3 {{this indicates a problem with the SYCL runtime header files; please consider reporting this to your SYCL runtime provider}}
  // expected-note@+2 {{in implicit call to 'sycl_handle_special_kernel_parameters' with template argument 'bad1::BADKN<1>' and function arguments (lvalue of type 'SpecialType') required here}}
  template<typename KNT, typename KTT>
  [[clang::sycl_kernel_entry_point(KNT)]]
  void skep(KTT Kernel) {
    Kernel();
  }
  // expected-note@+1 {{in instantiation of function template specialization 'bad1::skep<bad1::BADKN<1>, bad1::KT>' requested here}}
  template void skep<BADKN<1>>(KT);
}

// No matching function for call to sycl_handle_special_kernel_parameters;
// not a template.
namespace bad2 {
  template<int> struct BADKN;
  // expected-note@+1 {{declared as a non-template here}}
  void sycl_handle_special_kernel_parameters(SpecialType);
  // expected-error@+4 {{'sycl_handle_special_kernel_parameters' does not refer to a template}}
  // expected-note@+2 {{this indicates a problem with the SYCL runtime header files; please consider reporting this to your SYCL runtime provider}}
  // expected-note@+1 {{in implicit call to 'sycl_handle_special_kernel_parameters' with template argument 'BADKN<2>' required here}}
  [[clang::sycl_kernel_entry_point(BADKN<2>)]]
  void skep(Wrapper<SpecialType> k) {
    (void)k;
  }
}

// No matching function for call to sycl_handle_special_kernel_parameters;
// mismatched function parameter type.
namespace bad3 {
  template<int> struct BADKN;
  struct KT {
    Wrapper<SpecialType> w;
    void operator()() const;
  };
  // expected-note@+2 {{candidate function template not viable: no known conversion from 'SpecialType' to 'int' for 1st argument}}
  template<typename KN, typename... Ts>
  auto sycl_handle_special_kernel_parameters(int) {
    return [](auto ...Args){ return; };
  }
  // expected-error@+5 {{no matching function for call to 'sycl_handle_special_kernel_parameters'}}
  // expected-note@+3 {{this indicates a problem with the SYCL runtime header files; please consider reporting this to your SYCL runtime provider}}
  // expected-note@+2 {{in implicit call to 'sycl_handle_special_kernel_parameters' with template argument 'bad3::BADKN<3>' and function arguments (lvalue of type 'SpecialType') required here}}
  template<typename KNT, typename KTT>
  [[clang::sycl_kernel_entry_point(KNT)]]
  void skep(KTT Kernel) {
    Kernel();
  }
  // expected-note@+1 {{in instantiation of function template specialization 'bad3::skep<bad3::BADKN<3>, bad3::KT>' requested here}}
  template void skep<BADKN<3>>(KT);
}

// No matching function for call to sycl_handle_special_kernel_parameters;
// mismatched template parameter kind.
namespace bad4 {
  template<int> struct BADKN;
  // expected-note@+2 {{candidate template ignored: template argument for non-type template parameter must be an expression}}
  template<int, typename... Ts>
  auto sycl_handle_special_kernel_parameters(Ts...) {
    return [](auto ...Args){ return; };
  }
  // expected-error@+4 {{no matching function for call to 'sycl_handle_special_kernel_parameters'}}
  // expected-note@+2 {{this indicates a problem with the SYCL runtime header files; please consider reporting this to your SYCL runtime provider}}
  // expected-note@+1 {{in implicit call to 'sycl_handle_special_kernel_parameters' with template argument 'BADKN<4>' and function arguments (lvalue of type 'SpecialType') required here}}
  [[clang::sycl_kernel_entry_point(BADKN<4>)]]
  void skep(Wrapper<SpecialType> k) {
    (void)k;
  }
}

// sycl_handle_special_kernel_parameters declared after use and not found by ADL.
namespace bad5 {
  template<int> struct BADKN;
  struct KT {
    Wrapper<SpecialType> w;
    void operator()() const;
  };
  // expected-error@+5 {{call to function 'sycl_handle_special_kernel_parameters' that is neither visible in the template definition nor found by argument-dependent lookup}}
  // expected-note@+3 {{this indicates a problem with the SYCL runtime header files; please consider reporting this to your SYCL runtime provider}}
  // expected-note@+2 {{in implicit call to 'sycl_handle_special_kernel_parameters' with template argument 'bad5::BADKN<5>' and function arguments (lvalue of type 'SpecialType') required here}}
  template<typename KNT, typename KTT>
  [[clang::sycl_kernel_entry_point(KNT)]]
  void skep(KTT Kernel) {
    Kernel();
  }
  // expected-note@+2 {{'sycl_handle_special_kernel_parameters' should be declared prior to the call site or in the global namespace}}
  template<typename KN, typename... Ts>
  auto sycl_handle_special_kernel_parameters(Ts...) {
    return [](auto ...Args){ return; };
  }
  // expected-note@+1 {{in instantiation of function template specialization 'bad5::skep<bad5::BADKN<5>, bad5::KT>' requested here}}
  template void skep<BADKN<5>>(KT);
}

namespace bad6 {
// Check that if sycl_handle_special_kernel_parameters doesn't return a callable
// object, and error is emitted.
template<int> struct BADKN;
template<typename KN, typename... Ts>
auto sycl_handle_special_kernel_parameters(Ts...) {
  // expected-note@+2 {{conversion candidate of type 'void (*)(int)'}}
  // expected-note@+1 {{candidate function not viable: no known conversion from 'SpecialType' to 'int' for 1st argument}}
  return [](int){ return; };
}
// expected-error@+4 {{no matching function for call to object of type '(lambda at}}
// expected-note@+3 {{this indicates a problem with the SYCL runtime header files; please consider reporting this to your SYCL runtime provider}}
// expected-note@+2 {{in implicit call to callable object returned by 'sycl_handle_special_kernel_parameters' with arguments (lvalue of type 'SpecialType') required here}}
template <typename KNT, typename KT>
[[clang::sycl_kernel_entry_point(KNT)]] void skep(KT Kernel) {
  Kernel();
}
void test() {
  Wrapper<SpecialType> KernelArg;
  // expected-note@+1 {{in instantiation of function template specialization 'bad6::skep<bad6::BADKN<6>, (lambda at}}
  skep<BADKN<6>>([KernelArg](){});
}
}

