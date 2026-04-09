#ifndef WIKIMARK_REDIRECT_H
#define WIKIMARK_REDIRECT_H

#include <cmark-gfm.h>
#include "frontmatter.h"

/**
 * Check for redirects (frontmatter redirect: key or #REDIRECT [[Page]]).
 * If found, replaces the document content with a redirect notice.
 * Returns 1 if a redirect was found, 0 otherwise.
 */
int wm_handle_redirect(cmark_node *root, const wm_fm_node *frontmatter,
                        const char *body, cmark_mem *mem);

#endif
