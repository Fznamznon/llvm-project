// Tests without serialization:
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-unknown -fsycl-is-device \
// RUN:   -ast-dump %s \
// RUN:   | FileCheck --match-full-lines %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-unknown -fsycl-is-host \
// RUN:   -ast-dump %s \
// RUN:   | FileCheck --match-full-lines %s

// Thes test validates the AST body produced for functions declared with the
// sycl_kernel_entry_point attribute in case an argument of such function
// contains an object that requires decomposition.

// CHECK: TranslationUnitDecl {{.*}}

// A unique kernel name type is required for each declared kernel entry point.
template<int> struct KN;

template<typename KernelName, typename... Ts>
auto sycl_handle_special_kernel_parameters(Ts...) {
  return [](auto ...Args){ return; };
}

template<typename... Ts>
struct type_list {};

template <typename KernelName, typename... Ts>
auto sycl_kernel_launch(const char *, Ts...) {
  return [](auto ...Args){ return type_list<Ts...>{}; };
}

struct [[clang::sycl_special_kernel_parameter]] EmptySpecial {
};

template<typename T>
struct Wrapper {
 T data;
 int *data1;
};

template <typename KN, typename KT>
[[clang::sycl_kernel_entry_point(KN)]] void k(KT Kernel) {
  Kernel();
}

void case1() {
    Wrapper<EmptySpecial> KernelArg;
    k<KN<0>>([KernelArg](){});
}
