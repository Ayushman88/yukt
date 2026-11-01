#include "cmd.hpp"
#include "driver/manifest.hpp"
#include "driver/paths.hpp"
#include "test_harness.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

#ifndef YUKT_EXECUTABLE
#define YUKT_EXECUTABLE "yukt"
#endif

namespace {

int g_n = 0;

std::filesystem::path make_tree(const std::string& toml, const std::string& entry_rel = "src/main.yukt") {
    const auto root = std::filesystem::temp_directory_path() /
                      ("yukt-man-" + std::to_string(getpid()) + "-" + std::to_string(++g_n));
    std::filesystem::create_directories(root / std::filesystem::path(entry_rel).parent_path());
    std::ofstream(root / "yukt.toml") << toml;
    std::ofstream(root / entry_rel) << "create main:\n    say 1\n";
    return root;
}

bool load_has_title(const std::filesystem::path& toml, const std::string& title) {
    std::ifstream in(toml);
    std::ostringstream buf;
    buf << in.rdbuf();
    yukt::DiagnosticEngine diagnostics(toml.string(), buf.str());
    const auto loaded = yukt::load_manifest(toml, diagnostics);
    if (loaded.has_value()) {
        return false;
    }
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        if (diagnostic.title == title) {
            return true;
        }
    }
    return false;
}

const char* kValid = R"(name = "hello"
version = "0.1.0"
type = "cli"
entry = "src/main.yukt"
)";

}  // namespace

