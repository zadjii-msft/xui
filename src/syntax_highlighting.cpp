#include "xui/syntax_highlighting.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>
#ifdef XUI_ENABLE_LSH
#include <windows.h>
#include <lsh.h>
#include "lsh_grammar.hpp"
#include <mutex>
#endif

namespace xui {
#ifdef XUI_ENABLE_LSH
namespace {
class Engine {
    HMODULE module_{};
    lsh_engine* engine_{};
    template<typename T> T symbol(const char* name) {
        const auto address = GetProcAddress(module_, name);
        if (!address) throw std::runtime_error(std::string("Missing LSH export: ") + name);
        return reinterpret_cast<T>(address);
    }
public:
    decltype(&lsh_engine_free) free_engine{};
    decltype(&lsh_highlight) highlight{};
    decltype(&lsh_result_count) count{};
    decltype(&lsh_result_spans) spans{};
    decltype(&lsh_result_free) free_result{};
    decltype(&lsh_kind_name) kind{};
    std::vector<std::string> languages;
    std::mutex mutex;
    Engine() {
        HMODULE owner{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&syntax_highlighting_available), &owner))
            throw std::runtime_error("Cannot locate the XUI syntax runtime");
        std::wstring path(32768, L'\0');
        const auto size = GetModuleFileNameW(owner, path.data(), static_cast<DWORD>(path.size()));
        if (!size || size == path.size()) throw std::runtime_error("Cannot locate the XUI syntax runtime path");
        path.resize(size);
        path.resize(path.find_last_of(L"\\/") + 1);
        path += L"lsh_lib.dll";
        module_ = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module_) throw std::runtime_error("Cannot load lsh_lib.dll beside the XUI runtime");
        try {
            if (symbol<decltype(&lsh_abi_version)>("lsh_abi_version")() != 1)
                throw std::runtime_error("Unsupported LSH ABI");
            free_engine = symbol<decltype(free_engine)>("lsh_engine_free");
            highlight = symbol<decltype(highlight)>("lsh_highlight");
            count = symbol<decltype(count)>("lsh_result_count");
            spans = symbol<decltype(spans)>("lsh_result_spans");
            free_result = symbol<decltype(free_result)>("lsh_result_free");
            kind = symbol<decltype(kind)>("lsh_kind_name");
            const auto create = symbol<decltype(&lsh_engine_create_with_definition)>("lsh_engine_create_with_definition");
            const auto free_error = symbol<decltype(&lsh_error_free)>("lsh_error_free");
            char* diagnostic{};
            constexpr std::string_view name = "xui-bundled-grammars";
            const auto status = create(reinterpret_cast<const uint8_t*>(name.data()), name.size(),
                reinterpret_cast<const uint8_t*>(detail::lsh_grammar.data()), detail::lsh_grammar.size(), &engine_, &diagnostic);
            std::unique_ptr<char, decltype(free_error)> error(diagnostic, free_error);
            if (status != LSH_OK)
                throw std::runtime_error(diagnostic ? diagnostic : "Cannot compile the bundled LSH grammars");
            const auto language_count = symbol<decltype(&lsh_language_count)>("lsh_language_count");
            const auto language_name = symbol<decltype(&lsh_language_name)>("lsh_language_name");
            for (size_t i = 0; i < language_count(engine_); ++i) {
                const auto value = language_name(engine_, i);
                if (!value) throw std::runtime_error("Invalid LSH language table");
                languages.emplace_back(value);
            }
        } catch (...) {
            if (engine_ && free_engine) free_engine(engine_);
            FreeLibrary(module_);
            throw;
        }
    }
    ~Engine() { free_engine(engine_); FreeLibrary(module_); }
    lsh_engine* handle() const { return engine_; }
};
std::shared_ptr<Engine> engine() {
    static auto value = std::make_shared<Engine>();
    return value;
}
SyntaxKind token_kind(std::string_view kind) {
    if (kind == "comment") return SyntaxKind::comment;
    if (kind == "string") return SyntaxKind::string;
    if (kind == "constant.numeric") return SyntaxKind::number;
    if (kind.starts_with("keyword.") || kind == "constant.language") return SyntaxKind::keyword;
    if (kind.starts_with("storage.")) return SyntaxKind::type;
    if (kind == "method") return SyntaxKind::function;
    if (kind == "variable") return SyntaxKind::variable;
    return SyntaxKind::other;
}
std::vector<SyntaxSpan> tokenize(Engine& engine, std::string_view language, std::wstring_view text) {
    if (text.empty()) return {};
    // Native paragraphs are CR. A one-code-unit LF substitution preserves UTF-16 offsets.
    std::wstring lines(text);
    std::replace(lines.begin(), lines.end(), L'\r', L'\n');
    const auto length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, lines.data(),
        static_cast<int>(lines.size()), nullptr, 0, nullptr, nullptr);
    if (!length) throw std::runtime_error("Cannot encode syntax source as UTF-8");
    std::string utf8(length, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, lines.data(), static_cast<int>(lines.size()),
            utf8.data(), length, nullptr, nullptr))
        throw std::runtime_error("Cannot encode syntax source as UTF-8");
    std::lock_guard lock(engine.mutex);
    lsh_result* raw{};
    const auto status = engine.highlight(engine.handle(), reinterpret_cast<const uint8_t*>(language.data()), language.size(),
        reinterpret_cast<const uint8_t*>(utf8.data()), utf8.size(), &raw);
    if (status != LSH_OK) throw std::runtime_error("LSH highlighting failed with status " + std::to_string(status));
    std::unique_ptr<lsh_result, decltype(engine.free_result)> result(raw, engine.free_result);
    std::vector<SyntaxSpan> output;
    const auto count = engine.count(raw);
    const auto spans = engine.spans(raw);
    size_t byte_offset{}, utf16_offset{};
    for (size_t i = 0; i < count; ++i) {
        const auto& span = spans[i];
        if (span.start != byte_offset || span.end <= span.start || span.end > utf8.size())
            throw std::runtime_error("Invalid LSH token range");
        const auto units = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data() + span.start,
            static_cast<int>(span.end - span.start), nullptr, 0);
        if (!units) throw std::runtime_error("LSH token splits a UTF-8 character");
        const auto name = engine.kind(engine.handle(), span.kind);
        if (!name) throw std::runtime_error("Invalid LSH token kind");
        const auto kind = token_kind(name);
        const size_t end = utf16_offset + units;
        if (kind != SyntaxKind::other) {
            if (!output.empty() && output.back().end == utf16_offset && output.back().kind == kind)
                output.back().end = end;
            else output.push_back({utf16_offset, end, kind});
        }
        utf16_offset = end;
        byte_offset = span.end;
    }
    if (byte_offset != utf8.size() || utf16_offset != text.size())
        throw std::runtime_error("LSH tokens do not cover the source");
    return output;
}
}
#endif

