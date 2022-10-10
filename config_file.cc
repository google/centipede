// Copyright 2022 The Centipede Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "./config_file.h"

#include <filesystem>  // NOLINT
#include <string>
#include <set>
#include <utility>
#include <vector>

#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "./config_util.h"
#include "./logging.h"
#include "./remote_file.h"
#include "./util.h"

// TODO(ussuri): Move these flags next to main() ASAP. They are here
//  only temporarily to simplify the APIs and implementation in V1.

ABSL_FLAG(std::string, config, "",
          "Read flags from the specified file. The file can be either local or "
          "remote. Relative paths are referenced from the CWD. The format "
          "should be:\n"
          "--flag=value\n"
          "--another_flag=value\n"
          "...\n"
          "Lines that start with '#' or '//' are comments. Note that this "
          "format is compatible with the built-in --flagfile flag (defined by "
          "Abseil Flags library); however, unlike this flag, --flagfile "
          "supports only local files.\n"
          "Nested --load_config's won't work (but nested --flagfile's will,"
          "provided they point at a local file, e.g. $HOME/.centipede_rc).\n"
          "The flag is position-sensitive: flags read from it override (or "
          "append, in case of std::vector flags) any previous occurrences of "
          "the same flags on the command line, and vice versa.");
ABSL_FLAG(std::string, save_config, "",
          "Saves Centipede flags to the specified file. The file can be either "
          "local or remote. Relative paths are referenced from the CWD. Both "
          "the command-line flags and defaulted flags are saved. The format "
          "is:\n"
          "# --flag's help string.\n"
          "# --flag's default value.\n"
          "--flag=value\n"
          "...\n"
          "This format can be parsed back by both --config and --flagfile. "
          "Unlike those two flags, this flag is not position-sensitive and "
          "always saves the final resolved config.");
ABSL_FLAG(bool, update_config, false,
          "Must be used in combination with --config=<file>. Writes the final "
          "resolved config back to the same file.");

// Declare --flagfile defined by the Abseil Flags library. The flag should point
// at a _local_ file is always automatically parsed by Abseil Flags.
ABSL_DECLARE_FLAG(std::vector<std::string>, flagfile);

#define DASHED_FLAG_NAME(name) "--" << FLAGS_##name.Name()

