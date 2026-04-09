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
config.case_sensitive = 0;

char *html = wikimark_markdown_to_html_with_config(
    input, strlen(input), CMARK_OPT_DEFAULT, &config);
```

## Features (implementation status)

| Feature | Status | Spec section |
|---|---|---|
| Wiki links `[[Page]]` | **Done** | §7.1 |
| Wiki-style MD links `[text](Page)` | Planned | §7.2 |
| Page title normalization | **Done** | §13 |
| YAML frontmatter | Planned | §8 |
| Page variables `${...}` | Planned | §8.2 |
| Presentation attributes `{...}` | Planned | §10 |
| Semantic annotations `\|...\|` | Planned | §12 |
| Callouts `> [!TYPE]` | Planned | §5.1 |
| Redirects `#REDIRECT` | Planned | §9 |
| Templates `{{...}}` | Planned | §11 |
| Page embeds `![[...]]` | Planned | §7.9 |

## Testing

The test suite uses the [WikiMark specification tests](https://github.com/trellis-wiki/wikimark/tree/main/tests) (1,545 input/output pairs):

```bash
# Run WikiMark-specific tests
python3 ../wikimark/tests/run_tests.py ./wikimark --suite wikimark-spec,wikimark-extra

# Run GFM compliance tests
python3 ../wikimark/tests/run_tests.py ./wikimark --suite gfm-spec,gfm-extensions

# Run all tests
python3 ../wikimark/tests/run_tests.py ./wikimark
```

## Dependencies

- **[cmark-gfm](https://github.com/github/cmark-gfm)** — vendored as a git submodule
- **[libyaml](https://github.com/yaml/libyaml)** — vendored (for frontmatter parsing, not yet integrated)
- **CMake 3.14+**
- C11 compiler

## Specification

This library implements [WikiMark v0.5](https://github.com/trellis-wiki/wikimark/blob/main/spec.md) (draft).

## License

[MIT](LICENSE)
