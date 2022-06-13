#!/bin/bash

set -Eeuo pipefail

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
cd "$DIR/.."

ALL_FILES_TO_CHECK=$(find . -type f -regex ".*\.\(cpp\|hpp\|c\|h\)$" -print | paste -sd " ")
for file in ${ALL_FILES_TO_CHECK}; do
  clang-format -i -verbose "${file}"
done
changes="$(git diff)"
if [[ ! -z "${changes}" ]]; then
  echo "Clang-format check failed!"
  echo "You should fix the following formatting errors:"
  echo "${changes}"
  exit 1
else
  echo "Clang-format check ok!"
  exit 0
fi
