#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
test_bin="/tmp/mod-native-social-standalone-tests"

g++ -std=c++17 -Wall -Wextra -Werror -pedantic \
  -I"$repo_dir/src" \
  "$repo_dir/tests/native_social_tests.cpp" \
  "$repo_dir/src/SocialAccountEligibility.cpp" \
  "$repo_dir/src/SocialProfile.cpp" \
  "$repo_dir/src/SocialDirectory.cpp" \
  "$repo_dir/src/NsocCodec.cpp" \
  "$repo_dir/src/SocialAdmin.cpp" \
  -o "$test_bin"

"$test_bin"
python3 "$repo_dir/tests/epf_test.py"
python3 "$repo_dir/tests/safety_test.py"
python3 "$repo_dir/tests/client_ui_test.py"
