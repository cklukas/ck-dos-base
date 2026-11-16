#include "dosbase/devtools/cl_compiler.hpp"
#include "dosbase/devtools/cl_preproc.hpp"
#include "dosbase/devtools/dos_file.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string to_lower_ascii(std::string_view s) {
    std::string out(s);
    for (char& ch : out) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

std::string to_upper_ascii(std::string_view s) {
    std::string out(s);
    for (char& ch : out) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return out;
}

std::string normalize_dos_path_str(std::string_view p) {
    std::string out(p);
    std::replace(out.begin(), out.end(), '\\', '/');
    return out;
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open input file: " + path.string());
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void write_binary_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& data) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("cannot open output file: " + path.string());
    }
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!out) {
        throw std::runtime_error("failed writing output file: " + path.string());
    }
}

void print_usage() {
    std::cout << "ckcl - Microsoft C 5.1/6.0 compatible 16-bit OMF compiler\n"
              << "Usage: ckcl [options] file.c [...]\n"
              << "Options:\n"
              << "  -c              Compile only, do not link\n"
              << "  -I<dir>         Add include search directory\n"
              << "  -D<name>[=val]  Define preprocessor macro\n"
              << "  -U<name>        Undefine preprocessor macro\n"
              << "  -Fo<file|dir>   Set output object file or directory\n"
              << "  -AS|-AM|-AC|-AL Select memory model (Small/Medium/Compact/Large)\n"
              << "  -Zp             Pack structures on 1-byte boundaries\n"
              << "  -Zl             Omit default library name from object file\n"
              << "  -X              Ignore INCLUDE environment variable\n"
              << "  -nologo         Suppress startup banner\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        dosbase::devtools::PreprocOptions pp_opts;
        dosbase::devtools::CompileOptions cc_opts;
        bool ignore_env_include = false;
        std::string fo_arg;
        std::vector<std::string> inputs;

        auto add_define = [&](std::string_view def) {
            const auto eq = def.find('=');
            if (eq == std::string_view::npos) {
                pp_opts.defines[std::string(def)] = "1";
            } else {
                pp_opts.defines[std::string(def.substr(0, eq))] = std::string(def.substr(eq + 1));
            }
        };

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-h" || arg == "--help" || arg == "/?") {
                print_usage();
                return 0;
            }
            const bool is_option =
                arg.size() >= 2 &&
                (arg[0] == '-' ||
                 (arg[0] == '/' && arg.find('/', 1) == std::string::npos &&
                  !std::filesystem::exists(arg)));
            if (is_option) {
                std::string opt = arg.substr(1);
                std::string opt_lower = to_lower_ascii(opt);

                if (opt_lower == "c" || opt_lower == "nologo" || opt_lower == "ze" ||
                    opt_lower == "za" || opt_lower == "gs" || opt_lower == "g0" ||
                    opt_lower == "g1" || opt_lower == "g2" || opt_lower == "g3" ||
                    opt_lower == "zi" || opt_lower == "zd" || opt_lower == "fc" ||
                    opt_lower == "fs" || opt_lower == "fa" || opt_lower == "fp" ||
                    opt_lower == "j" || opt_lower == "oi" || opt_lower == "od" ||
                    opt_lower == "os" || opt_lower == "ot" || opt_lower == "ox" ||
                    opt_lower == "oa" || opt_lower == "oat" || opt_lower == "ol" ||
                    opt_lower == "op" || opt_lower == "w0" || opt_lower == "w1" ||
                    opt_lower == "w2" || opt_lower == "w3" || opt_lower == "w4" ||
                    opt_lower == "wx") {
                    continue;
                }
                if (opt_lower == "x") {
                    ignore_env_include = true;
                    continue;
                }
                if (opt_lower == "zp" || opt_lower == "zp1") {
                    cc_opts.pack_structs = true;
                    continue;
                }
                if (opt_lower == "zlp") {
                    cc_opts.pack_structs = true;
                    cc_opts.omit_default_lib = true;
                    continue;
                }
                if (opt_lower == "zl") {
                    cc_opts.omit_default_lib = true;
                    continue;
                }
                if (opt_lower == "as" || opt_lower == "asw" || opt_lower == "asu") {
                    cc_opts.model = dosbase::devtools::MemoryModel::small_model;
                    continue;
                }
                if (opt_lower == "am" || opt_lower == "amw") {
                    cc_opts.model = dosbase::devtools::MemoryModel::medium_model;
                    continue;
                }
                if (opt_lower == "ac" || opt_lower == "acw") {
                    cc_opts.model = dosbase::devtools::MemoryModel::compact_model;
                    continue;
                }
                if (opt_lower == "al" || opt_lower == "alw") {
                    cc_opts.model = dosbase::devtools::MemoryModel::large_model;
                    continue;
                }
                if (opt_lower == "i") {
                    if (i + 1 >= argc) {
                        throw std::runtime_error("missing directory after " + arg);
                    }
                    pp_opts.include_dirs.emplace_back(normalize_dos_path_str(argv[++i]));
                    continue;
                }
                if (opt_lower.rfind("i", 0) == 0 && opt.size() > 1) {
                    pp_opts.include_dirs.emplace_back(normalize_dos_path_str(opt.substr(1)));
                    continue;
                }
                if (opt_lower == "d") {
                    if (i + 1 >= argc) {
                        throw std::runtime_error("missing macro name after " + arg);
                    }
                    add_define(argv[++i]);
                    continue;
                }
                if (opt_lower.rfind("d", 0) == 0 && opt.size() > 1) {
                    add_define(opt.substr(1));
                    continue;
                }
                if (opt_lower == "u") {
                    if (i + 1 >= argc) {
                        throw std::runtime_error("missing macro name after " + arg);
                    }
                    pp_opts.defines.erase(argv[++i]);
                    continue;
                }
                if (opt_lower.rfind("u", 0) == 0 && opt.size() > 1) {
                    pp_opts.defines.erase(opt.substr(1));
                    continue;
                }
                if (opt_lower.rfind("fo", 0) == 0) {
                    if (opt.size() > 2) {
                        fo_arg = normalize_dos_path_str(opt.substr(2));
                    } else if (i + 1 < argc) {
                        fo_arg = normalize_dos_path_str(argv[++i]);
                    }
                    continue;
                }
                if (opt_lower.rfind("fe", 0) == 0 || opt_lower.rfind("fa", 0) == 0 ||
                    opt_lower.rfind("fc", 0) == 0 || opt_lower.rfind("fl", 0) == 0) {
                    if (opt.size() == 2 && i + 1 < argc) {
                        ++i;
                    }
                    continue;
                }
                if (opt_lower[0] == 'o' || opt_lower[0] == 'g' || opt_lower[0] == 'w' ||
                    opt_lower[0] == 'z' || opt_lower[0] == 'f') {
                    continue;
                }
                throw std::runtime_error("unrecognized option: " + arg);
            }
            inputs.push_back(normalize_dos_path_str(arg));
        }

        if (inputs.empty()) {
            print_usage();
            return 1;
        }

        if (!ignore_env_include) {
            if (const char* env_inc = std::getenv("INCLUDE")) {
                std::string inc_str(env_inc);
                std::size_t start = 0;
                while (start <= inc_str.size()) {
#if defined(_WIN32)
                    std::size_t sep = inc_str.find(';', start);
#else
                    std::size_t sep = std::string::npos;
                    for (std::size_t k = start; k < inc_str.size(); ++k) {
                        if (inc_str[k] == ';') {
                            sep = k;
                            break;
                        }
                        if (inc_str[k] == ':') {
                            const bool is_drive =
                                (k == start + 1) &&
                                std::isalpha(static_cast<unsigned char>(inc_str[start])) &&
                                (k + 1 < inc_str.size() &&
                                 (inc_str[k + 1] == '\\' || inc_str[k + 1] == '/'));
                            if (!is_drive) {
                                sep = k;
                                break;
                            }
                        }
                    }
#endif
                    std::string part = (sep == std::string::npos) ? inc_str.substr(start)
                                                                  : inc_str.substr(start, sep - start);
                    if (!part.empty()) {
                        pp_opts.include_dirs.emplace_back(normalize_dos_path_str(part));
                    }
                    if (sep == std::string::npos) {
                        break;
                    }
                    start = sep + 1;
                }
            }
        }

        if (pp_opts.defines.find("MSDOS") == pp_opts.defines.end()) {
            pp_opts.defines["MSDOS"] = "1";
        }
        if (pp_opts.defines.find("_MSDOS") == pp_opts.defines.end()) {
            pp_opts.defines["_MSDOS"] = "1";
        }
        if (pp_opts.defines.find("M_I86") == pp_opts.defines.end()) {
            pp_opts.defines["M_I86"] = "1";
        }
        switch (cc_opts.model) {
            case dosbase::devtools::MemoryModel::small_model:
                pp_opts.defines.try_emplace("M_I86SM", "1");
                break;
            case dosbase::devtools::MemoryModel::medium_model:
                pp_opts.defines.try_emplace("M_I86MM", "1");
                break;
            case dosbase::devtools::MemoryModel::compact_model:
                pp_opts.defines.try_emplace("M_I86CM", "1");
                break;
            case dosbase::devtools::MemoryModel::large_model:
                pp_opts.defines.try_emplace("M_I86LM", "1");
                break;
        }

        for (const auto& input_raw : inputs) {
            const auto input_path = dosbase::devtools::resolve_dos_input(input_raw);
            const std::string source = read_text_file(input_path);
            const std::string preprocessed = dosbase::devtools::preprocess_c_source(source, input_path, pp_opts);

            dosbase::devtools::CompileOptions file_opts = cc_opts;
            file_opts.module_name = to_upper_ascii(input_path.stem().string());
            const auto obj_bytes = dosbase::devtools::compile_c_to_omf(preprocessed, file_opts);

            std::filesystem::path out_path;
            if (!fo_arg.empty()) {
                const bool ends_with_slash = !fo_arg.empty() && fo_arg.back() == '/';
                std::filesystem::path fo_path(fo_arg);
                if (ends_with_slash || std::filesystem::is_directory(fo_path)) {
                    out_path = dosbase::devtools::resolve_dos_output(
                        (fo_path / (input_path.stem().string() + ".OBJ")).string());
                } else {
                    out_path = dosbase::devtools::resolve_dos_output(fo_path.string());
                }
            } else {
                out_path = dosbase::devtools::resolve_dos_output(
                    input_path.stem().string() + ".OBJ");
            }

            write_binary_file(out_path, obj_bytes);
        }

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "ckcl: error: " << ex.what() << "\n";
        return 1;
    }
}
