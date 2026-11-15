#!/usr/bin/env bash
# Checks every shared library installed from this repo:
#  1. imports no socket/networking/IPC symbols,
#  2. links no DDS or networking libraries,
#  3. exports only symbols in the project's namespaces (verifies -fvisibility=hidden
#     plus visibility macros, which is what MSVC enforces with __declspec).
set -u
INSTALL="${1:-/ws/install}"

# Symbols an intra-process RMW has no reason to import.
FORBIDDEN_IMPORTS='^(socket|socketpair|connect|bind|listen|accept|accept4|getaddrinfo|freeaddrinfo|gethostbyname|gethostbyname_r|gethostbyaddr|getnameinfo|send|sendto|sendmsg|sendmmsg|recv|recvfrom|recvmsg|recvmmsg|setsockopt|getsockopt|getsockname|getpeername|shutdown|inet_pton|inet_ntop|inet_addr|inet_aton|inet_ntoa|if_nametoindex|if_indextoname|getifaddrs|shm_open|shm_unlink|mq_open|sem_open|res_query|res_search)(@.*)?$'
FORBIDDEN_LIBS='lib(fastrtps|fastdds|ddsc|cyclonedds|nddscore|rti|zenoh|iceoryx|rmw_dds_common|ssl|crypto|boost_system|boost_asio)'
# Exported (defined, global) symbols must belong to the RMW API or our namespace.
# Extended in later milestones only with a recorded reason.
ALLOWED_EXPORTS='^(rmw_[A-Za-z0-9_]+|_ZN[0-9]+rmw_intraprocess_cpp[A-Za-z0-9_]*|_ZTV[A-Za-z0-9_]*rmw_intraprocess_cpp.*|_ZTI[A-Za-z0-9_]*rmw_intraprocess_cpp.*|_ZTS[A-Za-z0-9_]*rmw_intraprocess_cpp.*|_init|_fini|__bss_start|_edata|_end)$'
# libstdc++ declares namespace std with default visibility, so weak (W/V) or
# GNU-unique (u) instantiations of std templates and inline variables (e.g.
# std::piecewise_construct), and typeinfo/vtables/local statics of std templates
# (_ZTI/_ZTS/_ZTV/_ZZ...St), can be exported even with -fvisibility=hidden.
# Allowed only as weak/unique, never as strong definitions.
STD_WEAK_EXPORTS='^_Z(T[ISV]|Z)?N?K?St'

mapfile -t libs < <(find "$INSTALL" -path '*/lib/*' -name 'librmw_intraprocess*.so*' -type f 2>/dev/null | sort)
if [[ ${#libs[@]} -eq 0 ]]; then
  echo "symbol check: no librmw_intraprocess*.so found under $INSTALL" >&2
  exit 1
fi

fail=0
for lib in "${libs[@]}"; do
  bad_imports="$(nm -D --undefined-only "$lib" | awk '{print $NF}' | grep -E "$FORBIDDEN_IMPORTS" || true)"
  if [[ -n "$bad_imports" ]]; then
    echo "symbol check: $lib imports networking/IPC symbols:" >&2; echo "$bad_imports" | sed 's/^/  /' >&2; fail=1
  fi
  bad_libs="$(readelf -d "$lib" | grep NEEDED | grep -E "$FORBIDDEN_LIBS" || true)"
  if [[ -n "$bad_libs" ]]; then
    echo "symbol check: $lib links forbidden libraries:" >&2; echo "$bad_libs" | sed 's/^/  /' >&2; fail=1
  fi
  # Export allow-list: the shipped RMW library only (the test-only torture
  # package's generated type support and components export their own API).
  bad_exports=""
  [[ "$(basename "$lib")" == librmw_intraprocess_cpp.so* ]] && bad_exports="$(nm -D --defined-only "$lib" \
    | awk -v std="$STD_WEAK_EXPORTS" '$2 ~ /[TDBRVWiu]/ && !($2 ~ /^[uVW]$/ && $3 ~ std) {print $3}' \
    | grep -Ev "$ALLOWED_EXPORTS" || true)"
  if [[ -n "$bad_exports" ]]; then
    echo "symbol check: $lib exports symbols outside the allowed set:" >&2
    echo "$bad_exports" | sed 's/^/  /' >&2; fail=1
  fi
  n_exports="$(nm -D --defined-only "$lib" | awk '$2 ~ /[TDBRVWiu]/' | wc -l)"
  echo "symbol check: $(basename "$lib"): ${n_exports} exported symbols, no networking imports"
done

[[ $fail -eq 0 ]] && echo "symbol check: OK" || { echo "symbol check: FAILED" >&2; exit 1; }
