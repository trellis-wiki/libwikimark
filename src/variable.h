#ifndef WIKIMARK_VARIABLE_H
#define WIKIMARK_VARIABLE_H

#include <cmark-gfm.h>
#include "frontmatter.h"

/**
 * Expand ${...} variable references in all text nodes of the AST.
 * Resolves against the frontmatter tree.
 */
void wm_expand_variables(cmark_node *root, const wm_fm_node *frontmatter,
                          cmark_mem *mem);

#endif
