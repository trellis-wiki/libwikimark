# Introduction

This is a plain GFM document with no WikiMark extensions. It exists
to measure the baseline parsing and rendering overhead of the cmark-gfm
pipeline without any wiki-specific work on top.

## Paragraphs and formatting

Wikis contain a lot of prose. Here's a mix of **bold text**, *italic
text*, `inline code`, and [standard links](https://example.com).
Emphasis can be nested — ***bold italic***, **bold with `code` inside**,
and so on.

A longer paragraph lets us measure the per-character overhead of the
inline parser. cmark-gfm's inline parser is the hottest loop during
rendering, so inputs with long paragraphs tend to dominate total
parsing time. Realistic wiki pages alternate between paragraphs of
this length and shorter ones for contrast.

> A blockquote adds another structural element. Wikis use these for
> asides, warnings, and quoted material. GFM blockquotes can contain
> any other block element, including nested lists and code blocks.

## Lists

Bullet lists:

- First item
- Second item with [a link](https://example.org)
- Third item with `inline code`
- Fourth item
  - Nested item
  - Another nested item

Numbered lists:

1. Step one
2. Step two
3. Step three

Task lists (GFM extension):

- [x] Done
- [ ] Not done
- [x] Also done

## Code blocks

```python
def hello(name):
    return f"Hello, {name}!"

for i in range(10):
    print(hello(f"World {i}"))
```

```sql
SELECT id, title, content
FROM pages
WHERE namespace = 'Help'
  AND updated_at > now() - interval '7 days'
ORDER BY updated_at DESC
LIMIT 100;
```

## Tables

| Column A | Column B | Column C |
|----------|----------|----------|
| Value 1  | Value 2  | Value 3  |
| Value 4  | Value 5  | Value 6  |
| Value 7  | Value 8  | Value 9  |
| Value 10 | Value 11 | Value 12 |

## Thematic break

---

## Mixed content

Wikis often have pages with long run-on sections followed by dense
structural elements. This section intentionally stays prose-heavy to
stress the inline parser with normal sentences, followed by a cluster
of structural elements.

Another paragraph with assorted GFM features: ~~strikethrough~~,
autolinks like https://example.com, footnotes[^1], and hard line breaks
(two trailing spaces will do it).

[^1]: A footnote definition.

### Subsection

Text in a subsection. Most wiki pages have several heading levels
interleaved with paragraphs and occasional tables or lists.

### Another subsection

Enough content that the benchmark is not dominated by fixed per-render
overhead. Real wiki pages in the migrated corpus are in this size
range — a few screens of content, not single-line snippets.
