#!/bin/bash

script_dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
project_dir="$( dirname "$script_dir" )"
source_dir="$project_dir/src"
include_dir="$project_dir/include"
tests_dir="$project_dir/build/tests"

generated_dir="$script_dir/generated"
html_dir="$generated_dir/html"
data_file="$generated_dir/default.profdata"
json_file="$generated_dir/report.json"
coverage_file="$generated_dir/coverage.json"
summary_file="$generated_dir/summary.rmu"

# cleanup output directory
if [ -d "$generated_dir" ]; then
  rm -rf "$generated_dir"
fi
mkdir "$generated_dir"

# merge raw coverage info
raw_files="$( find "$tests_dir" -name "*.profraw" | sort | tr '\n' ' ' )"
llvm-profdata merge -sparse $raw_files -o "$data_file"

# create list of binaries
obj_files="$( echo "$raw_files" | \
              sed "s/\.profraw//g" | \
              sed -r "s/ +$//g" | \
              sed "s/ / -object /g" )"

# create list of source files
src_files=$( find "$source_dir" "$include_dir" \
             \( -name '*.c' -o -name '*.h' \) \
             -print | sort | tr '\n' ' ' )

# generate html output
llvm-cov show $obj_files \
  -format html \
  -instr-profile "$data_file" \
  -o "$html_dir" \
  -show-line-counts-or-regions \
  -Xdemangler c++filt -Xdemangler -n \
  $src_files

# generate json output
llvm-cov export $obj_files \
  -instr-profile "$data_file" \
  -Xdemangler c++filt -Xdemangler -n \
  $src_files > "$json_file"
