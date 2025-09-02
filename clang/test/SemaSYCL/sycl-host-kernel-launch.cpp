// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++17 -fsyntax-only -fsycl-is-host -fcxx-exceptions -verify %s
// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++20 -fsyntax-only -fsycl-is-host -fcxx-exceptions -verify %s

// A unique kernel name type is required for each declared kernel entry point.
template<int, int = 0> struct KN;

[[clang::sycl_kernel_entry_point(KN<1>)]]
void nolauncher() {} 
// expected-error@-1 {{unable to find suitable 'sycl_enqueue_kernel_launch' function for host code synthesis}}
// expected-note@-2 {{define 'sycl_enqueue_kernel_launch' function template to fix}}

void sycl_enqueue_kernel_launch(const char *, int arg);
// expected-note@-1 {{declared as a non-template here}}

[[clang::sycl_kernel_entry_point(KN<2>)]]
void nontemplatel() {}
// expected-error@-1 {{unable to find suitable 'sycl_enqueue_kernel_launch' function for host code synthesis}}
// expected-note@-2 {{define 'sycl_enqueue_kernel_launch' function template to fix}}
// expected-error@-3 {{'sycl_enqueue_kernel_launch' following the 'template' keyword does not refer to a template}}

template <typename KernName>
void sycl_enqueue_kernel_launch(const char *, int arg);
// expected-note@-1 {{candidate function template not viable: requires 2 arguments, but 1 was provided}}
// expected-note@-2 {{candidate function template not viable: no known conversion from 'Kern' to 'int' for 2nd argument}}

[[clang::sycl_kernel_entry_point(KN<3>)]]
void notenoughargs() {}
// expected-error@-1 {{no matching function for call to 'sycl_enqueue_kernel_launch'}}
// FIXME: Should this also say "no suitable function for host code synthesis"?


template <typename KernName>
void sycl_enqueue_kernel_launch(const char *, bool arg = 1);
// expected-note@-1 {{candidate function template not viable: no known conversion from 'Kern' to 'bool' for 2nd argument}}

[[clang::sycl_kernel_entry_point(KN<4>)]]
void enoughargs() {}

namespace boop {
template <typename KernName, typename KernelObj>
void sycl_enqueue_kernel_launch(const char *, KernelObj);

template <typename KernName, typename KernelObj>
[[clang::sycl_kernel_entry_point(KernName)]]
void iboop(KernelObj Kernel) {
  Kernel();
}
}

template <typename KernName, typename KernelObj>
[[clang::sycl_kernel_entry_point(KernName)]]
void idontboop(KernelObj Kernel) {
  Kernel();
}
// expected-error@-3 {{no matching function for call to 'sycl_enqueue_kernel_launch'}}

struct Kern {
  int a;
  int *b;
  Kern(int _a, int* _b) : a(_a), b(_b) {}
  void operator()(){ *b = a;}
};

void foo() {
  int *a;
  Kern b(1, a);
  idontboop<KN<6>>(b);
  // expected-note@-1 {{in instantiation of function template specialization 'idontboop<KN<6>, Kern>' requested here}}
  boop::iboop<KN<7>>(b);
}

