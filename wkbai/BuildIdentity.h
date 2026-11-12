// wkbai - which build this is, for multiplayer's build check
// Licensed under the GNU General Public License 3

#pragma once

#include <string>
#include <nlohmann/json.hpp>

// A host can require its guests to run the same wkbai (Server::buildPolicy). Each build knows:
// - its network version (MatchmakingGameVersion: the protocol; different ones can't play together);
// - the source commit it was built from, and whether the source had uncommitted changes
//   ("modified"), from the generated buildinfo.h (cmake/buildinfo.cmake, at every build);
// - a hash of its program code: SHA-256 of the exe's .text section as it is in memory, with the
//   loader's relocations masked out (they depend on where Windows put the exe).
// For an identical build the host doesn't trust what the guest says: it sends a random challenge
// (NETCLIMSG_BUILD_CHALLENGE) and the guest answers with SHA-256(challenge + its code in memory
// now); the host works out the same from its own code. A copy whose code was changed, on disk or
// in memory, gives another answer. This makes changed copies hard to connect, not impossible: wkbai
// is open source, and a determined person can build a client that answers from an unchanged copy
// it keeps beside it (docs/engine/multiplayer-lobby.md, "Build check").
namespace BuildIdentity {

const char* Commit();   // the source commit (12 hex digits), "unknown" without git
bool Modified();        // built from a source with uncommitted changes
bool Official();        // built by the release workflow (WKBAI_OFFICIAL_BUILD)
// "78828e4b1c2d", "78828e4b1c2d (modified)"
std::string Describe();
// SHA-256 of the code in memory (hex), worked out once; "" where it can't be (not Windows)
const std::string& CodeHash();
// SHA-256 of challenge + the code as it is in memory now (hex); "" where it can't be
std::string ChallengeAnswer(const std::string& challenge);

// what a guest tells the host (ModManifest's "build"): net, commit, modified, official, code
nlohmann::json Json();

// Server::buildPolicy
enum Policy {
	SAME_NETWORK = 0,  // the same network version (always required)
	SAME_VERSION = 1,  // and the same commit; a modified build only with the very same code (default)
	IDENTICAL = 2,     // and the very same program code, proved by the challenge
};
// why a guest's build doesn't fit the host's policy ("" if it does); answerOk: the challenge's
// answer was right (only looked at for IDENTICAL)
std::string Compare(int policy, const nlohmann::json& host, const nlohmann::json& guest, bool answerOk);

}