namespace centipede::config {

namespace {

// TODO(b/250998535): Move to RemoteFileRead(std::string).
void RemoteFileGetContents(const std::filesystem::path& path,
                           std::string& contents) {
  auto* file = RemoteFileOpen(path.c_str(), "r");
  CHECK(file != nullptr) << VV(path);
  ByteArray contents_ba;
  RemoteFileRead(file, contents_ba);
  contents.assign(contents_ba.cbegin(), contents_ba.cend());
  RemoteFileClose(file);
}

// TODO(b/250998535): Move to RemoteFileWrite(std::string).
void RemoteFileSetContents(const std::filesystem::path& path,
                           const std::string& contents) {
  auto* file = RemoteFileOpen(path.c_str(), "w");
  CHECK(file != nullptr) << VV(path);
  ByteArray contents_ba{contents.cbegin(), contents.cend()};
  RemoteFileAppend(file, contents_ba);
  RemoteFileClose(file);
}

}  // namespace

std::vector<char*> CastArgv(const std::vector<std::string>& argv) {
  std::vector<char*> ret_argv;
  ret_argv.reserve(argv.size());
  for (const auto& arg : argv) {
    ret_argv.push_back(const_cast<char*>(arg.c_str()));
  }
  return ret_argv;
}

std::vector<std::string> CastArgv(const std::vector<char*>& argv) {
  return {argv.cbegin(), argv.cend()};
}

std::vector<std::string> CastArgv(int argc, char** argv) {
  return {argv, argv + argc};
}

AugmentedArgvWithCleanup::AugmentedArgvWithCleanup(
    const std::vector<std::string>& orig_argv, const Replacements& replacements,
    BackingResourcesCleanup&& cleanup)
    : was_augmented_{false}, cleanup_{cleanup} {
  argv_.reserve(orig_argv.size());
  for (const auto& old_arg : orig_argv) {
    const std::string& new_arg =
        argv_.emplace_back(absl::StrReplaceAll(old_arg, replacements));
    if (new_arg != old_arg) {
      VLOG(1) << "Augmented argv arg:\n" << VV(old_arg) << "\n" << VV(new_arg);
      was_augmented_ = true;
    }
  }
}

AugmentedArgvWithCleanup::AugmentedArgvWithCleanup(
    AugmentedArgvWithCleanup&& rhs) noexcept {
  *this = std::move(rhs);
}

AugmentedArgvWithCleanup& AugmentedArgvWithCleanup::operator=(
    AugmentedArgvWithCleanup&& rhs) noexcept {
  argv_ = std::move(rhs.argv_);
  was_augmented_ = rhs.was_augmented_;
  cleanup_ = std::move(rhs.cleanup_);
  // Prevent rhs from calling the cleanup in dtor (moving an std::function
  // leaves the moved object in a valid, but undefined, state).
  rhs.cleanup_ = {};
  return *this;
}

AugmentedArgvWithCleanup::~AugmentedArgvWithCleanup() {
  if (cleanup_) cleanup_();
}

AugmentedArgvWithCleanup LocalizeConfigFilesInArgv(
    const std::vector<std::string>& argv) {
  const std::filesystem::path path = absl::GetFlag(FLAGS_config);

  if (!path.empty()) {
    CHECK_NE(path, absl::GetFlag(FLAGS_save_config))
        << "To update config in place, use " << DASHED_FLAG_NAME(update_config);
  }

  // Always need these (--config=<path> can be passed with a local <path>).
  AugmentedArgvWithCleanup::Replacements replacements = {
      // "-". not "--" to support the shortened "-flag" form as well.
      {absl::StrCat("-", FLAGS_config.Name()),
       absl::StrCat("-", FLAGS_flagfile.Name())},
  };
  AugmentedArgvWithCleanup::BackingResourcesCleanup cleanup;

  // Copy the remote config file to a temporary local mirror.
  if (!path.empty() && !std::filesystem::exists(path)) {  // assume remote
    // Read the remote file.
    std::string contents;
    RemoteFileGetContents(path, contents);

    // Save a temporary local copy.
    const std::filesystem::path tmp_dir = TemporaryLocalDirPath();
    const std::filesystem::path local_path = tmp_dir / path.filename();
    LOG(INFO) << "Localizing remote config: " << VV(path) << VV(local_path);
    // NOTE: Ignore "Remote" in the API names here: the paths are always local.
    RemoteMkdir(tmp_dir.c_str());
    RemoteFileSetContents(local_path, contents);

    // Augment the argv to point at the local copy and ensure it is cleaned up.
    replacements.emplace_back(path, local_path);
    cleanup = [local_path]() { std::filesystem::remove(local_path); };
  }

  return AugmentedArgvWithCleanup{argv, replacements, std::move(cleanup)};
}

std::filesystem::path MaybeSaveConfigToFile() {
  std::filesystem::path path;

  // Initialize `path` if --save_config or --update_config is passed.
  if (!absl::GetFlag(FLAGS_save_config).empty()) {
    path = absl::GetFlag(FLAGS_save_config);
    CHECK_NE(path, absl::GetFlag(FLAGS_config))
        << "To update config in place, use " << DASHED_FLAG_NAME(update_config);
    CHECK(!absl::GetFlag(FLAGS_update_config))
        << DASHED_FLAG_NAME(save_config) << " and "
        << DASHED_FLAG_NAME(update_config) << " are mutually exclusive";
  } else if (absl::GetFlag(FLAGS_update_config)) {
    path = absl::GetFlag(FLAGS_config);
    CHECK(!path.empty()) << DASHED_FLAG_NAME(update_config)
                         << " must be used in combination with "
                         << DASHED_FLAG_NAME(config);
  }

  // Save or update the config file.
  if (!path.empty()) {
    const std::set<std::string_view> excluded_flags = {
        FLAGS_config.Name(),
        FLAGS_save_config.Name(),
        FLAGS_update_config.Name(),
    };
    const FlagInfosPerSource flags =
        GetFlagsPerSource("third_party/centipede/", excluded_flags);
    const std::string flags_str = FormatFlagfileString(
        flags, DefaultedFlags::kIncluded, FlagComments::kHelpAndDefault);
    RemoteFileSetContents(path, flags_str);
  }

  return path;
}

}  // namespace centipede::config