int main() {
    {
        ::setenv("YUKT_STD", "/tmp/yukt-empty-std", 1);
        REQUIRE(yukt::stdlib_root() == std::filesystem::path("/tmp/yukt-empty-std"));
        ::unsetenv("YUKT_STD");
    }
    {
        const auto exe = yukt::executable_path();
        REQUIRE(!exe.empty());
        REQUIRE(std::filesystem::exists(exe));
        REQUIRE(yukt::install_prefix() == exe.parent_path().parent_path());
    }
    {
        const auto runtime = yukt::runtime_archive();
        REQUIRE(!runtime.empty());
        REQUIRE(std::filesystem::exists(runtime));
        REQUIRE(runtime.filename() == "libyukt_runtime.a");
    }

    {
        const auto root = make_tree(kValid);
        std::ifstream in(root / "yukt.toml");
        std::ostringstream buf;
        buf << in.rdbuf();
        yukt::DiagnosticEngine diagnostics((root / "yukt.toml").string(), buf.str());
        const auto loaded = yukt::load_manifest(root / "yukt.toml", diagnostics);
        REQUIRE(loaded.has_value());
        REQUIRE(loaded->name == "hello");
        REQUIRE(loaded->version == "0.1.0");
        REQUIRE(loaded->type == "cli");
        REQUIRE(loaded->root == std::filesystem::canonical(root));
        REQUIRE(loaded->entry == std::filesystem::canonical(root / "src" / "main.yukt"));
        const auto entry_rel = std::filesystem::relative(loaded->entry, loaded->root);
        REQUIRE(!entry_rel.empty());
        REQUIRE(entry_rel.native().find("..") == std::string::npos);
    }

    {
        const auto root = make_tree("name = \"hello\"\nversion = \"0.1.0\"\ntype = \"cli\"\nentry = \"../escape.yukt\"\n");
        REQUIRE(load_has_title(root / "yukt.toml", "Invalid manifest"));
    }

    {
        const auto root = make_tree("name = \"hello\"\nversion = \"0.1.0\"\ntype = \"cli\"\nentry = \"/tmp/x.yukt\"\n");
        REQUIRE(load_has_title(root / "yukt.toml", "Invalid manifest"));
    }

    {
        const auto root = make_tree(kValid);
        const auto outside = std::filesystem::temp_directory_path() /
                             ("yukt-out-" + std::to_string(getpid()) + "-" + std::to_string(++g_n));
        std::filesystem::create_directories(outside);
        std::ofstream(outside / "x.yukt") << "create main:\n    say 1\n";
        std::filesystem::create_directory_symlink(outside, root / "sub");
        std::ofstream(root / "yukt.toml") << "name = \"hello\"\nversion = \"0.1.0\"\ntype = \"cli\"\nentry = \"sub/x.yukt\"\n";
        REQUIRE(load_has_title(root / "yukt.toml", "Invalid manifest"));
    }

    {
        const auto root = make_tree(std::string(kValid) + "[dependencies]\nfoo = \"1\"\n");
        REQUIRE(load_has_title(root / "yukt.toml", "Invalid manifest"));
    }

    {
        const auto root = make_tree(std::string(kValid) + "foo = \"bar\"\n");
        REQUIRE(load_has_title(root / "yukt.toml", "Invalid manifest"));
    }

    {
        const auto root = make_tree(std::string(kValid) + "name = \"other\"\n");
        REQUIRE(load_has_title(root / "yukt.toml", "Invalid manifest"));
    }

    {
        const auto root = make_tree(kValid);
        const auto found = yukt::find_manifest(root / "src");
        REQUIRE(found.has_value());
        REQUIRE(*found == std::filesystem::canonical(root / "yukt.toml") ||
                *found == root / "yukt.toml");
    }

    {
        const auto empty = std::filesystem::temp_directory_path() /
                           ("yukt-none-" + std::to_string(getpid()) + "-" + std::to_string(++g_n));
        std::filesystem::create_directories(empty);
        REQUIRE(!yukt::find_manifest(empty).has_value());
    }

    const std::string yukt_exe = YUKT_EXECUTABLE;
    {
        const auto tmp = std::filesystem::temp_directory_path() /
                         ("yukt-new-" + std::to_string(getpid()) + "-" + std::to_string(++g_n));
        std::filesystem::create_directories(tmp);
        const auto created = run_cmd({yukt_exe, "new", "hello", "--type", "cli"}, tmp);
        REQUIRE_MSG(created.status == 0, created.err + created.out);
        REQUIRE(std::filesystem::exists(tmp / "hello" / "yukt.toml"));
        REQUIRE(std::filesystem::exists(tmp / "hello" / "src" / "main.yukt"));
        REQUIRE(std::filesystem::exists(tmp / "hello" / "tests" / "main.yukt"));

        const auto again = run_cmd({yukt_exe, "new", "hello", "--type", "cli"}, tmp);
        REQUIRE(again.status != 0);
        REQUIRE(again.err.find("Cannot create project") != std::string::npos);

        const auto bad_name = run_cmd({yukt_exe, "new", "../x", "--type", "cli"}, tmp);
        REQUIRE(bad_name.status != 0);
        REQUIRE(bad_name.err.find("Invalid project name") != std::string::npos);

        const auto missing_type = run_cmd({yukt_exe, "new", "notyped"}, tmp);
        REQUIRE(missing_type.status != 0);
        REQUIRE(missing_type.err.find("Invalid project type") != std::string::npos);

        const auto bad_type = run_cmd({yukt_exe, "new", "webapp", "--type", "web"}, tmp);
        REQUIRE(bad_type.status != 0);
        REQUIRE(bad_type.err.find("Invalid project type") != std::string::npos);
    }

    {
        const auto tmp = std::filesystem::temp_directory_path() /
                         ("yukt-build-" + std::to_string(getpid()) + "-" + std::to_string(++g_n));
        std::filesystem::create_directories(tmp);
        const auto created = run_cmd({yukt_exe, "new", "hello", "--type", "cli"}, tmp);
        REQUIRE_MSG(created.status == 0, created.err + created.out);
        const auto project = tmp / "hello";
        const auto built = run_cmd({yukt_exe, "build"}, project);
        REQUIRE_MSG(built.status == 0, built.err + built.out);
        REQUIRE(std::filesystem::exists(project / "build" / "hello"));
        REQUIRE(::access((project / "build" / "hello").c_str(), X_OK) == 0);

        const auto ran = run_cmd({yukt_exe, "run"}, project);
        REQUIRE_MSG(ran.status == 0, ran.err + ran.out);
        REQUIRE(ran.out.find("hello") != std::string::npos);

        const auto file_build = run_cmd({yukt_exe, "build", "src/main.yukt"}, project);
        REQUIRE_MSG(file_build.status == 0, file_build.err + file_build.out);

        const auto missing = run_cmd({yukt_exe, "build"}, tmp);
        REQUIRE(missing.status != 0);
        REQUIRE(missing.err.find("Cannot find project") != std::string::npos);
    }

    {
        const auto tmp = std::filesystem::temp_directory_path() /
                         ("yukt-test-" + std::to_string(getpid()) + "-" + std::to_string(++g_n));
        std::filesystem::create_directories(tmp);
        const auto created = run_cmd({yukt_exe, "new", "hello", "--type", "cli"}, tmp);
        REQUIRE_MSG(created.status == 0, created.err + created.out);
        const auto project = tmp / "hello";
        const auto ok = run_cmd({yukt_exe, "test"}, project);
        REQUIRE_MSG(ok.status == 0, ok.err + ok.out);
        REQUIRE(ok.out.find("ok tests/main.yukt") != std::string::npos);

        std::ofstream(project / "tests" / "bad.yukt") << "create main:\n    say 2\n";
        std::ofstream(project / "tests" / "bad.yukt.out") << "1\n";
        const auto mismatch = run_cmd({yukt_exe, "test"}, project);
        REQUIRE(mismatch.status != 0);
        REQUIRE((mismatch.out + mismatch.err).find("stdout mismatch") != std::string::npos);

        std::filesystem::remove(project / "tests" / "main.yukt");
        std::filesystem::remove(project / "tests" / "main.yukt.out");
        std::filesystem::remove(project / "tests" / "bad.yukt");
        std::filesystem::remove(project / "tests" / "bad.yukt.out");
        const auto empty = run_cmd({yukt_exe, "test"}, project);
        REQUIRE(empty.status != 0);
        REQUIRE(empty.err.find("No tests found") != std::string::npos);

        std::ofstream(project / "tests" / "broken.yukt") << "create main\n    say 1\n";
        const auto broken = run_cmd({yukt_exe, "test"}, project);
        REQUIRE(broken.status != 0);
        REQUIRE((broken.out + broken.err).find("FAIL") != std::string::npos);
    }

    {
        const auto tmp = std::filesystem::temp_directory_path() /
                         ("yukt-tpl-" + std::to_string(getpid()) + "-" + std::to_string(++g_n));
        std::filesystem::create_directories(tmp);
        const auto cli = run_cmd({yukt_exe, "new", "t", "--type", "cli"}, tmp);
        REQUIRE_MSG(cli.status == 0, cli.err + cli.out);
        const auto cli_test = run_cmd({yukt_exe, "test"}, tmp / "t");
        REQUIRE_MSG(cli_test.status == 0, cli_test.err + cli_test.out);
        const auto cli_build = run_cmd({yukt_exe, "build"}, tmp / "t");
        REQUIRE_MSG(cli_build.status == 0, cli_build.err + cli_build.out);

        const auto server = run_cmd({yukt_exe, "new", "s", "--type", "server"}, tmp);
        REQUIRE_MSG(server.status == 0, server.err + server.out);
        const auto server_test = run_cmd({yukt_exe, "test"}, tmp / "s");
        REQUIRE_MSG(server_test.status == 0, server_test.err + server_test.out);
        REQUIRE(server_test.status != 124);
        const auto server_build = run_cmd({yukt_exe, "build"}, tmp / "s");
        REQUIRE_MSG(server_build.status == 0, server_build.err + server_build.out);
    }

    return yukt::test::summary("yukt_project_tests");
}
