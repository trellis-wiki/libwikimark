# libwikimark

A C library implementing [WikiMark](https://github.com/trellis-wiki/wikimark) extensions for [cmark-gfm](https://github.com/github/cmark-gfm). This is the reference implementation of the WikiMark specification.

WikiMark is a strict superset of [GitHub Flavored Markdown](https://github.github.com/gfm/) with wiki links, templates, semantic annotations, and structured page metadata.

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

## Architecture

libwikimark is **not a fork** of cmark-gfm. It's a standalone extension library that registers plugins with an unmodified cmark-gfm using the standard extension API. This means:

- cmark-gfm handles all CommonMark and GFM parsing
- libwikimark adds WikiMark features as extensions
- Upstream cmark-gfm bug fixes apply automatically

```
cmark-gfm (vendored, unmodified)
    ↑ links against
libwikimark (this library — WikiMark extensions)
    ↑ language bindings
wikimark-python, wikimark-js, etc.
```

## Building

```bash
mkdir build && cd build
cmake ..
make
```

This builds cmark-gfm from the vendored submodule, then builds libwikimark and the `wikimark` CLI tool.

## Usage

### CLI

```bash
# Convert a file
./wikimark input.wm

# Pipe from stdin
echo '[[Main Page]]' | ./wikimark
```

### C API

```c
#include <wikimark.h>

int main(void) {
    // Register extensions (thread-safe, call once)
    wikimark_extensions_ensure_registered();

    // Convert WikiMark to HTML
    const char *input = "See [[Main Page]] for details.";
    char *html = wikimark_markdown_to_html(input, strlen(input), CMARK_OPT_DEFAULT);

    printf("%s", html);
    wikimark_free(html);

    return 0;
}
```

### With configuration

```c
wikimark_config config = wikimark_config_default();
config.base_url = "/wiki/";

char *html = wikimark_markdown_to_html_with_config(
    input, strlen(input), CMARK_OPT_DEFAULT, &config);
```

### With engine callbacks

Templates, variables, and page embeds resolve through engine-provided
callbacks. Without them, `${...}` and `{{...}}` produce error
indicators — the library is a pure parser and never touches the
filesystem.

```c
const char *my_var(const char *path, void *ud) { /* ... */ }
const char *my_tmpl(const char *name, const char *args, void *ud) { /* ... */ }

wikimark_context ctx = wikimark_context_default();
ctx.resolve_variable = my_var;
ctx.resolve_template = my_tmpl;
ctx.user_data = my_engine;

wikimark_config cfg = wikimark_config_default();
char *html = wikimark_render(input, strlen(input), CMARK_OPT_DEFAULT, &cfg, &ctx);
wikimark_free(html);
```

## Features (implementation status)

All features are implemented and passing the
[WikiMark test suite](../wikimark/tests/) (1,471 / 1,556 tests pass;
the 85 exclusions are documented baseline divergences from cmark-gfm).

| Feature | Status | Spec section | Source |
|---|---|---|---|
| Wiki links `[[Page]]` | Done | §7.1 | `src/wikilink.c` |
| Wiki-style MD links `[text](Page)` | Done | §7.2 | `src/wikilink.c` |
| Page title normalization (ASCII) | Done | §13 | `src/normalize.c` |
| YAML frontmatter | Done | §8 | `src/frontmatter.c` |
| Page variables `${...}` | Done | §8.2 | `src/variable.c`, `src/expand.c` |
| Presentation attributes `{...}` | Done | §10 | `src/attributes.c`, `src/spans.c` |
| Semantic annotations `\|...\|` | Done | §12 | `src/annotation.c` |
| Callouts `> [!TYPE]` | Done | §5.1 | `src/callout.c` |
| Redirects (frontmatter) | Done | §9 | `src/redirect.c` |
| Templates `{{...}}` | Done (via callback) | §11 | `src/template.c`, `src/expand.c` |
| Page embeds `![[...]]` | Done (via callback) | §7.9 | `src/wikilink.c` |

Content resolution (templates, variables, page embeds) is handled
through the `wikimark_context` callbacks — see "With engine
callbacks" above. The library itself never touches the filesystem.
Unicode NFC normalization is not yet implemented (ASCII fast-path
only) — tracked in [PLAN.md](PLAN.md).

## Testing

The test suite uses the [WikiMark specification tests](https://github.com/trellis-wiki/wikimark/tree/main/tests) (1,556 input/output pairs):

```bash
# Run WikiMark-specific tests
python3 ../wikimark/tests/run_tests.py ./wikimark --suite wikimark-spec,wikimark-extra

# Run GFM compliance tests
python3 ../wikimark/tests/run_tests.py ./wikimark --suite gfm-spec,gfm-extensions

# Run all tests
python3 ../wikimark/tests/run_tests.py ./wikimark
```

## Dependencies

- **[cmark-gfm](https://github.com/github/cmark-gfm)** — vendored under `third_party/`
- **[libyaml](https://github.com/yaml/libyaml)** — vendored under `third_party/`, used by `src/frontmatter.c`
- **CMake 3.14+**
- C11 compiler (TLS via `_Thread_local` / `__thread`)

## Specification

This library implements [WikiMark v0.5](https://github.com/trellis-wiki/wikimark/blob/main/spec.md) (draft).

## License

[MIT](LICENSE)
