#ifndef FOO__VISIBILITY_CONTROL_H_
#define FOO__VISIBILITY_CONTROL_H_
#if defined _WIN32 || defined __CYGWIN__
  #define FOO_EXPORT __declspec(dllexport)
  #define FOO_IMPORT __declspec(dllimport)
#else
  #define FOO_EXPORT __attribute__ ((visibility("default")))
  #define FOO_IMPORT
#endif
#endif