bool syntax_highlighting_available() noexcept {
#ifdef XUI_ENABLE_LSH
    return true;
#else
    return false;
#endif
}
void set_syntax_language(MultilineText& document, std::string_view language) {
    if (language.empty()) { document.set_syntax_highlighter({}); return; }
#ifdef XUI_ENABLE_LSH
    auto shared = engine();
    if (std::find(shared->languages.begin(), shared->languages.end(), language) == shared->languages.end())
        throw std::invalid_argument("Unknown LSH syntax language");
    document.set_syntax_highlighter([shared, language = std::string(language)](std::wstring_view text) {
        return tokenize(*shared, language, text);
    });
#else
    throw std::runtime_error("XUI was built without LSH syntax highlighting");
#endif
}
std::string_view syntax_language_for_path(std::wstring_view path) {
    const auto slash = path.find_last_of(L"\\/");
    auto name = path.substr(slash == path.npos ? 0 : slash + 1);
    std::wstring lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t c) {
        return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + L'a' - L'A') : c;
    });
    if (lower == L".gitignore" || lower == L".ignore") return "ignore";
    const auto dot = lower.find_last_of(L'.');
    const auto ext = dot == lower.npos ? std::wstring_view{} : std::wstring_view(lower).substr(dot);
    struct Mapping { std::wstring_view extension; std::string_view language; };
    static constexpr Mapping mappings[] = {
        {L".xui", "xui"}, {L".cs", "csharp"}, {L".csx", "csharp"}, {L".c", "c"},
        {L".cpp", "cpp"}, {L".cc", "cpp"}, {L".cxx", "cpp"}, {L".h", "cpp"}, {L".hpp", "cpp"},
        {L".rs", "rust"}, {L".json", "json"}, {L".xml", "xml"}, {L".config", "xml"},
        {L".csproj", "xml"}, {L".props", "xml"}, {L".targets", "xml"}, {L".slnx", "xml"},
        {L".yaml", "yaml"}, {L".yml", "yaml"}, {L".py", "python"},
        {L".js", "javascript"}, {L".jsx", "javascript"},
        {L".ps1", "powershell"}, {L".psm1", "powershell"}, {L".psd1", "powershell"},
        {L".sh", "shellscript"}, {L".bash", "shellscript"}, {L".md", "markdown"},
        {L".markdown", "markdown"}, {L".diff", "diff"}, {L".patch", "diff"},
        {L".properties", "properties"}, {L".lsh", "lsh"}
    };
    for (const auto& mapping : mappings) if (mapping.extension == ext) return mapping.language;
    return {};
}
}
